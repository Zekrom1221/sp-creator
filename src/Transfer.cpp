#include "Transfer.hpp"

#include "Text.hpp"

#include <Geode/utils/file.hpp>
#include <Geode/utils/string.hpp>

#include <ShlObj.h>
#include <commdlg.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

using namespace geode::prelude;
using text::Msg;

namespace {

constexpr char PACK_MAGIC[8] = {'S', 'P', 'C', 'P', 'A', 'C', 'K', 0};
constexpr int PACK_FORMAT = 1;
// Far above what SP files take (BPATA: 7 SPs, 1.7 MB): a file this big is something else.
constexpr size_t MAX_PACK_SIZE = 128 * 1024 * 1024;
constexpr size_t MAX_MANIFEST_SIZE = 1024 * 1024;
constexpr size_t MAX_SP_FILE_SIZE = 32 * 1024 * 1024;
constexpr size_t MAX_ITEMS = 1000;
constexpr size_t MAX_NAME_BYTES = 128;
// An imported SP this close to one of the level's is pointed out: the player may want only one of them.
constexpr float NEAR_PERCENT = 0.5f;
constexpr std::string_view LOW_DETAIL_SUFFIX = "-ldm";

constexpr int MAX_JSON_DEPTH = 32;

auto fail(Msg msg) {
    return Err(std::string(text::tr(msg)));
}

// matjson parses nested arrays and objects by recursion, with no limit: a pack.json nested deep enough
// would overflow the stack, which nothing can catch.
bool shallow(std::string_view json) {
    int depth = 0;
    bool inString = false;
    for (size_t i = 0; i < json.size(); i++) {
        char c = json[i];
        if (inString) {
            if (c == '\\') i++;
            else if (c == '"') inString = false;
        }
        else if (c == '"') inString = true;
        else if (c == '[' || c == '{') {
            if (++depth > MAX_JSON_DEPTH) return false;
        }
        else if (c == ']' || c == '}') depth--;
    }
    return true;
}

std::string_view withoutLowDetail(std::string_view key) {
    if (key.ends_with(LOW_DETAIL_SUFFIX)) key.remove_suffix(LOW_DETAIL_SUFFIX.size());
    return key;
}

std::filesystem::path defaultFolder() {
    PWSTR path = nullptr;
    std::filesystem::path folder;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &path))) folder = path;
    CoTaskMemFree(path);
    std::error_code ec;
    return std::filesystem::is_directory(folder, ec) ? folder : std::filesystem::path();
}

// "<level name>.spcreator", without the characters Windows doesn't allow in names.
std::string fileName(std::string const& levelName) {
    std::string name;
    for (unsigned char c : levelName) {
        if (c < 32 || std::string_view("<>:\"/\\|?*").find(static_cast<char>(c)) != std::string_view::npos) name += '_';
        else name += static_cast<char>(c);
    }
    // Windows drops dots and spaces at the end of a name.
    while (!name.empty() && (name.back() == '.' || name.back() == ' ')) name.pop_back();
    if (name.empty()) name = "SPs";
    return name + transfer::EXTENSION;
}

// Is the file's level the open one, the same version, the same mode? The error says how it isn't.
Result<> checkLevel(matjson::Value const& level, storage::LevelInfo const& open) {
    auto key = level["key"].asString().unwrapOr("");
    auto const& identity = level["identity"];
    if (withoutLowDetail(key) != withoutLowDetail(open.key)) {
        return Err(text::format(Msg::OtherLevel, level["name"].asString().unwrapOr("?")));
    }
    if (key != open.key) return fail(key.ends_with(LOW_DETAIL_SUFFIX) ? Msg::ForLowDetail : Msg::ForFullDetail);
    if (identity["game"] != open.identity["game"]) {
        return Err(text::format(Msg::OtherGame, identity["game"].asString().unwrapOr("?")));
    }
    if (!storage::fits(identity)) return fail(Msg::OtherVersion);
    return Ok();
}

}

namespace transfer {

ByteVector join(Raw const& raw) {
    ByteVector out(PACK_MAGIC, PACK_MAGIC + sizeof(PACK_MAGIC));
    auto put = [&](ByteSpan part) {
        auto size = static_cast<uint32_t>(part.size());
        auto at = reinterpret_cast<uint8_t const*>(&size);
        out.insert(out.end(), at, at + sizeof(size));
        out.insert(out.end(), part.begin(), part.end());
    };
    auto manifest = raw.manifest.dump(0);
    put(ByteSpan(reinterpret_cast<uint8_t const*>(manifest.data()), manifest.size()));
    for (auto const& file : raw.files) put(file);
    return out;
}

Result<Raw> split(ByteSpan bytes) {
    if (bytes.size() < sizeof(PACK_MAGIC) || std::memcmp(bytes.data(), PACK_MAGIC, sizeof(PACK_MAGIC)) != 0) {
        return Err("not a pack");
    }
    size_t at = sizeof(PACK_MAGIC);
    // The next part: its size, then that many bytes.
    auto next = [&](size_t max) -> std::optional<ByteSpan> {
        uint32_t size = 0;
        if (bytes.size() - at < sizeof(size)) return std::nullopt;
        std::memcpy(&size, bytes.data() + at, sizeof(size));
        at += sizeof(size);
        if (size > max || bytes.size() - at < size) return std::nullopt;
        auto part = bytes.subspan(at, size);
        at += size;
        return part;
    };
    auto manifest = next(MAX_MANIFEST_SIZE);
    if (!manifest) return Err("cut off in pack.json");
    std::string_view json(reinterpret_cast<char const*>(manifest->data()), manifest->size());
    if (!shallow(json)) return Err("pack.json nested too deep");
    auto parsed = matjson::parse(json);
    if (parsed.isErr()) return Err(fmt::format("pack.json: {}", parsed.unwrapErr()));
    Raw raw{std::move(parsed).unwrap()};
    while (at < bytes.size()) {
        auto file = next(MAX_SP_FILE_SIZE);
        if (!file || raw.files.size() >= MAX_ITEMS) return Err(fmt::format("cut off in SP {}", raw.files.size() + 1));
        raw.files.emplace_back(file->begin(), file->end());
    }
    return Ok(std::move(raw));
}

Result<ByteVector> pack(std::vector<core::SP> const& sps, int& packed) {
    packed = 0;
    auto level = storage::level();
    if (!level) return fail(Msg::CantSave);
    core::saveAll();
    Raw raw;
    auto entries = matjson::Value::array();
    for (auto const& sp : sps) {
        auto bytes = storage::fileOf(*sp);
        if (bytes.isErr()) {
            log::error("Export: SP {} left out: {}", sp->id, bytes.unwrapErr());
            continue;
        }
        raw.files.push_back(std::move(bytes).unwrap());
        entries.push(matjson::makeObject({{"name", sp->name}, {"percent", sp->percent}}));
        packed++;
    }
    if (packed == 0) return fail(Msg::ExportFailed);
    raw.manifest = matjson::makeObject({
        {"mod", Mod::get()->getID()},
        {"format", PACK_FORMAT},
        {"version", Mod::get()->getVersion().toVString()},
        {"level", matjson::makeObject({{"key", level->key}, {"name", level->name}, {"identity", level->identity}})},
        {"sps", entries},
    });
    return Ok(join(raw));
}

Result<Pack> open(std::filesystem::path const& path) {
    auto level = storage::level();
    if (!level) return fail(Msg::CantSave);
    std::error_code ec;
    auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        log::error("Import: {}: {}", path, ec.message());
        return fail(Msg::NotAPack);
    }
    if (size > MAX_PACK_SIZE) return fail(Msg::PackTooBig);
    auto data = file::readBinary(path);
    if (data.isErr()) {
        log::error("Import: {}: {}", path, data.unwrapErr());
        return fail(Msg::NotAPack);
    }
    // Anything can come in a file someone sent: whatever throws on the way means it's no good.
    try {
        auto const& bytes = data.unwrap();
        if (bytes.size() < sizeof(PACK_MAGIC) || std::memcmp(bytes.data(), PACK_MAGIC, sizeof(PACK_MAGIC)) != 0) {
            return fail(Msg::NotAPack);
        }
        auto raw = split(bytes);
        if (raw.isErr()) {
            log::warn("Import: {}: {}", path, raw.unwrapErr());
            return fail(Msg::PackDamaged);
        }
        auto const& manifest = raw.unwrap().manifest;
        auto& files = raw.unwrap().files;
        if (!manifest.isObject() || manifest["mod"].asString().unwrapOr("") != Mod::get()->getID()) return fail(Msg::NotAPack);
        auto format = manifest["format"].asInt().unwrapOr(0);
        if (format != PACK_FORMAT) return fail(format > PACK_FORMAT ? Msg::NewerFormat : Msg::NotAPack);
        GEODE_UNWRAP(checkLevel(manifest["level"], *level));

        auto const& list = manifest["sps"];
        if (!list.isArray() || list.size() != files.size()) return fail(Msg::PackDamaged);
        auto const& own = core::sps();
        // The level's SPs by content, to see which the file holds; all of them on disk first.
        core::saveAll();
        std::unordered_map<uint64_t, core::SP> owned;
        for (auto const& sp : own) {
            if (auto content = storage::contentOf(*sp)) owned.emplace(content, sp);
        }
        std::unordered_set<uint64_t> seen;
        for (auto const& [content, sp] : owned) seen.insert(content);
        Pack pack;
        for (size_t f = 0; f < files.size(); f++) {
            auto header = storage::readHeader(files[f]);
            if (header.isErr()) {
                log::warn("Import: SP {} of {}: {}", f + 1, path, header.unwrapErr());
                pack.damaged++;
                continue;
            }
            Item item;
            item.header = header.unwrap();
            item.file = std::move(files[f]);
            auto name = list[f]["name"].asString().unwrapOr("");
            if (name.size() <= MAX_NAME_BYTES) item.name = name;
            item.duplicate = !seen.insert(item.header.content).second;
            if (auto it = owned.find(item.header.content); it != owned.end()) {
                if (std::find(pack.inFile.begin(), pack.inFile.end(), it->second) == pack.inFile.end()) {
                    pack.inFile.push_back(it->second);
                }
            }
            for (size_t i = 0; i < own.size() && !item.duplicate; i++) {
                float distance = std::abs(own[i]->percent - item.header.percent);
                if (distance <= NEAR_PERCENT && (item.nearby == 0 || distance < std::abs(item.nearbyPercent - item.header.percent))) {
                    item.nearby = static_cast<int>(i) + 1;
                    item.nearbyPercent = own[i]->percent;
                }
            }
            pack.items.push_back(std::move(item));
        }
        if (pack.items.empty()) return fail(Msg::PackDamaged);
        std::stable_sort(pack.items.begin(), pack.items.end(), [](auto const& a, auto const& b) { return a.header.x < b.header.x; });
        log::info("Import: {}: {} SPs, {} damaged", path, pack.items.size(), pack.damaged);
        return Ok(std::move(pack));
    } catch (std::exception const& e) {
        log::error("Import: {}: {}", path, e.what());
        return fail(Msg::PackDamaged);
    }
}

core::ImportResult take(Pack const& pack, std::vector<bool> const& picked, bool replace) {
    std::vector<core::Incoming> files;
    for (size_t i = 0; i < pack.items.size() && i < picked.size(); i++) {
        if (picked[i]) files.push_back({pack.items[i].file, pack.items[i].name});
    }
    return core::import(files, replace, pack.inFile);
}

std::optional<std::filesystem::path> chooseFile(bool save, std::string const& levelName) {
    // Wide strings with the parts the dialog wants separated by nulls.
    auto filter = utils::string::utf8ToWide(fmt::format("{} (*{})", text::tr(Msg::FileType), EXTENSION));
    filter += L'\0';
    filter += utils::string::utf8ToWide(fmt::format("*{}", EXTENSION));
    filter += L'\0';
    std::vector<wchar_t> file(32768, L'\0');
    if (save) {
        auto name = utils::string::utf8ToWide(fileName(levelName));
        std::copy_n(name.begin(), std::min(name.size(), file.size() - 1), file.begin());
    }
    auto folder = defaultFolder();
    auto extension = utils::string::utf8ToWide(EXTENSION + 1);

    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = GetActiveWindow();
    dialog.lpstrFilter = filter.c_str();
    dialog.lpstrFile = file.data();
    dialog.nMaxFile = static_cast<DWORD>(file.size());
    dialog.lpstrInitialDir = folder.empty() ? nullptr : folder.c_str();
    dialog.lpstrDefExt = extension.c_str();
    dialog.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    // The game finds its files from the current folder, which the open dialog changes whatever the flags say.
    std::error_code ec;
    auto current = std::filesystem::current_path(ec);
    BOOL chosen = save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog);
    if (!current.empty()) std::filesystem::current_path(current, ec);
    if (!chosen) {
        if (auto error = CommDlgExtendedError()) log::error("File dialog failed: {:#x}", error);
        return std::nullopt;
    }
    std::filesystem::path path(file.data());
    // A name typed with another extension ("sps.txt") gets the mod's added, which the dialog's overwrite
    // question didn't cover: then a name no file has.
    if (save && utils::string::toLower(utils::string::pathToString(path.extension())) != EXTENSION) {
        auto base = path;
        path += EXTENSION;
        for (int n = 2; std::filesystem::exists(path, ec); n++) {
            path = base;
            path += fmt::format(" ({}){}", n, EXTENSION);
        }
    }
    return path;
}

}
