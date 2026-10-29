#include "Transfer.hpp"

#include "Text.hpp"

#include <Geode/utils/file.hpp>
#include <Geode/utils/string.hpp>

#include <ShlObj.h>
#include <shobjidl.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <thread>
#include <unordered_map>
#include <unordered_set>

// wglGetCurrentDC, for the game's window.
#pragma comment(lib, "opengl32.lib")

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

// The folder of the file last picked in one of the mod's file dialogs.
constexpr char const* LAST_FOLDER = "file-folder";

// Where the file dialogs open: the folder of the file last picked, or Downloads.
std::filesystem::path startFolder() {
    auto last = Mod::get()->getSavedValue<std::filesystem::path>(LAST_FOLDER);
    std::error_code ec;
    return !last.empty() && std::filesystem::is_directory(last, ec) ? last : defaultFolder();
}

// The level's name, without the characters Windows doesn't allow in names.
std::string fileStem(std::string const& levelName) {
    std::string name;
    for (unsigned char c : levelName) {
        if (c < 32 || std::string_view("<>:\"/\\|?*").find(static_cast<char>(c)) != std::string_view::npos) name += '_';
        else name += static_cast<char>(c);
    }
    // Windows drops dots and spaces at the end of a name.
    while (!name.empty() && (name.back() == '.' || name.back() == ' ')) name.pop_back();
    if (name.empty()) name = "SPs";
    return name;
}

// The name the save dialog offers in `folder`: "<stem>.spcreator", or "<stem> (2).spcreator" and on when
// that's taken (one level's SPs exported more than once).
std::wstring freeName(std::filesystem::path const& folder, std::wstring const& stem) {
    auto extension = utils::string::utf8ToWide(transfer::EXTENSION);
    auto name = stem + extension;
    std::error_code ec;
    for (int n = 2; !folder.empty() && std::filesystem::exists(folder / name, ec); n++) {
        name = stem + L" (" + std::to_wstring(n) + L")" + extension;
    }
    return name;
}

std::atomic<bool> g_dialogOpen = false;

// On the dialog's own thread. Like Geode's (nfd): the common item dialog, which leaves the current folder
// alone (the game finds its files from it).
std::optional<std::filesystem::path> showDialog(bool save, HWND owner, std::wstring const& typeName, std::wstring const& name,
    std::filesystem::path const& folder, HRESULT& result) {
    std::optional<std::filesystem::path> chosen;
    result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(result)) return chosen;
    IFileDialog* dialog = nullptr;
    result = CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&dialog));
    if (SUCCEEDED(result)) {
        auto pattern = utils::string::utf8ToWide(fmt::format("*{}", transfer::EXTENSION));
        COMDLG_FILTERSPEC type{typeName.c_str(), pattern.c_str()};
        dialog->SetFileTypes(1, &type);
        dialog->SetDefaultExtension(pattern.c_str() + 2);
        FILEOPENDIALOGOPTIONS options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | (save ? FOS_OVERWRITEPROMPT : FOS_FILEMUSTEXIST));
        IShellItem* item = nullptr;
        // Always this folder, not the one Windows would pick: the save dialog's name is free in it.
        if (!folder.empty() && SUCCEEDED(SHCreateItemFromParsingName(folder.c_str(), nullptr, IID_PPV_ARGS(&item)))) {
            dialog->SetFolder(item);
            item->Release();
            item = nullptr;
        }
        if (save) dialog->SetFileName(name.c_str());
        result = dialog->Show(owner);
        if (SUCCEEDED(result)) result = dialog->GetResult(&item);
        if (SUCCEEDED(result)) {
            PWSTR path = nullptr;
            result = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
            if (SUCCEEDED(result)) chosen = std::filesystem::path(path);
            CoTaskMemFree(path);
            item->Release();
        }
        dialog->Release();
    }
    CoUninitialize();
    return chosen;
}

// A name typed with another extension ("sps.txt") gets the mod's added, which the dialog's overwrite
// question didn't cover: then a name no file has.
std::filesystem::path withExtension(std::filesystem::path path) {
    if (utils::string::toLower(utils::string::pathToString(path.extension())) == transfer::EXTENSION) return path;
    auto base = path;
    path += transfer::EXTENSION;
    std::error_code ec;
    for (int n = 2; std::filesystem::exists(path, ec); n++) {
        path = base;
        path += fmt::format(" ({}){}", n, transfer::EXTENSION);
    }
    return path;
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

// The items of SP files (with their names) for the open level, against its SPs; `from` for the log.
transfer::Pack collect(std::vector<ByteVector>& files, std::vector<std::string> const& names, std::string const& from) {
    auto const& own = core::sps();
    // The level's SPs by content, to see which the file holds; all of them on disk first.
    core::saveAll();
    std::unordered_map<uint64_t, std::string> owned;  // content: name
    for (auto const& sp : own) {
        if (auto content = storage::contentOf(*sp)) owned.emplace(content, sp->name);
    }
    std::unordered_set<uint64_t> seen;
    transfer::Pack pack;
    for (size_t f = 0; f < files.size(); f++) {
        auto header = storage::readHeader(files[f]);
        if (header.isErr()) {
            log::warn("Import: SP {} of {}: {}", f + 1, from, header.unwrapErr());
            pack.damaged++;
            continue;
        }
        transfer::Item item;
        item.header = header.unwrap();
        item.file = std::move(files[f]);
        if (f < names.size() && names[f].size() <= MAX_NAME_BYTES) item.name = names[f];
        item.repeated = !seen.insert(item.header.content).second;
        item.here = owned.contains(item.header.content);
        for (size_t i = 0; i < own.size() && !item.here && !item.repeated; i++) {
            float distance = std::abs(own[i]->percent - item.header.percent);
            if (distance <= NEAR_PERCENT && (item.nearby == 0 || distance < std::abs(item.nearbyPercent - item.header.percent))) {
                item.nearby = static_cast<int>(i) + 1;
                item.nearbyPercent = own[i]->percent;
            }
        }
        pack.items.push_back(std::move(item));
    }
    // Every SP of the level in the file, by the same names, and nothing else: taking it would change nothing.
    // (Own SPs whose content couldn't be read count as not in it.) Other names: replacing brings them back.
    pack.sameAsLevel = !pack.items.empty() && owned.size() == own.size() &&
        std::all_of(owned.begin(), owned.end(), [&](auto const& o) { return seen.contains(o.first); }) &&
        std::all_of(pack.items.begin(), pack.items.end(),
            [&](auto const& item) { return item.here && item.name == owned.at(item.header.content); });
    std::stable_sort(pack.items.begin(), pack.items.end(), [](auto const& a, auto const& b) { return a.header.x < b.header.x; });
    log::info("Import: {}: {} SPs, {} damaged", from, pack.items.size(), pack.damaged);
    return pack;
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
        std::vector<std::string> names;
        for (size_t f = 0; f < files.size(); f++) names.push_back(list[f]["name"].asString().unwrapOr(""));
        auto pack = collect(files, names, utils::string::pathToString(path));
        if (pack.items.empty()) return fail(Msg::PackDamaged);
        return Ok(std::move(pack));
    } catch (std::exception const& e) {
        log::error("Import: {}: {}", path, e.what());
        return fail(Msg::PackDamaged);
    }
}

Result<Pack> openVersion(storage::Version const& version) {
    std::vector<storage::VersionSP> sps;
    std::vector<float> notMoved;
    if (!version.fits && version.movable) {
        auto moved = storage::moveVersion(version);
        if (moved.isErr()) {
            log::error("Version {}: {}", version.dir, moved.unwrapErr());
            return fail(Msg::OtherVersion);
        }
        sps = std::move(moved.unwrap().sps);
        notMoved = std::move(moved.unwrap().notMoved);
        if (sps.empty()) return Err(text::format(Msg::NoneMoved, text::percents(notMoved)));
    }
    else {
        auto read = storage::readVersion(version);
        if (read.isErr()) {
            log::error("Version {}: {}", version.dir, read.unwrapErr());
            return fail(version.fits ? Msg::VersionUnreadable : Msg::OtherVersion);
        }
        sps = std::move(read).unwrap();
    }
    std::vector<ByteVector> files;
    std::vector<std::string> names;
    for (auto& sp : sps) {
        files.push_back(std::move(sp.file));
        names.push_back(std::move(sp.name));
    }
    auto pack = collect(files, names, utils::string::pathToString(version.dir));
    if (pack.items.empty()) return fail(Msg::VersionUnreadable);
    pack.movedOver = !version.fits;
    pack.notMoved = std::move(notMoved);
    return Ok(std::move(pack));
}

core::ImportResult take(Pack const& pack, std::vector<bool> const& picked, core::Replace replace) {
    std::vector<core::Incoming> files;
    for (size_t i = 0; i < pack.items.size() && i < picked.size(); i++) {
        auto const& item = pack.items[i];
        if (!picked[i] || item.repeated || (item.here && replace == core::Replace::No)) continue;
        files.push_back({item.file, item.name});
    }
    return core::import(files, replace);
}

bool chooseFile(bool save, std::string const& levelName, geode::Function<void(std::optional<std::filesystem::path>)> then) {
    if (g_dialogOpen.exchange(true)) {
        log::info("A file dialog is open already");
        return false;
    }
    log::info("File dialog: {}", save ? "save" : "open");
    // The game's window owns the dialog: it takes no clicks meanwhile (Geode finds it the same way).
    HWND owner = WindowFromDC(wglGetCurrentDC());
    auto typeName = utils::string::utf8ToWide(fmt::format("{} (*{})", text::tr(Msg::FileType), EXTENSION));
    auto folder = startFolder();
    auto name = save ? freeName(folder, utils::string::utf8ToWide(fileStem(levelName))) : std::wstring();
    std::thread([=, then = std::move(then)]() mutable {
        HRESULT result = S_OK;
        auto path = showDialog(save, owner, typeName, name, folder, result);
        geode::queueInMainThread([path = std::move(path), result, save, then = std::move(then)]() mutable {
            g_dialogOpen = false;
            if (FAILED(result) && result != HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
                log::error("File dialog failed: {:#x}", static_cast<uint32_t>(result));
            }
            if (path && save) path = withExtension(*path);
            if (path) Mod::get()->setSavedValue(LAST_FOLDER, path->parent_path());
            then(std::move(path));
        });
    }).detach();
    return true;
}

}
