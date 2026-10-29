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
#include <map>
#include <set>
#include <unordered_map>

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

// What tells a level object from the others, in one level version and the next: the same object (not
// changed by the update) has the same print. Saved with the SPs (objects.bin), so that SPs of an older
// version can be moved over to the objects they refer to (see readOtherVersion()).
struct ObjectPrint {
    uint64_t hash = 0;
    uint16_t id = 0;  // the object's ID: a stand-in of its kind for one that's gone
    bool operator==(ObjectPrint const&) const = default;
};
constexpr char const* PRINTS_FILE = "objects.bin";

struct Level {
    std::filesystem::path dir;  // empty: this level's SPs aren't saved
    uint64_t stamp = 0;         // level key and version, written into every SP file
    std::string key;
    std::string plainKey;       // the key without the Low Detail Mode suffix, which the stamp is made of
    std::string name;
    matjson::Value info;        // as in level.json
    std::vector<ObjectPrint> prints;  // of m_objects as the level created them (with the anticheat spike)
    bool printsKept = false;    // in objects.bin of `dir`
} g_level;

// The open level's folder name, set before load() can give up on it (its SPs not loaded, nor saved).
std::string g_openKey;

// persistenceAPI::Stream opens files by a std::string path, which MSVC reads in the ANSI code page: a
// save folder with letters outside it (a Cyrillic Windows user name) wouldn't open. This opens by path.
class FileStream : public Stream {
public:
    bool open(std::filesystem::path const& path, bool write, unsigned int* bytesRead = nullptr) {
        delete m_stream;
        m_stream = new std::fstream(path, std::ios::binary | (write ? std::ios::out | std::ios::trunc : std::ios::in));
        m_bytesRead = bytesRead;
        m_PAVersion = PA_VERSION;
        if (!m_stream->good()) return false;
        // PersistenceAPI reads counts and sizes without checking them: in a file that doesn't hold what they
        // say, it would go on past the end. This makes that throw, for readSP to catch.
        if (!write) m_stream->exceptions(std::ios::failbit | std::ios::badbit);
        return true;
    }

    bool good() const {
        return m_stream && m_stream->good();
    }
};

constexpr uint64_t FNV_OFFSET = 1469598103934665603ull;

// FNV-1a; `h` carries a hash on over several pieces.
uint64_t hashBytes(void const* data, size_t size, uint64_t h = FNV_OFFSET) {
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

// Low Detail Mode leaves the level's high detail objects out (8063 of BPATA's 96395), and checkpoints refer
// to objects by their place: SPs only fit the mode they were placed in, so each mode has its own. Levels
// without the mode's option keep one set. With the mode off the folder keeps the plain key (older SPs).
constexpr std::string_view LOW_DETAIL_SUFFIX = "-ldm";

bool lowDetail(PlayLayer* pl) {
    return pl->m_lowDetailMode && pl->m_level->m_lowDetailMode;
}

int highDetailObjects(PlayLayer* pl) {
    int count = 0;
    for (auto obj : CCArrayExt<GameObject*>(pl->m_objects)) count += obj->m_isHighDetail;
    return count;
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

// SPs of both read their checkpoints the same way: the same format, game and platform (the level's
// version and objects may differ).
bool sameGame(matjson::Value const& saved, matjson::Value const& now) {
    if (!saved.isObject()) return false;
    for (auto key : {"format", "game", "platform"}) {
        if (!saved.contains(key) || saved[key] != now[key]) return false;
    }
    return true;
}

// The objects as the level created them, before anything moves them: kind, start position, rotation,
// scale and flip, groups. Not a trigger's settings: one changed where it stands stays the same object, and
// what it was doing at an SP goes on as it was, as the rest of the SP's state does.
std::vector<ObjectPrint> printsOf(PlayLayer* pl) {
    std::vector<ObjectPrint> out;
    out.reserve(pl->m_objects->count());
    for (auto obj : CCArrayExt<GameObject*>(pl->m_objects)) {
        uint64_t h = FNV_OFFSET;
        auto add = [&h](auto const& value) { h = hashBytes(&value, sizeof(value), h); };
        add(obj->m_objectID);
        add(obj->m_startPosition.x);
        add(obj->m_startPosition.y);
        add(obj->m_startRotationX);
        add(obj->m_startRotationY);
        add(obj->m_startScaleX);
        add(obj->m_startScaleY);
        add(obj->m_startFlipX);
        add(obj->m_startFlipY);
        if (obj->m_groups) {
            for (int i = 0; i < obj->m_groupCount && i < static_cast<int>(obj->m_groups->size()); i++) add((*obj->m_groups)[i]);
        }
        out.push_back({h, static_cast<uint16_t>(obj->m_objectID)});
    }
    return out;
}

// objects.bin: magic, count, a hash and an ID per object, and a hash of all that.
constexpr char PRINTS_MAGIC[8] = {'S', 'P', 'C', 'O', 'B', 'J', 'S', '1'};
constexpr size_t PRINT_SIZE = sizeof(uint64_t) + sizeof(uint16_t);
constexpr size_t PRINTS_HEAD = sizeof(PRINTS_MAGIC) + sizeof(uint32_t);

constexpr uintmax_t printsFileSize(size_t count) {
    return PRINTS_HEAD + count * PRINT_SIZE + sizeof(uint64_t);
}

bool writePrints(std::filesystem::path const& path, std::vector<ObjectPrint> const& prints) {
    ByteVector bytes(printsFileSize(prints.size()));
    auto at = bytes.data();
    auto put = [&at](void const* data, size_t size) {
        std::memcpy(at, data, size);
        at += size;
    };
    put(PRINTS_MAGIC, sizeof(PRINTS_MAGIC));
    auto count = static_cast<uint32_t>(prints.size());
    put(&count, sizeof(count));
    for (auto const& p : prints) {
        put(&p.hash, sizeof(p.hash));
        put(&p.id, sizeof(p.id));
    }
    uint64_t seal = hashBytes(bytes.data(), at - bytes.data());
    put(&seal, sizeof(seal));
    auto res = file::writeBinarySafe(path, bytes);
    if (res.isErr()) log::error("Can't write {}: {}", path, res.unwrapErr());
    return res.isOk();
}

// Empty if there's no such file or it isn't whole.
std::vector<ObjectPrint> readPrints(std::filesystem::path const& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return {};
    auto data = file::readBinary(path);
    if (data.isErr()) return {};
    auto const& bytes = data.unwrap();
    if (bytes.size() < printsFileSize(0) || std::memcmp(bytes.data(), PRINTS_MAGIC, sizeof(PRINTS_MAGIC)) != 0) return {};
    uint32_t count = 0;
    std::memcpy(&count, bytes.data() + sizeof(PRINTS_MAGIC), sizeof(count));
    if (bytes.size() != printsFileSize(count)) return {};
    uint64_t seal = 0;
    std::memcpy(&seal, bytes.data() + bytes.size() - sizeof(seal), sizeof(seal));
    if (seal != hashBytes(bytes.data(), bytes.size() - sizeof(seal))) return {};
    std::vector<ObjectPrint> out(count);
    auto at = bytes.data() + PRINTS_HEAD;
    for (auto& p : out) {
        std::memcpy(&p.hash, at, sizeof(p.hash));
        std::memcpy(&p.id, at + sizeof(p.hash), sizeof(p.id));
        at += PRINT_SIZE;
    }
    return out;
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

// The open level version's prints next to its SPs, once it has some: an update can move them over then.
void keepPrints() {
    if (g_level.printsKept || g_level.dir.empty() || g_level.prints.empty()) return;
    auto path = g_level.dir / PRINTS_FILE;
    // Written anew if they're another version's (or damaged): a version made of this folder takes them.
    if (readPrints(path) == g_level.prints) {
        g_level.printsKept = true;
        return;
    }
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) log::warn("{} isn't of this level version: written anew", path);
    g_level.printsKept = writePrints(path, g_level.prints);
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

// Body: magic, format, level stamp, the SP's own data, the checkpoint, the presses. save() then seals it.
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
    // After the checkpoint, so that files without it (pressHolds) still read.
    if (sp.pressHolds) return;
    for (auto const& p : sp.pressed) {
        bool jumpBuffered = p.jumpBuffered, ringJump = p.ringJump;
        out << jumpBuffered;
        out << ringJump;
    }
}

enum class ReadError { None, Damaged, Other };

// Where the parts of an SP file before its frames are (writeSP).
constexpr size_t FORMAT_AT = sizeof(MAGIC), STAMP_AT = FORMAT_AT + 4, ID_AT = STAMP_AT + 8, X_AT = ID_AT + 4,
    PERCENT_AT = X_AT + 4;
constexpr size_t MIN_FILE_SIZE = sizeof(MAGIC) + 64 + SEAL_SIZE;
// Far above what an SP of BPATA (96k objects) takes, 250-800 KB.
constexpr size_t MAX_FILE_SIZE = 32 * 1024 * 1024;

// Is the file whole, as save() wrote it? Its size without the seal, 0 if not.
size_t sealedBody(ByteSpan bytes, std::string& error) {
    if (bytes.size() < MIN_FILE_SIZE) {
        error = "too short";
        return 0;
    }
    if (bytes.size() > MAX_FILE_SIZE) {
        error = "too big";
        return 0;
    }
    size_t bodySize = bytes.size() - SEAL_SIZE;
    uint64_t hash = 0;
    unsigned int end = 0;
    std::memcpy(&hash, bytes.data() + bodySize, sizeof(hash));
    std::memcpy(&end, bytes.data() + bodySize + sizeof(hash), sizeof(end));
    if (end != END_MARKER || hash != hashBytes(bytes.data(), bodySize)) {
        error = "damaged (cut off or changed)";
        return 0;
    }
    return bodySize;
}

// The same SP has the same hash whatever its id (the one part an import changes).
uint64_t contentHash(ByteSpan bytes, size_t bodySize) {
    uint32_t noId = 0;
    auto h = hashBytes(bytes.data(), ID_AT);
    h = hashBytes(&noId, sizeof(noId), h);
    return hashBytes(bytes.data() + ID_AT + 4, bodySize - ID_AT - 4, h);
}

std::shared_ptr<StartPoint> readSealedSP(std::filesystem::path const& path, int id, size_t bodySize, uint64_t versionStamp,
    std::string& error, ReadError& kind) {
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
    if (stamp != versionStamp) {
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
    sp->pressHolds = bytesRead == bodySize;
    if (!sp->pressHolds) {
        for (auto& p : sp->pressed) {
            in >> p.jumpBuffered;
            in >> p.ringJump;
        }
    }
    if (bytesRead != bodySize) {
        error = fmt::format("the checkpoint read {} bytes, the file holds {}", bytesRead, bodySize);
        return nullptr;
    }
    in.end();
    kind = ReadError::None;
    return sp;
}

// An SP file of the level version `versionStamp` (levelStamp()) stands for.
std::shared_ptr<StartPoint> readSP(std::filesystem::path const& path, int id, uint64_t versionStamp, std::string& error,
    ReadError& kind) {
    kind = ReadError::Other;
    // PersistenceAPI checks nothing it reads, so the whole file is checked against its seal first:
    // past that it is exactly what save() wrote.
    auto data = file::readBinary(path);
    if (data.isErr()) {
        error = fmt::format("can't read it: {}", data.unwrapErr());
        return nullptr;
    }
    size_t bodySize = sealedBody(data.unwrap(), error);
    if (bodySize == 0) {
        kind = ReadError::Damaged;
        return nullptr;
    }
    try {
        return readSealedSP(path, id, bodySize, versionStamp, error, kind);
    } catch (std::exception const& e) {
        // Whole, but not what an SP file holds (an imported file can be anything).
        kind = ReadError::Damaged;
        error = fmt::format("not readable: {}", e.what());
        return nullptr;
    }
}

std::shared_ptr<StartPoint> readSP(std::filesystem::path const& path, int id, std::string& error, ReadError& kind) {
    return readSP(path, id, g_level.stamp, error, kind);
}

// A stand-in for a level object an update took away, of the same kind (its object ID), for while an SP of
// the older version is read (readOtherVersion()). Outside the level: none may stay in a checkpoint.
// Kept for as long as the game runs: released at exit, after cocos, they could crash it. Null if even a
// block can't be made.
GameObject* standIn(uint16_t id) {
    static std::map<uint16_t, GameObject*> made;
    auto& obj = made[id];
    if (!obj) {
        obj = GameObject::createWithKey(id);
        if (!obj) obj = GameObject::createWithKey(1);
        if (obj) obj->retain();
    }
    return obj;
}

// While an SP is read: the array in the level's m_objects. PersistenceAPI saves a reference to an object
// as its index and turns it back with m_objects->objectAtIndex(index), checking nothing, which the hook
// catches for this array. An index past the array gets a stand-in instead of memory past it; and an array
// made of the open level version's objects in another version's order (readOtherVersion()) gives an SP of
// that version the same objects in this one.
struct Remap {
    CCArray* array = nullptr;
    std::vector<bool> standing;                 // by index: a stand-in is there
    unsigned int objects = 0;                   // the level's objects in it (not the mod's own after them)
    int standIns = 0;                           // references to stand-ins
    int outside = 0;                            // references past the level's objects
#ifdef SPC_SMOKE
    std::vector<unsigned int>* seen = nullptr;  // smoke test: every index asked for
#endif
} g_remap;

CCObject* remapObjectAtIndex(CCArray* self, unsigned int index) {
    // Called from the hook, this goes on to the game's own.
    if (self != g_remap.array) return self->objectAtIndex(index);
#ifdef SPC_SMOKE
    if (g_remap.seen) g_remap.seen->push_back(index);
#endif
    if (index >= g_remap.objects) {
        g_remap.outside++;
        // Something to read into: the SP doesn't get used.
        if (auto obj = standIn(1)) return obj;
        return self->count() > 0 ? self->objectAtIndex(0) : nullptr;
    }
    if (index < g_remap.standing.size() && g_remap.standing[index]) g_remap.standIns++;
    return self->objectAtIndex(index);
}

Hook* g_remapHook = nullptr;

// Off but while it's needed: the game asks arrays for objects all the time.
$execute {
    auto cocos = GetModuleHandleW(L"libcocos2d.dll");
    auto address = cocos ? GetProcAddress(cocos, "?objectAtIndex@CCArray@cocos2d@@QEAAPEAVCCObject@2@I@Z") : nullptr;
    if (!address) return log::error("CCArray::objectAtIndex not found: SPs can't be moved over to a new level version");
    auto hook = Hook::create(reinterpret_cast<void*>(address), &remapObjectAtIndex, "cocos2d::CCArray::objectAtIndex",
        tulip::hook::TulipConvention::Default);
    hook->setAutoEnable(false);
    auto claimed = Mod::get()->claimHook(std::move(hook));
    if (claimed.isErr()) return log::error("Can't hook CCArray::objectAtIndex: {}", claimed.unwrapErr());
    g_remapHook = claimed.unwrap();
}

// `array` in the level's m_objects, the hook on, for as long as it lives. Without the hook SPs are read
// as before: unchecked, and none moved over.
class RemapOn {
    PlayLayer* m_pl;
    CCArray* m_objects;
    bool m_on = false;

public:
    RemapOn(PlayLayer* pl, CCArray* array, std::vector<bool> standing = {}) : m_pl(pl), m_objects(pl->m_objects) {
        if (!g_remapHook) return;
        if (auto res = g_remapHook->enable(); res.isErr()) {
            log::error("Can't turn the CCArray::objectAtIndex hook on: {}", res.unwrapErr());
            return;
        }
        m_on = true;
        g_remap.array = array;
        g_remap.standing = std::move(standing);
        // The level's own: the mod's objects after them (SPs' startposes) are made anew each time.
        g_remap.objects = array == pl->m_objects && !g_level.prints.empty() ? static_cast<unsigned int>(g_level.prints.size())
                                                                             : array->count();
        pl->m_objects = array;
    }
    ~RemapOn() {
        if (!m_on) return;
        m_pl->m_objects = m_objects;
        g_remap = {};
        if (auto res = g_remapHook->disable(); res.isErr()) log::error("Can't turn the CCArray::objectAtIndex hook off: {}", res.unwrapErr());
    }
    explicit operator bool() const {
        return m_on;
    }
};

// What the checkpoint keeps of objects the update took away: where their groups moved them, whether they
// were used, their animation. The objects being gone, so is that (the game would move the stand-ins
// between the level's sections, which they aren't in). The number of entries dropped.
int dropStandIns(CheckpointObject* cp, std::set<GameObject*> const& standIns) {
    int dropped = 0;
    auto drop = [&](auto& states) {
        dropped += static_cast<int>(std::erase_if(states, [&](auto const& s) { return standIns.contains(s.m_gameObject); }));
    };
    drop(cp->m_vectorSavedObjectStateRef);
    drop(cp->m_vectorActiveSaveObjectState);
    drop(cp->m_vectorSpecialSaveObjectState);
    return dropped;
}

// The percent in an SP file as written, NaN if it can't be read: for telling the player which SP it was.
float percentIn(std::filesystem::path const& path) {
    float percent = NAN;
    auto bytes = file::readBinary(path);
    if (bytes.isOk() && bytes.unwrap().size() >= PERCENT_AT + sizeof(percent)) {
        std::memcpy(&percent, bytes.unwrap().data() + PERCENT_AT, sizeof(percent));
    }
    return percent;
}

// Lowest first, unknown ones (NaN) last.
void sortPercents(std::vector<float>& percents) {
    std::sort(percents.begin(), percents.end(), [](float a, float b) { return !std::isnan(a) && (std::isnan(b) || a < b); });
}

struct OtherVersion {
    bool known = false;  // its objects are known, and its checkpoints read like the open version's: SPs can move
    std::vector<std::shared_ptr<StartPoint>> sps;  // moved over: with the open version's objects, ids as there
    std::vector<float> notMoved;                   // percents of the others (NaN: unknown), lowest first
};

// SPs of `dir`, a version of the open level made for another level version, each read with the objects it
// refers to found among the open version's by their prints. What it keeps of objects the update took away
// (or changed) goes; one that needs such an object in any other way (a trigger it was in the middle of,
// say) isn't moved over.
OtherVersion readOtherVersion(PlayLayer* pl, std::filesystem::path const& dir) {
    OtherVersion out;
    auto info = file::readJson(dir / "level.json");
    if (!g_remapHook || g_level.prints.empty() || info.isErr() || !sameGame(info.unwrap(), g_level.info)) return out;
    auto old = readPrints(dir / PRINTS_FILE);
    if (old.empty() || static_cast<int64_t>(old.size()) != info.unwrap()["objects"].asInt().unwrapOr(-1)) return out;

    // The same print, the same object; objects alike (on top of each other) pair up in order.
    std::unordered_map<uint64_t, std::vector<unsigned int>> where;
    for (unsigned int i = 0; i < g_level.prints.size(); i++) where[g_level.prints[i].hash].push_back(i);
    std::unordered_map<uint64_t, size_t> taken;
    Ref<CCArray> array = CCArray::createWithCapacity(static_cast<unsigned int>(old.size()));
    std::vector<bool> standing(old.size());
    std::set<GameObject*> standIns;
    size_t found = 0;
    for (size_t i = 0; i < old.size(); i++) {
        auto it = where.find(old[i].hash);
        auto& next = taken[old[i].hash];
        if (it != where.end() && next < it->second.size() && g_level.prints[it->second[next]].id == old[i].id) {
            array->addObject(pl->m_objects->objectAtIndex(it->second[next++]));
            found++;
            continue;
        }
        auto obj = standIn(old[i].id);
        if (!obj) return out;
        array->addObject(obj);
        standIns.insert(obj);
        standing[i] = true;
    }
    log::info("{}: {} of the {} objects of {} are in this version", g_level.key, found, old.size(), dir.filename());

    std::vector<std::pair<int, std::filesystem::path>> files;
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator(dir, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
        if (int id = idFromName(it->path()); id > 0) files.push_back({id, it->path()});
    }
    std::sort(files.begin(), files.end());
    auto names = file::readJson(dir / "names.json").unwrapOr(matjson::Value::object());
    if (!names.isObject()) names = matjson::Value::object();
    uint64_t stamp = levelStamp(g_level.plainKey, info.unwrap());
    RemapOn on(pl, array, std::move(standing));
    if (!on) return out;
    out.known = true;
    for (auto const& [id, path] : files) {
        g_remap.standIns = g_remap.outside = 0;
        std::string error;
        ReadError kind;
        auto sp = readSP(path, id, stamp, error, kind);
        // Each reference read is one object asked for: all of those to stand-ins have to be among the
        // object states, which go.
        int dropped = sp ? dropStandIns(sp->cp, standIns) : 0;
        if (sp && g_remap.outside == 0 && dropped == g_remap.standIns) {
            auto name = names[fmt::to_string(id)].asString().unwrapOr("");
            if (name.size() <= MAX_NAME_BYTES) sp->name = std::move(name);
            log::info("SP file {} of {} moved over ({} states of objects gone dropped)", path.filename(), dir.filename(), dropped);
            out.sps.push_back(sp);
            continue;
        }
        log::info("SP file {} of {} not moved over: {}", path.filename(), dir.filename(),
            sp ? fmt::format("{} references to objects gone, {} of them object states; {} past the level", g_remap.standIns,
                     dropped, g_remap.outside)
               : error);
        float percent = sp ? sp->percent : percentIn(path);
        out.notMoved.push_back(percent);
    }
    sortPercents(out.notMoved);
    log::info("{}: {} SPs of {} moved over, {} not", g_level.key, out.sps.size(), dir.filename(), files.size() - out.sps.size());
    return out;
}

// Could readOtherVersion() move SPs of `dir` over, as far as can be told without reading them?
bool canMoveOver(std::filesystem::path const& dir) {
    if (!g_remapHook || g_level.prints.empty()) return false;
    auto info = file::readJson(dir / "level.json");
    if (info.isErr() || !sameGame(info.unwrap(), g_level.info)) return false;
    auto count = info.unwrap()["objects"].asInt().unwrapOr(-1);
    std::error_code ec;
    auto size = std::filesystem::file_size(dir / PRINTS_FILE, ec);
    return !ec && count >= 0 && size == printsFileSize(static_cast<size_t>(count));
}

// SPs placed with Low Detail Mode on before each mode had its own folder are in the plain one: they go
// to the low detail folder. Their level.json tells them apart, by the number of objects. False if they
// couldn't be moved: then they'd be seen in the wrong mode, so SPs are off in this level.
bool moveLowDetailSPs(PlayLayer* pl, std::filesystem::path const& root, std::string const& key, matjson::Value const& info) {
    if (!pl->m_level->m_lowDetailMode) return true;
    auto plain = root / key;
    auto low = root / (key + std::string(LOW_DETAIL_SUFFIX));
    std::error_code ec;
    if (std::filesystem::exists(low, ec) || !std::filesystem::exists(plain / "level.json", ec)) return true;
    auto saved = file::readJson(plain / "level.json");
    if (saved.isErr()) return true;
    auto lowInfo = info;
    if (!lowDetail(pl)) {
        int high = highDetailObjects(pl);
        if (high == 0) return true;  // the same objects either way
        lowInfo["objects"] = static_cast<int>(pl->m_objects->count()) - high;
    }
    if (!sameLevel(saved.unwrap(), lowInfo)) return true;
    std::filesystem::rename(plain, low, ec);
    log::info("SPs of {} were placed with Low Detail Mode on: moved to {} ({})", key, low, ec ? ec.message() : "ok");
    return !ec;
}

bool hasSPFiles(std::filesystem::path const& dir) {
    std::error_code ec;
    auto it = std::filesystem::directory_iterator(dir, ec);
    // A folder there that can't be read may hold some.
    if (ec) return std::filesystem::exists(dir, ec);
    for (; !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
        if (idFromName(it->path()) > 0) return true;
    }
    return static_cast<bool>(ec);
}

// A name no folder has yet for SPs set aside: "-old" ones were made for another level version,
// "-replaced" ones gave way to imported SPs.
constexpr std::string_view OLD = "old", REPLACED = "replaced";

std::filesystem::path asideName(std::filesystem::path const& dir, std::string_view why) {
    std::error_code ec;
    for (int n = 0;; n++) {
        auto aside = dir;
        aside += n == 0 ? fmt::format("-{}-{}", why, std::time(nullptr)) : fmt::format("-{}-{}-{}", why, std::time(nullptr), n);
        if (!std::filesystem::exists(aside, ec)) return aside;
    }
}

// In a version's folder: the name the player gave it.
constexpr char const* VERSION_FILE = "version.json";

std::string nameOf(std::filesystem::path const& path) {
    auto u8 = path.filename().u8string();
    return std::string(u8.begin(), u8.end());
}

// The level key of a version folder's name ("online-1-replaced-1700000000-2": "online-1"), "" if it isn't one.
std::string versionKeyOf(std::string const& name) {
    auto digits = [](std::string_view s) { return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; }); };
    for (auto why : {OLD, REPLACED}) {
        auto mark = fmt::format("-{}-", why);
        auto at = name.rfind(mark);
        if (at == std::string::npos || at == 0) continue;
        auto rest = std::string_view(name).substr(at + mark.size());
        auto dash = rest.find('-');
        if (digits(rest.substr(0, dash)) && (dash == std::string_view::npos || digits(rest.substr(dash + 1)))) {
            return name.substr(0, at);
        }
    }
    return "";
}

// The folders asideName() made for the level folder `dir`, with what they hold; `fits` against `info`.
std::vector<storage::Version> versionsOf(std::filesystem::path const& dir, matjson::Value const& info) {
    std::vector<storage::Version> out;
    auto key = nameOf(dir);
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator(dir.parent_path(), ec); !ec && it != std::filesystem::directory_iterator();
         it.increment(ec)) {
        std::error_code ec2;
        if (!it->is_directory(ec2)) continue;
        auto name = nameOf(it->path());
        storage::Version v;
        size_t timeAt = 0;
        for (auto why : {OLD, REPLACED}) {
            auto prefix = fmt::format("{}-{}-", key, why);
            if (name.starts_with(prefix)) {
                v.replaced = why == REPLACED;
                timeAt = prefix.size();
            }
        }
        if (timeAt == 0 || versionKeyOf(name) != key) continue;
        long long time = 0;
        std::from_chars(name.data() + timeAt, name.data() + name.size(), time);
        v.time = static_cast<std::time_t>(time);
        v.dir = it->path();
        for (auto f = std::filesystem::directory_iterator(v.dir, ec2); !ec2 && f != std::filesystem::directory_iterator();
             f.increment(ec2)) {
            std::error_code ec3;
            if (!f->is_regular_file(ec3)) continue;
            auto size = f->file_size(ec3);
            if (!ec3) v.bytes += size;
            if (idFromName(f->path()) > 0) v.sps++;
        }
        auto saved = file::readJson(v.dir / "level.json");
        v.fits = saved.isOk() && sameLevel(saved.unwrap(), info);
        if (auto meta = file::readJson(v.dir / VERSION_FILE); meta.isOk()) {
            auto given = meta.unwrap()["name"].asString().unwrapOr("");
            if (given.size() <= MAX_NAME_BYTES) v.name = std::move(given);
        }
        out.push_back(std::move(v));
    }
    // In the same second, the one with the "-n" asideName() added is newer, a longer n newer still ("-10" after "-9").
    std::sort(out.begin(), out.end(), [](auto const& a, auto const& b) {
        if (a.time != b.time) return a.time > b.time;
        auto lengthA = a.dir.native().size(), lengthB = b.dir.native().size();
        return lengthA != lengthB ? lengthA > lengthB : a.dir > b.dir;
    });
    return out;
}

// Is `dir` a version of the open level (not the level's own folder, nor anything else)?
bool isVersionOfLevel(std::filesystem::path const& dir) {
    if (g_level.dir.empty() || dir.parent_path() != g_level.dir.parent_path()) return false;
    auto name = nameOf(dir);
    for (auto why : {OLD, REPLACED}) {
        if (name.starts_with(fmt::format("{}-{}-", g_level.key, why))) return true;
    }
    return false;
}

}

namespace storage {

#ifdef SPC_SMOKE
void setRoot(std::string_view utf8) {
    g_root = utils::string::utf8ToWide(utf8);
}
#endif

void beforeCreatingObjects() {
    *reinterpret_cast<int*>(geode::base::get() + UNIQUE_ID_COUNTER) = FIRST_OBJECT_ID;
}

Loaded load(PlayLayer* pl) {
    Loaded out;
    g_level = {};
    g_openKey.clear();
    auto plainKey = levelKey(pl->m_level);
    if (plainKey.empty()) return out;
    auto key = lowDetail(pl) ? plainKey + std::string(LOW_DETAIL_SUFFIX) : plainKey;
    g_openKey = key;
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

    auto root = Mod::get()->getSaveDir() / g_root;
    auto dir = root / key;
    auto info = identity(pl);
    if (!moveLowDetailSPs(pl, root, plainKey, info)) {
        out.notice = Notice::CantMoveAside;
        return out;
    }
    auto infoPath = dir / "level.json";
    std::error_code ec;
    std::filesystem::path oldVersion;  // the SPs of another version of the level, set aside in it
    if (std::filesystem::exists(infoPath, ec)) {
        auto saved = file::readJson(infoPath);
        if ((saved.isErr() || !sameLevel(saved.unwrap(), info)) && !hasSPFiles(dir)) {
            // Played before without SPs: nothing to keep, the folder is this version's now.
            auto res = file::writeStringSafe(infoPath, info.dump());
            if (res.isErr()) {
                log::error("Can't write {}: {}", infoPath, res.unwrapErr());
                return out;
            }
            std::filesystem::remove(dir / "names.json", ec);
            std::filesystem::remove(dir / PRINTS_FILE, ec);
            log::info("{} changed since it was last played, it had no SPs", key);
        }
        else if (saved.isErr() || !sameLevel(saved.unwrap(), info)) {
            // Another version of the level (or of the game): keep its SPs aside.
            auto aside = asideName(dir, OLD);
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
            oldVersion = aside;
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
    // From the key without the mode: SPs placed with Low Detail Mode before its folder of their own came
    // carry that stamp. The number of objects tells the modes apart all the same.
    g_level.stamp = levelStamp(plainKey, info);
    g_level.key = key;
    g_level.plainKey = plainKey;
    g_level.name = pl->m_level->m_levelName;
    g_level.info = info;
    g_level.prints = printsOf(pl);
    g_level.printsKept = false;

    if (!oldVersion.empty()) {
        // An update: the SPs that can be are moved over, keeping their ids and names, and load as the rest.
        auto other = readOtherVersion(pl, oldVersion);
        out.movedOver = other.known;
        // Moved over: those that load below (`moved`).
        for (auto const& sp : other.sps) {
            if (!save(*sp)) other.notMoved.push_back(sp->percent);
        }
        if (!other.sps.empty()) saveNames(other.sps);
        sortPercents(other.notMoved);
        out.notMoved = std::move(other.notMoved);
    }

    auto start = std::chrono::steady_clock::now();
    int failed = 0;
    std::vector<std::filesystem::path> files;
    for (auto it = std::filesystem::directory_iterator(dir, ec); !ec && it != std::filesystem::directory_iterator();
         it.increment(ec)) {
        files.push_back(it->path());
    }
    // Checked: an SP referring past the level's objects would read memory past them.
    std::optional<RemapOn> checked;
    checked.emplace(pl, pl->m_objects);
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
        g_remap.outside = 0;
        auto sp = readSP(path, id, error, kind);
        if (sp && g_remap.outside > 0) {
            // Kept where it is: whole, so not damaged, only not for these objects.
            error = fmt::format("its checkpoint refers to {} objects past the level's", g_remap.outside);
            kind = ReadError::Other;
            sp = nullptr;
        }
        if (!sp) {
            failed++;
            // In the folder of an update there are only the SPs moved over: one more not moved.
            if (out.movedOver) out.notMoved.push_back(percentIn(path));
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
    checked.reset();
    std::sort(out.sps.begin(), out.sps.end(), [](auto const& a, auto const& b) { return a->x < b->x; });
    readNames(out.sps);
    if (!out.sps.empty()) keepPrints();
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    log::info("{}: {} SPs loaded in {:.1f} ms, {} failed", key, out.sps.size(), ms, failed);
    out.failed = failed;
    if (out.movedOver) {
        out.moved = static_cast<int>(out.sps.size());
        sortPercents(out.notMoved);
    }
    // After an update, those that didn't load are among the ones not moved over.
    if (failed > 0 && !out.movedOver) out.notice = Notice::Failed;
    else if (!out.sps.empty() && out.notice == Notice::None) out.notice = Notice::Loaded;
    else if (out.sps.empty() && out.notice == Notice::None && pl->m_level->m_lowDetailMode) {
        // The player's SPs may be in the other mode's folder.
        bool low = lowDetail(pl);
        if (hasSPFiles(root / (low ? plainKey : plainKey + std::string(LOW_DETAIL_SUFFIX)))) {
            out.notice = low ? Notice::SPsWithoutLowDetail : Notice::SPsWithLowDetail;
        }
    }
    return out;
}

namespace {

// The SP's file at `path`, whole or not at all.
bool writeSPFile(StartPoint const& sp, std::filesystem::path const& path) {
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
    return true;
}

// The SP's file as save() would write it, for the open level version.
Result<ByteVector> fileFor(StartPoint const& sp) {
    // .tmp: load() deletes it if a crash leaves it.
    auto path = g_level.dir / fmt::format("moving-{}.tmp", sp.id);
    if (!writeSPFile(sp, path)) return Err("can't write it");
    auto bytes = file::readBinary(path);
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return bytes;
}

}

bool save(StartPoint const& sp) {
    if (g_level.dir.empty() || !sp.cp || !sp.cp->m_player1Checkpoint) return false;
    auto start = std::chrono::steady_clock::now();
    auto path = spPath(sp.id);
    if (!writeSPFile(sp, path)) return false;
    std::error_code ec;
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    log::info("SP {} saved: {} KB in {:.1f} ms", sp.id, std::filesystem::file_size(path, ec) / 1024, ms);
    keepPrints();
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

bool remove(StartPoint const& sp) {
    if (g_level.dir.empty()) return false;
    std::error_code ec;
    std::filesystem::remove(spPath(sp.id), ec);
    if (ec) log::error("Can't delete {}: {}", spPath(sp.id), ec.message());
    return !ec;
}

std::optional<LevelInfo> level() {
    if (g_level.dir.empty()) return std::nullopt;
    return LevelInfo{g_level.key, g_level.name, g_level.info};
}

bool fits(matjson::Value const& identity) {
    return !g_level.dir.empty() && sameLevel(identity, g_level.info);
}

Result<ByteVector> fileOf(StartPoint const& sp) {
    if (g_level.dir.empty()) return Err("SPs of this level aren't saved");
    return file::readBinary(spPath(sp.id));
}

Result<Header> readHeader(ByteSpan bytes) {
    std::string error;
    size_t body = sealedBody(bytes, error);
    if (body == 0) return Err(error);
    if (std::memcmp(bytes.data(), MAGIC, sizeof(MAGIC)) != 0) return Err("not an SP file");
    int format = 0;
    uint64_t stamp = 0;
    Header header;
    std::memcpy(&format, bytes.data() + FORMAT_AT, sizeof(format));
    std::memcpy(&stamp, bytes.data() + STAMP_AT, sizeof(stamp));
    std::memcpy(&header.x, bytes.data() + X_AT, sizeof(header.x));
    std::memcpy(&header.percent, bytes.data() + PERCENT_AT, sizeof(header.percent));
    if (format != FORMAT_VERSION) return Err(fmt::format("format {}, not {}", format, FORMAT_VERSION));
    if (stamp != g_level.stamp) return Err("made for another level or version");
    if (!std::isfinite(header.x) || !std::isfinite(header.percent)) return Err("no position");
    header.content = contentHash(bytes, body);
    return Ok(header);
}

uint64_t contentOf(StartPoint const& sp) {
    auto bytes = fileOf(sp);
    if (bytes.isErr()) return 0;
    std::string error;
    size_t body = sealedBody(bytes.unwrap(), error);
    return body == 0 ? 0 : contentHash(bytes.unwrap(), body);
}

std::vector<std::shared_ptr<StartPoint>> add(std::vector<ByteSpan> const& files, int& nextId) {
    std::vector<std::shared_ptr<StartPoint>> added;
    for (auto bytes : files) {
        if (g_level.dir.empty()) {
            added.push_back(nullptr);
            continue;
        }
        int id = nextId++;
        auto path = spPath(id);
        auto temp = path;
        temp += ".tmp";
        std::error_code ec;
        std::string error;
        std::shared_ptr<StartPoint> sp;
        // Read back the way load() reads it, and in place only if that worked.
        auto written = file::writeBinary(temp, bytes);
        if (written.isErr()) {
            error = written.unwrapErr();
        } else {
            ReadError kind;
            std::optional<RemapOn> checked;
            if (auto pl = PlayLayer::get()) checked.emplace(pl, pl->m_objects);
            g_remap.outside = 0;
            sp = readSP(temp, id, error, kind);
            if (sp && g_remap.outside > 0) {
                error = fmt::format("its checkpoint refers to {} objects past the level's", g_remap.outside);
                sp = nullptr;
            }
            if (sp) {
                std::filesystem::rename(temp, path, ec);
                if (ec) {
                    error = ec.message();
                    sp = nullptr;
                }
            }
        }
        if (!sp) {
            log::error("Imported SP not added: {}", error);
            std::filesystem::remove(temp, ec);
        } else {
            log::info("Imported SP added as SP {} at x {:.1f} ({:.2f}%)", id, sp->x, sp->percent);
            keepPrints();
        }
        added.push_back(sp);
    }
    return added;
}

// A version that fits holding just `sps`: the same files, byte for byte, under the same names. A copy
// damaged on disk doesn't match, so the SPs never go in favour of a broken one.
std::optional<Version> sameVersion(std::vector<std::shared_ptr<StartPoint>> const& sps) {
    using Entry = std::pair<ByteVector, std::string>;
    std::vector<Entry> own;
    for (auto const& sp : sps) {
        auto bytes = file::readBinary(spPath(sp->id));
        if (bytes.isErr()) return std::nullopt;
        own.push_back({std::move(bytes).unwrap(), sp->name});
    }
    std::sort(own.begin(), own.end());
    for (auto const& v : versions()) {
        if (!v.fits || v.sps != static_cast<int>(sps.size())) continue;
        auto files = readVersion(v);
        if (files.isErr() || files.unwrap().size() != sps.size()) continue;
        std::vector<Entry> theirs;
        for (auto& f : files.unwrap()) theirs.push_back({std::move(f.file), std::move(f.name)});
        std::sort(theirs.begin(), theirs.end());
        if (theirs == own) return v;
    }
    return std::nullopt;
}

Aside setAside(std::vector<std::shared_ptr<StartPoint>> const& sps) {
    Aside out;
    auto& moved = out.moved;
    if (g_level.dir.empty() || sps.empty()) return out;
    std::error_code ec;
    if (auto same = sameVersion(sps)) {
        // Renamed as if made now (as one before "Replace all", whatever it was): the newest, where the
        // player looks for the SPs that just went. Not renamed, it's a copy of them all the same.
        auto newest = asideName(g_level.dir, REPLACED);
        std::filesystem::rename(same->dir, newest, ec);
        log::info("SPs of {} are in {} already: it's {} now ({})", g_level.key, same->dir, newest, ec ? ec.message() : "ok");
        out.existing = !ec;
        for (auto const& sp : sps) {
            std::filesystem::remove(spPath(sp->id), ec);
            if (ec) {
                log::error("Can't delete SP {}: {}", sp->id, ec.message());
                continue;
            }
            moved.push_back(sp);
        }
        return out;
    }
    auto aside = asideName(g_level.dir, REPLACED);
    std::filesystem::create_directories(aside, ec);
    if (ec) {
        log::error("Can't create {}: {}", aside, ec.message());
        return out;
    }
    // A folder like the level's: renamed back, it would load as it was. Without its level.json it couldn't
    // be brought back, so then nothing moves.
    std::filesystem::copy_file(g_level.dir / "level.json", aside / "level.json", ec);
    if (ec) {
        log::error("Can't copy level.json into {}: {}", aside, ec.message());
        std::filesystem::remove_all(aside, ec);
        return out;
    }
    // Its prints, for an update to move its SPs over: the same file (a link takes no space; a copy if
    // links can't be made). Rewritten later, the level's goes to a file of its own (writeBinarySafe).
    keepPrints();
    if (g_level.printsKept) {
        std::error_code linked;
        std::filesystem::create_hard_link(g_level.dir / PRINTS_FILE, aside / PRINTS_FILE, linked);
        if (linked) std::filesystem::copy_file(g_level.dir / PRINTS_FILE, aside / PRINTS_FILE, linked);
        if (linked) log::warn("Can't put the level's prints into {}: {}", aside, linked.message());
    }
    auto names = matjson::Value::object();
    for (auto const& sp : sps) {
        std::filesystem::rename(spPath(sp->id), aside / spPath(sp->id).filename(), ec);
        if (ec) {
            log::error("Can't move SP {} aside: {}", sp->id, ec.message());
            continue;
        }
        if (!sp->name.empty()) names[fmt::to_string(sp->id)] = sp->name;
        moved.push_back(sp);
    }
    log::info("{} of {} SPs of {} set aside in {}", moved.size(), sps.size(), g_level.key, aside);
    if (moved.empty()) {
        std::filesystem::remove_all(aside, ec);
        return out;
    }
    auto res = file::writeStringSafe(aside / "names.json", names.dump());
    if (res.isErr()) log::error("Can't write names into {}: {}", aside, res.unwrapErr());
    return out;
}

std::vector<Version> versions() {
    if (g_level.dir.empty()) return {};
    auto all = versionsOf(g_level.dir, g_level.info);
    std::erase_if(all, [](Version const& v) { return v.sps == 0; });
    for (auto& v : all) v.movable = !v.fits && canMoveOver(v.dir);
    return all;
}

Result<MovedVersion> moveVersion(Version const& version) {
    auto pl = PlayLayer::get();
    if (!pl || !isVersionOfLevel(version.dir)) return Err("not a version of this level");
    if (version.fits) return Err("made for this level version");
    auto other = readOtherVersion(pl, version.dir);
    if (!other.known) return Err("its objects aren't known");
    MovedVersion out;
    out.notMoved = std::move(other.notMoved);
    for (auto const& sp : other.sps) {
        auto file = fileFor(*sp);
        if (file.isErr()) {
            out.notMoved.push_back(sp->percent);
            continue;
        }
        out.sps.push_back({std::move(file).unwrap(), sp->name});
    }
    sortPercents(out.notMoved);
    return Ok(std::move(out));
}

#ifdef SPC_SMOKE
std::vector<References> referencesOfSPs() {
    std::vector<References> out;
    auto pl = PlayLayer::get();
    if (!pl || g_level.dir.empty()) return out;
    std::vector<std::pair<int, std::filesystem::path>> files;
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator(g_level.dir, ec); !ec && it != std::filesystem::directory_iterator();
         it.increment(ec)) {
        if (int id = idFromName(it->path()); id > 0) files.push_back({id, it->path()});
    }
    std::sort(files.begin(), files.end());
    for (auto const& [id, path] : files) {
        References refs{id};
        std::shared_ptr<StartPoint> sp;
        {
            RemapOn on(pl, pl->m_objects);
            if (!on) return out;
            g_remap.seen = &refs.all;
            std::string error;
            ReadError kind;
            sp = readSP(path, id, error, kind);
            if (!sp) log::warn("Smoke test: SP file {}: {}", path.filename(), error);
        }
        auto index = [](GameObject* obj) { return static_cast<unsigned int>(obj->m_uniqueID - FIRST_OBJECT_ID); };
        if (sp) {
            for (auto const& s : sp->cp->m_vectorSavedObjectStateRef) refs.states.push_back(index(s.m_gameObject));
            for (auto const& s : sp->cp->m_vectorActiveSaveObjectState) refs.states.push_back(index(s.m_gameObject));
            for (auto const& s : sp->cp->m_vectorSpecialSaveObjectState) refs.states.push_back(index(s.m_gameObject));
        }
        for (auto* list : {&refs.all, &refs.states}) {
            std::sort(list->begin(), list->end());
            list->erase(std::unique(list->begin(), list->end()), list->end());
        }
        out.push_back(std::move(refs));
    }
    return out;
}
#endif

Result<std::vector<VersionSP>> readVersion(Version const& version) {
    if (!isVersionOfLevel(version.dir)) return Err("not a version of this level");
    if (!version.fits) return Err("made for another level version");
    auto names = file::readJson(version.dir / "names.json").unwrapOr(matjson::Value::object());
    std::vector<std::pair<int, VersionSP>> found;
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator(version.dir, ec); !ec && it != std::filesystem::directory_iterator();
         it.increment(ec)) {
        int id = idFromName(it->path());
        std::error_code ec2;
        if (id == 0 || it->file_size(ec2) > MAX_FILE_SIZE) continue;
        auto bytes = file::readBinary(it->path());
        if (bytes.isErr()) {
            log::error("Version {}: can't read {}: {}", version.dir, it->path().filename(), bytes.unwrapErr());
            continue;
        }
        auto name = names[fmt::to_string(id)].asString().unwrapOr("");
        if (name.size() > MAX_NAME_BYTES) name.clear();
        found.push_back({id, VersionSP{std::move(bytes).unwrap(), std::move(name)}});
    }
    if (ec) return Err(ec.message());
    std::sort(found.begin(), found.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
    std::vector<VersionSP> out;
    for (auto& [id, sp] : found) out.push_back(std::move(sp));
    return Ok(std::move(out));
}

bool removeVersion(Version const& version) {
    if (!isVersionOfLevel(version.dir)) {
        log::error("Not deleting {}: not a version of {}", version.dir, g_level.key);
        return false;
    }
    std::error_code ec;
    auto removed = std::filesystem::remove_all(version.dir, ec);
    log::info("Version {} deleted: {} files ({})", version.dir, removed, ec ? ec.message() : "ok");
    return !ec;
}

bool renameVersion(Version const& version, std::string const& name) {
    std::error_code ec;
    // Not one gone meanwhile: writing its name would make the folder again.
    if (!isVersionOfLevel(version.dir) || !std::filesystem::is_directory(version.dir, ec) || name.size() > MAX_NAME_BYTES) {
        return false;
    }
    auto path = version.dir / VERSION_FILE;
    if (name.empty()) {
        std::filesystem::remove(path, ec);
        if (ec) log::error("Can't delete {}: {}", path, ec.message());
        return !ec;
    }
    auto meta = matjson::Value::object();
    meta["name"] = name;
    auto res = file::writeStringSafe(path, meta.dump());
    if (res.isErr()) log::error("Can't write {}: {}", path, res.unwrapErr());
    return res.isOk();
}

#ifdef SPC_SMOKE
void removeAll() {
    if (g_level.dir.empty()) return;
    std::error_code ec;
    for (auto const& v : versionsOf(g_level.dir, g_level.info)) std::filesystem::remove_all(v.dir, ec);
    std::filesystem::remove_all(g_level.dir, ec);
    log::info("Removed {} and its versions ({})", g_level.dir, ec ? ec.message() : "ok");
    g_level = {};
}
#endif

namespace {

std::filesystem::path rootDir() {
    return Mod::get()->getSaveDir() / g_root;
}

// Nothing in the folder but what the mod writes on its own: no SP files, nothing else of the player's.
bool onlyModFiles(std::filesystem::path const& dir) {
    std::error_code ec;
    auto it = std::filesystem::directory_iterator(dir, ec);
    if (ec) return false;
    for (; !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
        auto name = nameOf(it->path());
        if (name == "level.json" || name == "names.json" || name == VERSION_FILE || name == PRINTS_FILE ||
            it->path().extension() == ".tmp") {
            continue;
        }
        return false;
    }
    return !ec;
}

// The folder of a level (`key`), or one of its versions, right in the storage root.
bool isStoredFolder(std::filesystem::path const& dir, std::string const& key, bool version) {
    if (dir.parent_path() != rootDir()) return false;
    auto name = nameOf(dir);
    return version ? versionKeyOf(name) == key : name == key && versionKeyOf(name).empty();
}

// The level being played now (g_level stays as it was once it's left).
bool isOpenLevel(std::string const& key, std::filesystem::path const& dir) {
    if (!PlayLayer::get()) return false;
    return key == g_openKey || (!g_level.dir.empty() && dir == g_level.dir);
}

bool removeFolder(std::filesystem::path const& dir) {
    std::error_code ec;
    auto removed = std::filesystem::remove_all(dir, ec);
    log::info("Storage: {} deleted: {} files ({})", dir, removed, ec ? ec.message() : "ok");
    return !ec;
}

// A level's own folder once nothing is kept for it: no SPs in it, no versions (those without SPs go first).
void removeIfNothingKept(std::filesystem::path const& dir) {
    auto versions = versionsOf(dir, matjson::Value::object());
    std::erase_if(versions, [](Version const& v) { return v.sps == 0 && onlyModFiles(v.dir) && removeFolder(v.dir); });
    if (versions.empty() && onlyModFiles(dir)) removeFolder(dir);
}

}

void leave() {
    if (!g_level.dir.empty()) removeIfNothingKept(g_level.dir);
}

std::vector<StoredLevel> storedLevels() {
    auto root = rootDir();
    std::map<std::string, StoredLevel> byKey;
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator(root, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
        std::error_code ec2;
        if (!it->is_directory(ec2)) continue;
        auto name = nameOf(it->path());
        auto key = versionKeyOf(name);
        byKey[key.empty() ? name : key].key = key.empty() ? name : key;
    }
    std::vector<StoredLevel> out;
    for (auto& [key, level] : byKey) {
        level.dir = root / std::filesystem::path(std::u8string(key.begin(), key.end()));
        level.lowDetail = key.ends_with(LOW_DETAIL_SUFFIX);
        level.open = isOpenLevel(key, level.dir);
        auto info = file::readJson(level.dir / "level.json");
        std::error_code ec2;
        // Without SPs in it, it holds what the mod writes on entering the level: nothing to free.
        bool own = std::filesystem::is_directory(level.dir, ec2) && !onlyModFiles(level.dir);
        if (own) {
            for (auto f = std::filesystem::directory_iterator(level.dir, ec2); !ec2 && f != std::filesystem::directory_iterator();
                 f.increment(ec2)) {
                std::error_code ec3;
                if (!f->is_regular_file(ec3)) continue;
                auto size = f->file_size(ec3);
                if (!ec3) level.spBytes += size;
                if (idFromName(f->path()) > 0) level.sps++;
            }
        }
        // Against the level version it was last played in; without that, none is said not to fit.
        level.versions = versionsOf(level.dir, info.isOk() ? info.unwrap() : matjson::Value::object());
        std::erase_if(level.versions, [&](Version& v) {
            if (!info.isOk()) v.fits = true;
            return v.sps == 0 && onlyModFiles(v.dir);
        });
        for (auto const& v : level.versions) level.versionBytes += v.bytes;
        if (info.isOk()) level.name = info.unwrap()["name"].asString().unwrapOr("");
        for (auto const& v : level.versions) {
            if (!level.name.empty()) break;
            if (auto saved = file::readJson(v.dir / "level.json"); saved.isOk()) level.name = saved.unwrap()["name"].asString().unwrapOr("");
        }
        if (level.name.empty()) level.name = key;
        if (own || !level.versions.empty()) out.push_back(std::move(level));
    }
    std::sort(out.begin(), out.end(), [](auto const& a, auto const& b) { return a.bytes() > b.bytes(); });
    return out;
}

bool removeLevelSPs(StoredLevel const& level) {
    if (!isStoredFolder(level.dir, level.key, false) || isOpenLevel(level.key, level.dir)) {
        log::error("Storage: not deleting {}", level.dir);
        return false;
    }
    return removeFolder(level.dir);
}

bool removeStoredVersion(StoredLevel const& level, Version const& version) {
    if (!isStoredFolder(version.dir, level.key, true)) {
        log::error("Storage: not deleting {}: not a version of {}", version.dir, level.key);
        return false;
    }
    if (!removeFolder(version.dir)) return false;
    if (isStoredFolder(level.dir, level.key, false) && !isOpenLevel(level.key, level.dir)) removeIfNothingKept(level.dir);
    return true;
}

}
