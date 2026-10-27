#include "Storage.hpp"

#include <Geode/utils/file.hpp>
#include <Geode/utils/string.hpp>

// PersistenceAPI marks everything __declspec(dllimport), templates defined in its headers too, which
// MSVC refuses (C2491; clang-cl only warns). Without the attribute the templates are compiled here and
// the functions PersistenceAPI exports are reached through its import library's stubs all the same.
#include <sabe.persistenceapi/include/import_export.hpp>
#undef SABE_PA_DLL
#define SABE_PA_DLL

#include <sabe.persistenceapi/include/hooks/EffectManagerState.hpp>
#include <sabe.persistenceapi/include/hooks/FMODAudioState.hpp>
#include <sabe.persistenceapi/include/hooks/GJGameState.hpp>
#include <sabe.persistenceapi/include/hooks/GJShaderState.hpp>
#include <sabe.persistenceapi/include/hooks/PlayerCheckpoint.hpp>
#include <sabe.persistenceapi/include/hooks/cocos2d/CCArray.hpp>
#include <sabe.persistenceapi/include/hooks/cocos2d/CCNode.hpp>
#include <sabe.persistenceapi/include/util/Stream.hpp>

#include <charconv>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <fstream>

using namespace geode::prelude;
using namespace persistenceAPI;

namespace {

constexpr char MAGIC[8] = {'S', 'P', 'C', 'R', 'E', 'A', 'T', 'R'};
constexpr int FORMAT_VERSION = 2;
// PersistenceAPI's own format version, the one Practice Checkpoint Permanence writes with.
constexpr int PA_VERSION = 2;
constexpr unsigned int END_MARKER = 0x21505321;  // "!SP!"
// After the body: its hash and the end marker.
constexpr size_t SEAL_SIZE = sizeof(uint64_t) + sizeof(END_MARKER);
// Sanity limits when reading: an auto part is at most a few seconds of steps.
constexpr unsigned int MAX_FRAMES = 4000;
constexpr unsigned int MAX_INPUTS = 4000;
constexpr int MAX_ID = 1000000;

// PersistenceAPI saves a reference to a level object as its m_uniqueID - 12, and turns it back into
// m_objects[index] when loading. That only holds if the level's objects get unique IDs 12, 13, ... in
// m_objects order, in every session. PersistenceAPI resets the global counter in PlayLayer::init, but
// anything created between that and the level's objects would shift them, so the counter is set again
// right before the level's objects, as Practice Checkpoint Permanence does. It's the counter
// GameObject::resetMID writes (Geode bindings, GD 2.2081 Windows); load() checks the result.
constexpr uintptr_t UNIQUE_ID_COUNTER = 0x6ba158;
constexpr int FIRST_OBJECT_ID = 12;

std::filesystem::path g_root = "levels";

struct Level {
    std::filesystem::path dir;  // empty: this level's SPs aren't saved
    uint64_t stamp = 0;         // level key and version, written into every SP file
} g_level;

// persistenceAPI::Stream opens files by a std::string path, which MSVC reads in the ANSI code page: a
// save folder with letters outside it (a Cyrillic Windows user name) wouldn't open. This opens by path.
class FileStream : public Stream {
public:
    bool open(std::filesystem::path const& path, bool write, unsigned int* bytesRead = nullptr) {
        delete m_stream;
        m_stream = new std::fstream(path, std::ios::binary | (write ? std::ios::out | std::ios::trunc : std::ios::in));
        m_bytesRead = bytesRead;
        m_PAVersion = PA_VERSION;
        return m_stream->good();
    }

    bool good() const {
        return m_stream && m_stream->good();
    }
};

uint64_t hashBytes(void const* data, size_t size) {
    uint64_t h = 1469598103934665603ull;  // FNV-1a
    auto bytes = static_cast<unsigned char const*>(data);
    for (size_t i = 0; i < size; i++) {
        h ^= bytes[i];
        h *= 1099511628211ull;
    }
    return h;
}

// Main and online levels by their ID; editor levels by their content, so an edit leaves the SPs behind
// like an update does.
std::string levelKey(GJGameLevel* level) {
    int id = level->m_levelID.value();
    switch (level->m_levelType) {
        case GJLevelType::Main: return fmt::format("main-{}", id);
        case GJLevelType::Editor: {
            auto const& s = level->m_levelString;
            return fmt::format("editor-{:016x}", hashBytes(s.c_str(), s.size()));
        }
        default: return id > 0 ? fmt::format("online-{}", id) : std::string();
    }
}

matjson::Value identity(PlayLayer* pl) {
    return matjson::makeObject({
        {"format", FORMAT_VERSION},
        {"game", Loader::get()->getGameVersion()},
        {"platform", GEODE_PLATFORM_SHORT_IDENTIFIER},
        {"name", std::string(pl->m_level->m_levelName)},
        {"version", pl->m_level->m_levelVersion},
        {"objects", static_cast<int>(pl->m_objects->count())},
    });
}

// What saved checkpoints depend on: the same level version, game version and platform (not the name).
constexpr std::array<char const*, 5> IDENTITY_KEYS = {"format", "game", "platform", "version", "objects"};

bool sameLevel(matjson::Value const& saved, matjson::Value const& now) {
    if (!saved.isObject()) return false;
    for (auto key : IDENTITY_KEYS) {
        if (!saved.contains(key) || saved[key] != now[key]) return false;
    }
    return true;
}

uint64_t levelStamp(std::string const& key, matjson::Value const& info) {
    auto s = key;
    for (auto k : IDENTITY_KEYS) s += "|" + info[k].dump(0);
    return hashBytes(s.data(), s.size());
}

// Index of the first level object whose unique ID doesn't follow the order above, -1 if none.
int firstObjectOutOfOrder(PlayLayer* pl) {
    auto objs = pl->m_objects;
    int first = -1, count = 0;
    for (unsigned int i = 0; i < objs->count(); i++) {
        auto obj = static_cast<GameObject*>(objs->objectAtIndex(i));
        if (obj->m_uniqueID == FIRST_OBJECT_ID + static_cast<int>(i)) continue;
        if (first < 0) first = static_cast<int>(i);
        if (++count <= 5) {
            log::warn("Object {} of {}: unique ID {}, expected {}; object ID {}, {} at {:.0f},{:.0f}", i, objs->count(),
                obj->m_uniqueID, FIRST_OBJECT_ID + i, obj->m_objectID, typeid(*obj).name(), obj->getPositionX(),
                obj->getPositionY());
        }
    }
    if (count > 0) log::warn("{} objects out of order", count);
    return first;
}

std::filesystem::path spPath(int id) {
    return g_level.dir / fmt::format("sp-{}.spc", id);
}

// The SP id in a file name like sp-12.spc, 0 if it isn't one.
int idFromName(std::filesystem::path const& path) {
    if (path.extension() != ".spc") return 0;
    // u8string: string() throws on names outside the ANSI code page.
    auto u8 = path.stem().u8string();
    std::string stem(u8.begin(), u8.end());
    if (!stem.starts_with("sp-")) return 0;
    int id = 0;
    auto [end, ec] = std::from_chars(stem.data() + 3, stem.data() + stem.size(), id);
    if (ec != std::errc() || end != stem.data() + stem.size() || id <= 0 || id > MAX_ID) return 0;
    return id;
}

// Names live apart from the SP files: renaming doesn't rewrite a checkpoint.
constexpr size_t MAX_NAME_BYTES = 128;

std::filesystem::path namesPath() {
    return g_level.dir / "names.json";
}

void readNames(std::vector<std::shared_ptr<StartPoint>> const& sps) {
    std::error_code ec;
    if (!std::filesystem::exists(namesPath(), ec)) return;
    auto res = file::readJson(namesPath());
    if (res.isErr() || !res.unwrap().isObject()) {
        log::warn("Can't read {}: {}", namesPath(), res.isErr() ? res.unwrapErr() : std::string("not an object"));
        return;
    }
    auto const& names = res.unwrap();
    size_t named = 0;
    for (auto const& sp : sps) {
        auto name = names[fmt::to_string(sp->id)].asString();
        if (name.isOk() && name.unwrap().size() <= MAX_NAME_BYTES) {
            sp->name = name.unwrap();
            named++;
        }
    }
    // Names of SPs that are gone (a damaged file set aside): a new SP could get the id, and the name with it.
    if (names.size() > named) storage::saveNames(sps);
}

void writePlayer(Stream& out, PlayerFrame f) {
    out << f.y;
    out << f.yVel;
    out << f.rot;
}

void readPlayer(Stream& in, PlayerFrame& f) {
    in >> f.y;
    in >> f.yVel;
    in >> f.rot;
}

void writeCheckpoint(Stream& out, CheckpointObject* cp) {
    reinterpret_cast<PACCNode*>(cp)->save(out);
    bool hasP2 = cp->m_player2Checkpoint != nullptr;
    bool hasGradients = cp->m_gradientTriggerObjectArray != nullptr;
    out << hasP2;
    out << hasGradients;
    reinterpret_cast<PAGJGameState*>(&cp->m_gameState)->save(out);
    reinterpret_cast<PAGJShaderState*>(&cp->m_shaderState)->save(out);
    reinterpret_cast<PAFMODAudioState*>(&cp->m_audioState)->save(out);
    reinterpret_cast<PAPlayerCheckpoint*>(cp->m_player1Checkpoint)->save(out);
    if (hasP2) reinterpret_cast<PAPlayerCheckpoint*>(cp->m_player2Checkpoint)->save(out);
    out << cp->m_unke78;
    out << cp->m_unke7c;
    out << cp->m_unke80;
    out << cp->m_ground2Invisible;
    out << cp->m_streakBlend;
    out << cp->m_uniqueID;
    out << cp->m_respawnID;
    out << cp->m_vectorSavedObjectStateRef;
    out << cp->m_vectorActiveSaveObjectState;
    out << cp->m_vectorSpecialSaveObjectState;
    reinterpret_cast<PAEffectManagerState*>(&cp->m_effectManagerState)->save(out);
    if (hasGradients) static_cast<PACCArray*>(cp->m_gradientTriggerObjectArray)->save<GradientTriggerObject>(out);
    out << cp->m_unk11e8;
    out << cp->m_sequenceTriggerStateUnorderedMap;
    out << cp->m_commandIndex;
}

CheckpointObject* readCheckpoint(Stream& in) {
    auto cp = CheckpointObject::create();
    reinterpret_cast<PACCNode*>(cp)->load(in);
    bool hasP2 = false, hasGradients = false;
    in >> hasP2;
    in >> hasGradients;
    reinterpret_cast<PAGJGameState*>(&cp->m_gameState)->load(in);
    reinterpret_cast<PAGJShaderState*>(&cp->m_shaderState)->load(in);
    reinterpret_cast<PAFMODAudioState*>(&cp->m_audioState)->load(in);
    // The checkpoint's destructor releases these.
    cp->m_player1Checkpoint = PlayerCheckpoint::create();
    CC_SAFE_RETAIN(cp->m_player1Checkpoint);
    reinterpret_cast<PAPlayerCheckpoint*>(cp->m_player1Checkpoint)->load(in);
    if (hasP2) {
        cp->m_player2Checkpoint = PlayerCheckpoint::create();
        CC_SAFE_RETAIN(cp->m_player2Checkpoint);
        reinterpret_cast<PAPlayerCheckpoint*>(cp->m_player2Checkpoint)->load(in);
    }
    in >> cp->m_unke78;
    in >> cp->m_unke7c;
    in >> cp->m_unke80;
    in >> cp->m_ground2Invisible;
    in >> cp->m_streakBlend;
    in >> cp->m_uniqueID;
    in >> cp->m_respawnID;
    in >> cp->m_vectorSavedObjectStateRef;
    in >> cp->m_vectorActiveSaveObjectState;
    in >> cp->m_vectorSpecialSaveObjectState;
    reinterpret_cast<PAEffectManagerState*>(&cp->m_effectManagerState)->load(in);
    if (hasGradients) {
        cp->m_gradientTriggerObjectArray = CCArray::create();
        CC_SAFE_RETAIN(cp->m_gradientTriggerObjectArray);
        static_cast<PACCArray*>(cp->m_gradientTriggerObjectArray)->load<GradientTriggerObject>(in);
    }
    in >> cp->m_unk11e8;
    in >> cp->m_sequenceTriggerStateUnorderedMap;
    in >> cp->m_commandIndex;
    return cp;
}

// Body: magic, format, level stamp, the SP's own data, the checkpoint. save() then seals it.
void writeSP(Stream& out, StartPoint const& sp) {
    char magic[sizeof(MAGIC)];
    std::memcpy(magic, MAGIC, sizeof(MAGIC));
    out.write(magic, sizeof(magic));
    int format = FORMAT_VERSION, id = sp.id, spawnStep = sp.spawnStep;
    unsigned long long stamp = g_level.stamp;
    float x = sp.x, percent = sp.percent;
    out << format;
    out << stamp;
    out << id;
    out << x;
    out << percent;
    out << spawnStep;
    for (int i = 1; i < 7; i++) {
        bool held = sp.holds[i];
        out << held;
    }
    unsigned int frames = static_cast<unsigned int>(sp.frames.size());
    out << frames;
    for (auto f : sp.frames) {
        out << f.x;
        writePlayer(out, f.p1);
        writePlayer(out, f.p2);
        out << f.dual;
        out << f.camX;
        out << f.camY;
    }
    unsigned int inputs = static_cast<unsigned int>(sp.inputs.size());
    out << inputs;
    for (auto in : sp.inputs) {
        out << in.step;
        out << in.button;
        out << in.p1;
        out << in.down;
    }
    writeCheckpoint(out, sp.cp);
}

enum class ReadError { None, Damaged, Other };

std::shared_ptr<StartPoint> readSP(std::filesystem::path const& path, int id, std::string& error, ReadError& kind) {
    kind = ReadError::Other;
    // PersistenceAPI checks nothing it reads, so the whole file is checked against its seal first:
    // past that it is exactly what save() wrote.
    auto data = file::readBinary(path);
    if (data.isErr()) {
        error = fmt::format("can't read it: {}", data.unwrapErr());
        return nullptr;
    }
    auto const& bytes = data.unwrap();
    if (bytes.size() < sizeof(MAGIC) + 64 + SEAL_SIZE) {
        kind = ReadError::Damaged;
        error = "too short";
        return nullptr;
    }
    size_t bodySize = bytes.size() - SEAL_SIZE;
    uint64_t hash = 0;
    unsigned int end = 0;
    std::memcpy(&hash, bytes.data() + bodySize, sizeof(hash));
    std::memcpy(&end, bytes.data() + bodySize + sizeof(hash), sizeof(end));
    if (end != END_MARKER || hash != hashBytes(bytes.data(), bodySize)) {
        kind = ReadError::Damaged;
        error = "damaged (cut off or changed)";
        return nullptr;
    }

    unsigned int bytesRead = 0;
    FileStream in;
    if (!in.open(path, false, &bytesRead)) {
        error = "can't open it";
        return nullptr;
    }
    char magic[sizeof(MAGIC)] = {};
    in.read(magic, sizeof(magic));
    int format = 0;
    unsigned long long stamp = 0;
    in >> format;
    in >> stamp;
    if (std::memcmp(magic, MAGIC, sizeof(MAGIC)) != 0 || format != FORMAT_VERSION) {
        error = fmt::format("not an SP file of format {}", FORMAT_VERSION);
        return nullptr;
    }
    if (stamp != g_level.stamp) {
        error = "made for another level or version";
        return nullptr;
    }
    auto sp = std::make_shared<StartPoint>();
    in >> sp->id;
    sp->id = id;  // the file name decides, so save() and remove() always reach this file
    in >> sp->x;
    in >> sp->percent;
    in >> sp->spawnStep;
    for (int i = 1; i < 7; i++) in >> sp->holds[i];
    unsigned int frames = 0;
    in >> frames;
    if (frames == 0 || frames > MAX_FRAMES || !std::isfinite(sp->x) || !std::isfinite(sp->percent)) {
        error = fmt::format("{} auto part steps, x {}", frames, sp->x);
        return nullptr;
    }
    sp->frames.resize(frames);
    for (auto& f : sp->frames) {
        in >> f.x;
        readPlayer(in, f.p1);
        readPlayer(in, f.p2);
        in >> f.dual;
        in >> f.camX;
        in >> f.camY;
    }
    unsigned int inputs = 0;
    in >> inputs;
    if (inputs > MAX_INPUTS) {
        error = fmt::format("{} inputs", inputs);
        return nullptr;
    }
    sp->inputs.resize(inputs);
    for (auto& input : sp->inputs) {
        in >> input.step;
        in >> input.button;
        in >> input.p1;
        in >> input.down;
    }
    if (bytesRead >= bodySize) {
        error = "no checkpoint";
        return nullptr;
    }
    sp->cp = readCheckpoint(in);
    if (bytesRead != bodySize) {
        error = fmt::format("the checkpoint read {} bytes, the file holds {}", bytesRead, bodySize);
        return nullptr;
    }
    in.end();
    kind = ReadError::None;
    return sp;
}

// A name for the folder of SPs made for another level version that no folder has yet.
std::filesystem::path asideName(std::filesystem::path const& dir) {
    std::error_code ec;
    for (int n = 0;; n++) {
        auto aside = dir;
        aside += n == 0 ? fmt::format("-old-{}", std::time(nullptr)) : fmt::format("-old-{}-{}", std::time(nullptr), n);
        if (!std::filesystem::exists(aside, ec)) return aside;
    }
}

}

namespace storage {

void setRoot(std::string_view utf8) {
    g_root = utils::string::utf8ToWide(utf8);
}

void beforeCreatingObjects() {
    *reinterpret_cast<int*>(geode::base::get() + UNIQUE_ID_COUNTER) = FIRST_OBJECT_ID;
}

Loaded load(PlayLayer* pl) {
    Loaded out;
    g_level = {};
    auto key = levelKey(pl->m_level);
    if (key.empty()) return out;
    // The game adds its anticheat spike (a spike at 0,105) after the level's objects, with an ID one past
    // its place in m_objects. Give it the ID of its place, which no other object has (the check below
    // makes sure), so that references to it load too.
    if (auto spike = pl->m_anticheatSpike) {
        int index = static_cast<int>(pl->m_objects->indexOfObject(spike));
        if (index >= 0) spike->m_uniqueID = FIRST_OBJECT_ID + index;
    }
    int bad = firstObjectOutOfOrder(pl);
    if (bad >= 0) {
        log::error("SPs of {} can't be saved: object {} has unique ID {}, expected {}", key, bad,
            static_cast<GameObject*>(pl->m_objects->objectAtIndex(bad))->m_uniqueID, FIRST_OBJECT_ID + bad);
        out.notice = Notice::CantSave;
        return out;
    }

    auto dir = Mod::get()->getSaveDir() / g_root / key;
    auto info = identity(pl);
    auto infoPath = dir / "level.json";
    std::error_code ec;
    if (std::filesystem::exists(infoPath, ec)) {
        auto saved = file::readJson(infoPath);
        if (saved.isErr() || !sameLevel(saved.unwrap(), info)) {
            // Another version of the level (or of the game): keep its SPs aside.
            auto aside = asideName(dir);
            std::filesystem::rename(dir, aside, ec);
            log::warn("SPs of {} were made for {}, this is {}: moved to {} ({})", key,
                saved.isOk() ? saved.unwrap().dump(0) : std::string("an unreadable level.json"), info.dump(0), aside,
                ec ? ec.message() : "ok");
            if (ec) {
                // Loading them would put them into the wrong version, saving would mix the two.
                out.notice = Notice::CantMoveAside;
                return out;
            }
            out.notice = Notice::LevelChanged;
        }
    }
    std::filesystem::create_directories(dir, ec);
    if (!std::filesystem::exists(infoPath, ec)) {
        auto res = file::writeStringSafe(infoPath, info.dump());
        if (res.isErr()) {
            log::error("Can't write {}: {}", infoPath, res.unwrapErr());
            return out;
        }
    }
    g_level.dir = dir;
    g_level.stamp = levelStamp(key, info);

    auto start = std::chrono::steady_clock::now();
    int failed = 0;
    std::vector<std::filesystem::path> files;
    for (auto it = std::filesystem::directory_iterator(dir, ec); !ec && it != std::filesystem::directory_iterator();
         it.increment(ec)) {
        files.push_back(it->path());
    }
    for (auto const& path : files) {
        if (path.extension() == ".tmp") {
            std::filesystem::remove(path, ec);  // left by a crash while saving
            continue;
        }
        int id = idFromName(path);
        if (id == 0) continue;
        out.nextId = std::max(out.nextId, id + 1);
        std::string error;
        ReadError kind;
        auto sp = readSP(path, id, error, kind);
        if (!sp) {
            failed++;
            log::error("SP file {} of {} not loaded: {}", path.filename(), key, error);
            if (kind == ReadError::Damaged) {
                // Kept for a look, but out of the way so the warning doesn't come back every time.
                auto aside = path;
                aside += ".bad";
                std::filesystem::rename(path, aside, ec);
            }
            continue;
        }
        out.sps.push_back(sp);
    }
    std::sort(out.sps.begin(), out.sps.end(), [](auto const& a, auto const& b) { return a->x < b->x; });
    readNames(out.sps);
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    log::info("{}: {} SPs loaded in {:.1f} ms, {} failed", key, out.sps.size(), ms, failed);
    out.failed = failed;
    if (failed > 0) out.notice = Notice::Failed;
    else if (!out.sps.empty() && out.notice == Notice::None) out.notice = Notice::Loaded;
    return out;
}

bool save(StartPoint const& sp) {
    if (g_level.dir.empty() || !sp.cp || !sp.cp->m_player1Checkpoint) return false;
    auto start = std::chrono::steady_clock::now();
    auto path = spPath(sp.id);
    auto temp = path;
    temp += ".tmp";
    std::error_code ec;
    auto fail = [&](std::string const& why) {
        log::error("Can't save SP {} to {}: {}", sp.id, path, why);
        std::filesystem::remove(temp, ec);
        return false;
    };
    {
        FileStream out;
        if (!out.open(temp, true)) return fail("can't create the file");
        writeSP(out, sp);
        bool written = out.good();
        out.end();  // closed before fail() deletes it: Windows doesn't delete open files
        if (!written) return fail("write failed");
    }
    // Seal the body with its hash, which readSP checks before PersistenceAPI reads anything.
    auto body = file::readBinary(temp);
    if (body.isErr()) return fail(body.unwrapErr());
    uint64_t hash = hashBytes(body.unwrap().data(), body.unwrap().size());
    {
        std::ofstream seal(temp, std::ios::binary | std::ios::app);
        seal.write(reinterpret_cast<char const*>(&hash), sizeof(hash));
        seal.write(reinterpret_cast<char const*>(&END_MARKER), sizeof(END_MARKER));
        seal.close();
        if (!seal) return fail("write failed");
    }
    // Written in full, then put in place: a crash while saving leaves no damaged SP behind.
    std::filesystem::rename(temp, path, ec);
    if (ec) return fail(ec.message());
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    log::info("SP {} saved: {} KB in {:.1f} ms", sp.id, std::filesystem::file_size(path, ec) / 1024, ms);
    return true;
}

void numberObject(PlayLayer* pl, GameObject* obj) {
    int index = static_cast<int>(pl->m_objects->indexOfObject(obj));
    if (index >= 0) obj->m_uniqueID = FIRST_OBJECT_ID + index;
}

void saveNames(std::vector<std::shared_ptr<StartPoint>> const& sps) {
    if (g_level.dir.empty()) return;
    auto names = matjson::Value::object();
    for (auto const& sp : sps) {
        if (!sp->name.empty()) names[fmt::to_string(sp->id)] = sp->name;
    }
    auto res = file::writeStringSafe(namesPath(), names.dump());
    if (res.isErr()) log::error("Can't write {}: {}", namesPath(), res.unwrapErr());
}

void remove(StartPoint const& sp) {
    if (g_level.dir.empty()) return;
    std::error_code ec;
    std::filesystem::remove(spPath(sp.id), ec);
    if (ec) log::error("Can't delete {}: {}", spPath(sp.id), ec.message());
}

void removeAll() {
    if (g_level.dir.empty()) return;
    std::error_code ec;
    std::filesystem::remove_all(g_level.dir, ec);
    log::info("Removed {} ({})", g_level.dir, ec ? ec.message() : "ok");
    g_level = {};
}

}
