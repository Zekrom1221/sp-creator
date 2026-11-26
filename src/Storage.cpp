#include "Storage.hpp"

#include "Convert2081.hpp"
#include "Disk.hpp"

#include <Geode/utils/file.hpp>
#include <Geode/utils/string.hpp>

#include <zlib.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <streambuf>
#include <unordered_map>
#include <unordered_set>

using namespace geode::prelude;

namespace {

constexpr char MAGIC[8] = {'S', 'P', 'C', 'R', 'E', 'A', 'T', 'R'};
// A file with the body compressed after its header (see compress()). Older files hold it as it is (MAGIC).
constexpr char COMPRESSED_MAGIC[8] = {'S', 'P', 'C', 'R', 'E', 'A', 'T', 'Z'};
// 3: the checkpoint as the game's own string (serializeCheckpoint), GD 2.2091 on. 2: through PersistenceAPI (GD
// 2.2081): read only to be written again as 3 (Convert2081.cpp), when their level is entered.
constexpr int FORMAT_VERSION = 3;
constexpr int FORMAT_2081 = 2;
constexpr char const* GAME_2081 = "2.2081";
constexpr unsigned int END_MARKER = 0x21505321;  // "!SP!"
// After the body: its hash and the end marker.
constexpr size_t SEAL_SIZE = sizeof(uint64_t) + sizeof(END_MARKER);
// Sanity limits when reading: an auto part is at most a few seconds of steps.
constexpr unsigned int MAX_FRAMES = 4000;
constexpr unsigned int MAX_INPUTS = 4000;
constexpr int MAX_ID = 1000000;
// Places and speeds past any level (hours of it): numbers like that, or NaN, crash the game where the player is put
// (its sections run out, GJBaseGameLayer::addToSection).
constexpr float MAX_COORD = 1e7f;
// The SP's checkpoint is taken on the step of the auto part's first frame: the player stands where that frame is.
constexpr float MAX_SPAWN_OFF = 30.f;

// The game's checkpoint string refers to a level object by its m_uniqueID, and deserializeCheckpoint finds it again
// by that ID (in GJBaseGameLayer::m_unk3680, ID -> object). That only holds if the level's objects get the same
// unique IDs in every session: 12, 13, ... in m_objects order, as the game gives them. Anything created between
// the game's reset of the counter and the level's objects would shift them, so the counter is set again right
// before the level's objects. It's the counter GameObject::resetMID writes (Geode bindings, GD 2.2091 Windows);
// load() checks the result.
constexpr uintptr_t UNIQUE_ID_COUNTER = 0x8fe470;
constexpr int FIRST_OBJECT_ID = 12;
// The SPs' own startpos objects: unique IDs of their own, the same for as long as an object lives, which no other
// object takes (the game keys a startpos's checkpoint by its ID, see main.cpp, and finds objects by their IDs).
constexpr int FIRST_SP_OBJECT_ID = 0x50000000;
// Stand-ins for objects an update took away (standIn): IDs no checkpoint of the open version has.
constexpr int FIRST_STAND_IN_ID = 0x7a000000;

std::filesystem::path g_root = "levels";
#ifdef SPC_SMOKE
bool g_dump2081 = false;  // storage::dump2081()
#endif

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
storage::Notice g_offBecause = storage::Notice::None;  // what load() gave up on the level with (offMsg())

// While a big level makes its objects, a batch a frame, other mods make objects of their own in between: Globed
// makes two players for every other player in the level. With the game's counter, those would take unique IDs
// from the middle of the level's and shift every level object after them. Between the batches, the counter runs
// from FOREIGN_FIRST_ID instead, far past any level's objects, and the level's goes on where it stopped.
constexpr int FOREIGN_FIRST_ID = 0x40000000;
struct Loading {
    PlayLayer* layer = nullptr;  // only compared
    bool on = false;        // between the level's first batch and its last
    bool foreign = false;   // the counter is the foreign one now
    int levelNext = 0;      // the level's counter while the foreign one runs
    int batches = 0;
    int foreignMade = 0;    // unique IDs other mods' objects took between the batches
};
Loading g_loading;
// Not set back for each level: objects other mods keep from one level to the next keep IDs of their own.
int g_foreignNext = FOREIGN_FIRST_ID;

// The level's own startposes hidden from other mods (storage::hide()): while the blanks are in m_objects, a
// blank object stands in the place of each.
struct Hidden {
    unsigned int index = 0;
    Ref<GameObject> real, blank;
};
struct HiddenObjects {
    Ref<CCArray> objects;  // the level's m_objects
    std::vector<Hidden> hidden;
    std::unordered_set<CCObject*> reals;  // the startposes of `hidden`
    bool blanksIn = false;  // storage::showBlanks()
    int shown = 0;          // RealObjects alive
};
// Never destroyed: released at exit, after cocos, the objects could crash it (as standIn()'s).
HiddenObjects& g_hidden = *new HiddenObjects();

// The blanks or the startposes in their places in m_objects, where the other one is: anything else there
// (m_objects changed under them) stays. One object in the place of another: CCArray's replaceObjectAtIndex
// would move all those after it twice.
void putHidden(bool blanks) {
    auto objects = g_hidden.objects.data();
    if (!objects) return;
    for (auto const& h : g_hidden.hidden) {
        if (h.index >= objects->count()) continue;
        auto& slot = objects->data->arr[h.index];
        CCObject* from = blanks ? h.real.data() : h.blank.data();
        CCObject* to = blanks ? h.blank.data() : h.real.data();
        if (slot != from) continue;
        to->retain();
        slot->release();
        slot = to;
    }
}

// The level's objects in their places in m_objects for as long as it lives, those hidden from other mods
// too: SPs refer to objects by their place.
class RealObjects {
public:
    RealObjects() {
        if (g_hidden.shown++ == 0 && g_hidden.blanksIn) putHidden(false);
    }
    ~RealObjects() {
        if (--g_hidden.shown == 0 && g_hidden.blanksIn) putHidden(true);
    }
    RealObjects(RealObjects const&) = delete;
    RealObjects& operator=(RealObjects const&) = delete;
};

// The hidden startposes back in their places, for good.
void showHidden() {
    putHidden(false);
    g_hidden = HiddenObjects();
}

// An SP file's body, written and read in memory (it's compressed after its header, see compress()): values as they
// are in memory, little-endian, one after the other, as PersistenceAPI's stream had them in format 2 (a bool a byte).
class BodyWriter {
    ByteVector m_bytes;

public:
    void write(void const* data, size_t size) {
        auto at = static_cast<uint8_t const*>(data);
        m_bytes.insert(m_bytes.end(), at, at + size);
    }
    template <class T>
        requires std::is_arithmetic_v<T>
    BodyWriter& operator<<(T value) {
        write(&value, sizeof(value));
        return *this;
    }
    ByteVector& bytes() {
        return m_bytes;
    }
};

// Reads `bytes` (which outlive it); past their end it throws, for readSP to catch.
class BodyReader {
    ByteSpan m_bytes;
    size_t m_at = 0;

public:
    explicit BodyReader(ByteSpan bytes) : m_bytes(bytes) {}
    void read(void* data, size_t size) {
        if (size > m_bytes.size() - m_at) throw std::out_of_range("past the end of the body");
        std::memcpy(data, m_bytes.data() + m_at, size);
        m_at += size;
    }
    template <class T>
        requires(std::is_arithmetic_v<T> && !std::is_same_v<T, bool>)
    BodyReader& operator>>(T& value) {
        read(&value, sizeof(value));
        return *this;
    }
    BodyReader& operator>>(bool& value) {
        uint8_t byte = 0;
        read(&byte, 1);
        value = byte != 0;
        return *this;
    }
    size_t done() const {
        return m_at;
    }
    size_t left() const {
        return m_bytes.size() - m_at;
    }
    ByteSpan bytes() const {
        return m_bytes;
    }
    void skipTo(size_t at) {
        if (at < m_at || at > m_bytes.size()) throw std::out_of_range("past the end of the body");
        m_at = at;
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

// An object as the level created it, before anything moves it: kind, start position, rotation (unless
// `rotation` is false), scale and flip, groups. Not a trigger's settings: one changed where it stands stays the
// same object, and what it was doing at an SP goes on as it was, as the rest of the SP's state does.
uint64_t printOf(GameObject* obj, bool rotation) {
    uint64_t h = FNV_OFFSET;
    auto add = [&h](auto const& value) { h = hashBytes(&value, sizeof(value), h); };
    add(obj->m_objectID);
    add(obj->m_startPosition.x);
    add(obj->m_startPosition.y);
    if (rotation) {
        add(obj->m_startRotationX);
        add(obj->m_startRotationY);
    }
    add(obj->m_startScaleX);
    add(obj->m_startScaleY);
    add(obj->m_startFlipX);
    add(obj->m_startFlipY);
    if (obj->m_groups) {
        for (int i = 0; i < obj->m_groupCount && i < static_cast<int>(obj->m_groups->size()); i++) add((*obj->m_groups)[i]);
    }
    return h;
}

std::vector<ObjectPrint> printsOf(PlayLayer* pl) {
    std::vector<ObjectPrint> out;
    out.reserve(pl->m_objects->count());
    for (auto obj : CCArrayExt<GameObject*>(pl->m_objects)) {
        out.push_back({printOf(obj, true), static_cast<uint16_t>(obj->m_objectID)});
    }
    return out;
}

// Main and online levels by their ID; editor levels by their objects, so an edit leaves the SPs behind like an
// update does. Not by the level's string: the game writes it anew whenever the level is saved, changed or not,
// and with it the objects' rotations, a hundredth of a degree off each time (BPATA's -8.77 then -8.76, -8.75):
// those are left out. So are the high detail objects, which Low Detail Mode leaves out, and the anticheat
// spike: one key either way.
std::string levelKey(PlayLayer* pl) {
    auto level = pl->m_level;
    int id = level->m_levelID.value();
    switch (level->m_levelType) {
        case GJLevelType::Main: return fmt::format("main-{}", id);
        case GJLevelType::Editor: {
            uint64_t h = FNV_OFFSET;
            for (auto obj : CCArrayExt<GameObject*>(pl->m_objects)) {
                if (obj->m_isHighDetail || obj == pl->m_anticheatSpike) continue;
                auto print = printOf(obj, false);
                h = hashBytes(&print, sizeof(print), h);
            }
            return fmt::format("editor-{:016x}", h);
        }
        default: return id > 0 ? fmt::format("online-{}", id) : std::string();
    }
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
    auto res = disk::writeBinarySafe(path, bytes);
    if (res.isErr()) log::error("Can't write {}: {}", path, res.unwrapErr());
    return res.isOk();
}

// Empty if there's no such file or it isn't whole.
std::vector<ObjectPrint> readPrints(std::filesystem::path const& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return {};
    auto data = disk::readBinary(path);
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
    auto res = disk::readJson(namesPath());
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

void writePlayer(BodyWriter& out, PlayerFrame f) {
    out << f.y;
    out << f.yVel;
    out << f.rot;
}

void readPlayer(BodyReader& in, PlayerFrame& f) {
    in >> f.y;
    in >> f.yVel;
    in >> f.rot;
}

bool inWorld(double v) {
    return std::isfinite(v) && std::abs(v) <= MAX_COORD;
}

// Its rotation only finite: it isn't a place, and a ball keeps adding to it.
bool playerInWorld(PlayerFrame const& f) {
    return inWorld(f.y) && inWorld(f.yVel) && std::isfinite(f.rot);
}

// The checkpoint as the game writes it itself (the way it saves platformer levels): its size, then the string.
// What it keeps was checked against the checkpoint field by field and in play (plan, П1): all a restart needs, the
// song by its file name only. References to level objects are their unique IDs.
void writeCheckpoint(BodyWriter& out, CheckpointObject* cp) {
    std::string text = serializeCheckpoint(cp);
    out << static_cast<uint32_t>(text.size());
    out.write(text.data(), text.size());
}

// Read with the open level's objects (deserializeCheckpoint finds them by their unique IDs). An ID it doesn't
// find leaves that reference empty (the state of an object, out): nothing is read past anything.
CheckpointObject* readCheckpoint(BodyReader& in, std::string& error) {
    uint32_t size = 0;
    in >> size;
    if (size == 0 || size > in.left()) {
        error = fmt::format("a checkpoint of {} chars, {} bytes left", size, in.left());
        return nullptr;
    }
    std::string text(size, '\0');
    in.read(text.data(), size);
    auto pl = PlayLayer::get();
    if (!pl) {
        error = "no level to read the checkpoint with";
        return nullptr;
    }
    auto cp = CheckpointObject::create();
    deserializeCheckpoint(cp, text, pl);
    if (!cp->m_player1Checkpoint) {
        error = "no player in the checkpoint";
        return nullptr;
    }
    return cp;
}

// A checkpoint of GD 2.2081, as PersistenceAPI wrote it, read into one of this game: objects by their places in
// that version (`objects`). Running out of bytes throws (a damaged file); anything else that doesn't fit, `error`.
CheckpointObject* readCheckpoint2081(BodyReader& in, convert2081::Objects const& objects, std::string& error) {
    size_t at = in.done();
    CheckpointObject* cp = nullptr;
    try {
        cp = convert2081::readCheckpoint(in.bytes(), at, objects);
    }
    catch (std::out_of_range const&) {
        throw;
    }
    catch (std::exception const& e) {
        error = fmt::format("its checkpoint of GD {} can't be read: {}", GAME_2081, e.what());
        return nullptr;
    }
    in.skipTo(at);
    return cp;
}

// Body: magic, format, level stamp, the SP's own data, the checkpoint, the presses. fileFor() then compresses
// and seals it.
void writeSP(BodyWriter& out, StartPoint const& sp) {
    out.write(MAGIC, sizeof(MAGIC));
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

// Where the parts of an SP file before its frames are (writeSP). This header stays as it is when the rest is
// compressed: readHeader and percentIn read it without unpacking.
constexpr size_t FORMAT_AT = sizeof(MAGIC), STAMP_AT = FORMAT_AT + 4, ID_AT = STAMP_AT + 8, X_AT = ID_AT + 4,
    PERCENT_AT = X_AT + 4, HEADER_SIZE = PERCENT_AT + 4;
// In a compressed file, after the header: the size of the body (header and all), then the rest of it compressed.
constexpr size_t COMPRESSED_AT = HEADER_SIZE + sizeof(uint32_t);
// zlib's: of BPATA's SPs, 1 leaves 34% of the size (11 ms an SP), 6 32% (25-30 ms).
constexpr int COMPRESSION_LEVEL = 1;
// SPs compress to a third of their size, small ones to a seventh: a body this many times its compressed size
// isn't one.
constexpr size_t MAX_RATIO = 32;
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

// The hash and the end marker after `bytes`.
void seal(ByteVector& bytes) {
    uint64_t hash = hashBytes(bytes.data(), bytes.size());
    auto put = [&bytes](void const* data, size_t size) {
        auto at = static_cast<uint8_t const*>(data);
        bytes.insert(bytes.end(), at, at + size);
    };
    put(&hash, sizeof(hash));
    put(&END_MARKER, sizeof(END_MARKER));
}

bool isCompressed(ByteSpan bytes) {
    return bytes.size() >= sizeof(COMPRESSED_MAGIC) &&
        std::memcmp(bytes.data(), COMPRESSED_MAGIC, sizeof(COMPRESSED_MAGIC)) == 0;
}

// The size a compressed file gives its body, if bodyOf unpacks that much, else 0. A few KB compressed can stand
// for GBs: no more than a file may hold, nor far more than SPs compress to.
size_t sizeToUnpack(ByteSpan bytes) {
    static_assert(MIN_FILE_SIZE - SEAL_SIZE > COMPRESSED_AT);
    if (bytes.size() < MIN_FILE_SIZE) return 0;
    size_t data = bytes.size() - SEAL_SIZE - COMPRESSED_AT;
    uint32_t size = 0;
    std::memcpy(&size, bytes.data() + HEADER_SIZE, sizeof(size));
    if (size <= HEADER_SIZE || size > MAX_FILE_SIZE || size - HEADER_SIZE > MAX_RATIO * data) return 0;
    return size;
}

// The body writeSP wrote of a whole file: unpacked into `unpacked` if the file is compressed, else the file's
// own bytes. Empty if the file isn't whole or doesn't unpack.
ByteSpan bodyOf(ByteSpan bytes, ByteVector& unpacked, std::string& error) {
    size_t stored = sealedBody(bytes, error);
    if (stored == 0) return {};
    if (!isCompressed(bytes)) return bytes.first(stored);
    size_t size = sizeToUnpack(bytes);
    if (size == 0) {
        uint32_t given = 0;
        std::memcpy(&given, bytes.data() + HEADER_SIZE, sizeof(given));
        error = fmt::format("{} bytes unpacked from {}", given, stored - COMPRESSED_AT);
        return {};
    }
    unpacked.resize(size);
    std::memcpy(unpacked.data(), MAGIC, sizeof(MAGIC));
    std::memcpy(unpacked.data() + sizeof(MAGIC), bytes.data() + sizeof(MAGIC), HEADER_SIZE - sizeof(MAGIC));
    uLongf out = static_cast<uLongf>(size - HEADER_SIZE);
    uLong in = static_cast<uLong>(stored - COMPRESSED_AT);
    // Z_OK once the data ends where it should, its checksum right; an end before `in` leaves the rest unread.
    int res = uncompress2(unpacked.data() + HEADER_SIZE, &out, bytes.data() + COMPRESSED_AT, &in);
    if (res != Z_OK || out != size - HEADER_SIZE || in != stored - COMPRESSED_AT) {
        error = fmt::format("doesn't unpack (zlib {}: {} of {} bytes, from {} of {})", res, out, size - HEADER_SIZE, in,
            stored - COMPRESSED_AT);
        return {};
    }
    return unpacked;
}

// A body writeSP wrote, as an SP file: its header as it is, the rest compressed, sealed; or, if bodyOf doesn't
// read that back (compressed far more than SPs are, it takes it for a bomb), all of it as it is, sealed. Read
// back either way, to be sure, before what it was made of goes.
std::optional<ByteVector> compress(ByteSpan body, std::string& error) {
    if (body.size() <= HEADER_SIZE || body.size() > MAX_FILE_SIZE - SEAL_SIZE) {
        error = fmt::format("a body of {} bytes", body.size());
        return std::nullopt;
    }
    // Unpacked, it starts with MAGIC whatever it started with.
    if (std::memcmp(body.data(), MAGIC, sizeof(MAGIC)) != 0) {
        error = "not an SP file";
        return std::nullopt;
    }
    auto readsBack = [&](ByteSpan file) {
        ByteVector unpacked;
        auto again = bodyOf(file, unpacked, error);
        if (again.empty()) return false;
        if (std::ranges::equal(again, body)) return true;
        error = "it reads back as something else";
        return false;
    };
    uLong rest = static_cast<uLong>(body.size() - HEADER_SIZE);
    uLongf packed = compressBound(rest);
    ByteVector file(COMPRESSED_AT + packed);
    std::memcpy(file.data(), COMPRESSED_MAGIC, sizeof(COMPRESSED_MAGIC));
    std::memcpy(file.data() + sizeof(MAGIC), body.data() + sizeof(MAGIC), HEADER_SIZE - sizeof(MAGIC));
    auto size = static_cast<uint32_t>(body.size());
    std::memcpy(file.data() + HEADER_SIZE, &size, sizeof(size));
    int res = compress2(file.data() + COMPRESSED_AT, &packed, body.data() + HEADER_SIZE, rest, COMPRESSION_LEVEL);
    if (res == Z_OK) {
        file.resize(COMPRESSED_AT + packed);
        seal(file);
        if (readsBack(file)) return file;
    }
    else error = fmt::format("zlib {}", res);
    log::warn("A body of {} bytes kept as it is: compressed, {}", body.size(), error);
    ByteVector plain(body.begin(), body.end());
    seal(plain);
    if (readsBack(plain)) return plain;
    return std::nullopt;
}

// A whole SP file as it's kept: compressed. One that is as it is; an older one's body compressed.
std::optional<ByteVector> keptFile(ByteSpan file, std::string& error) {
    if (isCompressed(file)) {
        if (sealedBody(file, error) == 0) return std::nullopt;
        return ByteVector(file.begin(), file.end());
    }
    ByteVector unpacked;
    auto body = bodyOf(file, unpacked, error);
    if (body.empty()) return std::nullopt;
    return compress(body, error);
}

// The same SP has the same hash whatever its id (the one part an import changes), compressed or not.
uint64_t contentHash(ByteSpan body) {
    uint32_t noId = 0;
    auto h = hashBytes(body.data(), ID_AT);
    h = hashBytes(&noId, sizeof(noId), h);
    return hashBytes(body.data() + ID_AT + 4, body.size() - ID_AT - 4, h);
}

// `body` as bodyOf gives it. With `from2081`, a file of GD 2.2081 (format 2), its checkpoint's objects by their places
// in its version; it's read just as one of this game but for the checkpoint.
std::shared_ptr<StartPoint> readBody(ByteSpan body, int id, uint64_t versionStamp, std::string& error, ReadError& kind,
    convert2081::Objects const* from2081 = nullptr) {
    size_t bodySize = body.size();
    BodyReader in(body);
    char magic[sizeof(MAGIC)] = {};
    in.read(magic, sizeof(magic));
    int format = 0;
    unsigned long long stamp = 0;
    in >> format;
    in >> stamp;
    int wanted = from2081 ? FORMAT_2081 : FORMAT_VERSION;
    if (std::memcmp(magic, MAGIC, sizeof(MAGIC)) != 0 || format != wanted) {
        error = fmt::format("not an SP file of format {}", wanted);
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
    if (frames == 0 || frames > MAX_FRAMES || !inWorld(sp->x) || !std::isfinite(sp->percent)) {
        error = fmt::format("{} auto part steps, x {}", frames, sp->x);
        kind = ReadError::Damaged;
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
    for (size_t k = 0; k < sp->frames.size(); k++) {
        auto const& f = sp->frames[k];
        if (!inWorld(f.x) || !inWorld(f.camX) || !inWorld(f.camY) || !playerInWorld(f.p1) || !playerInWorld(f.p2)) {
            error = fmt::format("auto part step {}: x {}, player 1 y {} speed {}", k, f.x, f.p1.y, f.p1.yVel);
            kind = ReadError::Damaged;
            return nullptr;
        }
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
    if (in.left() == 0) {
        error = "no checkpoint";
        return nullptr;
    }
    sp->cp = from2081 ? readCheckpoint2081(in, *from2081, error) : readCheckpoint(in, error);
    if (!sp->cp) return nullptr;
    // A checkpoint string that reads but isn't the SP's (made up, or damaged in a way the seal can't see: it seals
    // what it's given) puts the player wherever it says: garbage has it standing still at x 0.
    auto spawn = sp->cp->m_player1Checkpoint->m_position;
    float off = std::abs(spawn.x - sp->frames.front().x);
#ifdef SPC_SMOKE
    log::info("Smoke test: SP {} checkpoint at x {:.3f}, auto part from x {:.3f}: off {:.3f}", sp->id, spawn.x,
        sp->frames.front().x, off);
#endif
    if (!inWorld(spawn.x) || !inWorld(spawn.y) || !(off <= MAX_SPAWN_OFF)) {
        error = fmt::format("its checkpoint has the player at {},{}, its auto part starts at x {}", spawn.x, spawn.y,
            sp->frames.front().x);
        kind = ReadError::Damaged;
        return nullptr;
    }
    sp->pressHolds = in.left() == 0;
    if (!sp->pressHolds) {
        for (auto& p : sp->pressed) {
            in >> p.jumpBuffered;
            in >> p.ringJump;
        }
    }
    if (in.done() != bodySize) {
        error = fmt::format("{} bytes read, the body holds {}", in.done(), bodySize);
        return nullptr;
    }
    kind = ReadError::None;
    return sp;
}

// An SP file of the level version `versionStamp` (levelStamp()) stands for (of GD 2.2081 with `from2081`, see
// readBody).
std::shared_ptr<StartPoint> readSP(std::filesystem::path const& path, int id, uint64_t versionStamp, std::string& error,
    ReadError& kind, convert2081::Objects const* from2081 = nullptr) {
    kind = ReadError::Other;
    // The whole file is checked against its seal first (and a compressed one unpacked): past that it isn't cut off
    // or changed since it was sealed.
    auto data = disk::readBinary(path);
    if (data.isErr()) {
        error = fmt::format("can't read it: {}", data.unwrapErr());
        return nullptr;
    }
    ByteVector unpacked;
    auto body = bodyOf(data.unwrap(), unpacked, error);
    if (body.empty()) {
        kind = ReadError::Damaged;
        return nullptr;
    }
    try {
        auto sp = readBody(body, id, versionStamp, error, kind, from2081);
#ifdef SPC_SMOKE
        if (sp && from2081 && g_dump2081) {
            auto to = disk::saveDir() / "convert-dump" / path.parent_path().filename();
            std::error_code ec;
            std::filesystem::create_directories(to, ec);
            to /= path.filename();
            to += ".json";
            auto res = disk::writeString(to, convert2081::dumpCheckpoint(sp->cp, *from2081).dump(0));
            if (res.isErr()) log::error("Smoke test: can't dump {}: {}", to, res.unwrapErr());
        }
#endif
        return sp;
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

// The SP files of `dir` written before SP files were compressed, compressed: the same body, not read again. Damaged
// ones stay as they are, for load() to set aside.
void compressOlder(std::filesystem::path const& dir) {
    auto start = std::chrono::steady_clock::now();
    int done = 0, failed = 0;
    uintmax_t before = 0, after = 0;
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator(dir, ec); !ec && it != std::filesystem::directory_iterator();
         it.increment(ec)) {
        auto const& path = it->path();
        if (idFromName(path) == 0) continue;
        char magic[sizeof(MAGIC)] = {};
        std::ifstream(path, std::ios::binary).read(magic, sizeof(magic));
        if (std::memcmp(magic, MAGIC, sizeof(MAGIC)) != 0) continue;
        auto data = disk::readBinary(path);
        std::string error = data.isErr() ? data.unwrapErr() : "";
        auto file = data.isOk() ? keptFile(data.unwrap(), error) : std::nullopt;
        // Kept as it is (see compress()).
        if (file && std::ranges::equal(*file, data.unwrap())) continue;
        if (file) {
            if (auto res = disk::writeBinarySafe(path, *file); res.isErr()) error = res.unwrapErr();
            else {
                done++;
                before += data.unwrap().size();
                after += file->size();
                continue;
            }
        }
        failed++;
        log::warn("SP file {} of {} not compressed: {}", path.filename(), dir.filename(), error);
    }
    if (done == 0 && failed == 0) return;
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    log::info("{}: {} older SP files compressed, {} KB to {} KB, in {:.0f} ms; {} not", dir.filename(), done, before / 1024,
        after / 1024, ms, failed);
}

int& idCounter();
std::vector<std::filesystem::path> spFilesOf(std::filesystem::path const& dir);
std::string nameOf(std::filesystem::path const& path);
std::string versionKeyOf(std::string const& name);

// A stand-in for a level object an update took away, of the same kind (its object ID), for while an SP of the older
// version is read (readOtherVersion()). Outside the level, with an ID of its own (FIRST_STAND_IN_ID on): none may stay
// in a checkpoint. Made without an ID from the game's counter. Kept for as long as the game runs: released at exit,
// after cocos, they could crash it. Null if even a block can't be made.
GameObject* standIn(uint16_t id) {
    static std::map<uint16_t, GameObject*> made;
    auto& obj = made[id];
    if (!obj) {
        int next = idCounter();
        obj = GameObject::createWithKey(id);
        if (!obj) obj = GameObject::createWithKey(1);
        idCounter() = next;
        if (obj) {
            obj->retain();
            obj->m_uniqueID = FIRST_STAND_IN_ID + id;
        }
    }
    return obj;
}

bool isStandInId(int64_t id) {
    return id >= FIRST_STAND_IN_ID && id < FIRST_STAND_IN_ID + 0x10000;
}

// How many fields of a checkpoint string (values between , | : ~) are stand-ins' unique IDs: references to them.
int standInIdsIn(std::string_view text) {
    int found = 0;
    size_t start = 0;
    for (size_t i = 0; i <= text.size(); i++) {
        if (i < text.size() && text[i] != ',' && text[i] != '|' && text[i] != ':' && text[i] != '~') continue;
        auto field = text.substr(start, i - start);
        start = i + 1;
        int64_t value = 0;
        auto [end, ec] = std::from_chars(field.data(), field.data() + field.size(), value);
        if (ec == std::errc() && end == field.data() + field.size() && isStandInId(value)) found++;
    }
    return found;
}

// While an SP of another level version is read (readOtherVersion()): the game finds the objects a checkpoint refers
// to by their unique IDs in GJBaseGameLayer::m_unk3680. For that while it holds that version's IDs (its objects'
// places + FIRST_OBJECT_ID) for the open version's same objects, or stand-ins for those the update took away; the
// level's own map comes back after. An ID the map doesn't have leaves that reference empty.
class UidMapOn {
    PlayLayer* m_pl;
    std::unordered_map<int, GameObject*> m_real;

public:
    UidMapOn(PlayLayer* pl, std::unordered_map<int, GameObject*> map) : m_pl(pl), m_real(std::move(pl->m_unk3680)) {
        pl->m_unk3680 = std::move(map);
    }
    ~UidMapOn() {
        m_pl->m_unk3680 = std::move(m_real);
    }
    UidMapOn(UidMapOn const&) = delete;
    UidMapOn& operator=(UidMapOn const&) = delete;
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
    auto bytes = disk::readBinary(path);
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
    bool from2081 = false;  // SPs of GD 2.2081, their checkpoints read into this game's
    std::vector<std::shared_ptr<StartPoint>> sps;  // moved over: with the open version's objects, ids as there
    std::vector<float> notMoved;                   // percents of the others (NaN: unknown), lowest first
};

// SPs of GD 2.2081 (format 2), by their folder's level.json, on this platform: their checkpoints are read into this
// game's (Convert2081.cpp), and the SPs written again as this game's.
bool from2081(matjson::Value const& saved, matjson::Value const& now) {
    return saved.isObject() && saved["format"].asInt().unwrapOr(0) == FORMAT_2081 &&
        saved["game"].asString().unwrapOr("") == GAME_2081 && saved.contains("platform") && saved["platform"] == now["platform"];
}

// The same level version (its version and its objects), whatever game made the SPs.
bool sameVersion(matjson::Value const& saved, matjson::Value const& now) {
    return saved.isObject() && saved.contains("version") && saved.contains("objects") && saved["version"] == now["version"] &&
        saved["objects"] == now["objects"];
}

// A level version's objects by their places in it, as SPs of GD 2.2081 refer to them: the open version's same
// objects, stand-ins for those gone (standIn()), the players.
class OldPlaces : public convert2081::Objects {
    PlayLayer* m_pl;
    std::vector<GameObject*> m_byPlace;
    mutable std::unordered_map<GameObject*, int> m_places;

public:
    OldPlaces(PlayLayer* pl, std::vector<GameObject*> byPlace) : m_pl(pl), m_byPlace(std::move(byPlace)) {}

    GameObject* at(int place) const override {
        if (place >= 0) {
            if (place >= static_cast<int>(m_byPlace.size())) {
                throw std::runtime_error(fmt::format("object {}, the level version has {}", place, m_byPlace.size()));
            }
            return m_byPlace[place];
        }
        // As PersistenceAPI: -2 and -3 the players, anything else below 0 none.
        if (place == -2) return m_pl->m_player1;
        if (place == -3) return m_pl->m_player2;
        return nullptr;
    }

    int placeOf(GameObject* obj) const override {
        if (!obj) return -1;
        if (obj == m_pl->m_player1) return -2;
        if (obj == m_pl->m_player2) return -3;
        if (m_places.empty()) {
            for (int i = static_cast<int>(m_byPlace.size()); i-- > 0;) m_places[m_byPlace[i]] = i;
        }
        auto it = m_places.find(obj);
        return it == m_places.end() ? -404 : it->second;
    }
};

// SPs of GD 2.2081 for this very level version (sameVersion()) with the prints `old` kept with them (none: from before
// prints were kept): do the open version's objects stand at the same places? The same kind of object in each place
// (GD 2.2091 gives some objects another print than 2.2081 did, decoration a fraction of a unit off, but makes them
// in the same order). Then they're found by their places, as PersistenceAPI referred to them.
// The level key SP files of GD 2.2081 in a folder with this level.json were stamped with (levelStamp()): the open
// level's, or the one its own level had in 2.2081 (findOwnLevel2081() keeps it there).
std::string stampKey2081(matjson::Value const& saved) {
    return saved["key2081"].asString().unwrapOr(g_level.plainKey);
}

// The same kind of object in each place of `old` and `now`; `otherPrint` of them printed otherwise.
bool sameKinds(std::vector<ObjectPrint> const& old, std::vector<ObjectPrint> const& now, size_t& otherPrint) {
    otherPrint = 0;
    if (old.size() != now.size()) return false;
    for (size_t i = 0; i < old.size(); i++) {
        if (old[i].id != now[i].id) return false;
        otherPrint += old[i].hash != now[i].hash;
    }
    return true;
}

bool samePlaces2081(std::vector<ObjectPrint> const& old) {
    if (old.empty()) return true;
    size_t otherPrint = 0;
    if (!sameKinds(old, g_level.prints, otherPrint)) return false;
    if (otherPrint) log::info("{}: {} objects printed otherwise than in GD {}, in the same places", g_level.key, otherPrint, GAME_2081);
    return true;
}

// The player's own levels go by their objects (levelKey()), and GD 2.2091 prints some objects otherwise than 2.2081
// did: their SPs of 2.2081 are in a folder of another key. The folder of the level's name, version and objects, each
// place holding the same kind of object (by its prints; without them, the only such folder), takes this key, and its
// versions with it. False if one was found but couldn't be renamed.
bool findOwnLevel2081(std::filesystem::path const& root, std::string const& key, matjson::Value const& info,
    std::vector<ObjectPrint> const& prints) {
    bool ldm = key.ends_with(LOW_DETAIL_SUFFIX);
    std::filesystem::path best;
    size_t bestSame = 0, found = 0;
    bool bestHasPrints = false;
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator(root, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
        auto name = nameOf(it->path());
        if (!name.starts_with("editor-") || name == key || name.ends_with(LOW_DETAIL_SUFFIX) != ldm || !versionKeyOf(name).empty()) continue;
        auto saved = disk::readJson(it->path() / "level.json");
        if (saved.isErr() || !from2081(saved.unwrap(), info) || saved.unwrap()["name"] != info["name"] || !sameVersion(saved.unwrap(), info)) {
            continue;
        }
        auto old = readPrints(it->path() / PRINTS_FILE);
        size_t otherPrint = 0;
        if (!old.empty() && !sameKinds(old, prints, otherPrint)) continue;
        found++;
        size_t same = old.empty() ? 0 : old.size() - otherPrint;
        if (best.empty() || (!old.empty() && (!bestHasPrints || same > bestSame))) {
            best = it->path();
            bestSame = same;
            bestHasPrints = !old.empty();
        }
    }
    // Without prints only if it's the only one.
    if (best.empty() || (!bestHasPrints && found > 1)) return true;
    auto oldKey = nameOf(best);
    // Its files were stamped with its key (without the mode's suffix): kept for reading them.
    auto oldPlainKey = ldm ? oldKey.substr(0, oldKey.size() - LOW_DETAIL_SUFFIX.size()) : oldKey;
    auto keepKey = [&](std::filesystem::path const& dir) {
        auto saved = disk::readJson(dir / "level.json");
        if (saved.isErr()) return;
        auto json = saved.unwrap();
        json["key2081"] = oldPlainKey;
        (void)disk::writeStringSafe(dir / "level.json", json.dump());
    };
    std::vector<std::filesystem::path> versions;
    for (auto it = std::filesystem::directory_iterator(root, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
        if (versionKeyOf(nameOf(it->path())) == oldKey) versions.push_back(it->path());
    }
    keepKey(best);
    std::filesystem::rename(best, root / key, ec);
    log::info("{}: SPs of GD {} of this level found in {} ({} of its objects printed the same): {}", key, GAME_2081, oldKey, bestSame,
        ec ? ec.message() : "renamed");
    if (ec) return false;
    for (auto const& v : versions) {
        auto name = nameOf(v);
        auto to = root / (key + name.substr(oldKey.size()));
        keepKey(v);
        std::error_code ec2;
        std::filesystem::rename(v, to, ec2);
        log::info("{}: its version {} -> {} ({})", key, name, to.filename(), ec2 ? ec2.message() : "ok");
    }
    return true;
}

// The open version's objects at their places.
std::vector<GameObject*> objectsByPlace(PlayLayer* pl) {
    std::vector<GameObject*> out;
    out.reserve(pl->m_objects->count());
    for (auto obj : CCArrayExt<GameObject*>(pl->m_objects)) out.push_back(obj);
    return out;
}

// Folders of SPs of GD 2.2081 are kept as they were, by the storage (<root>-2.2081): out of the player's way (Versions,
// the storage manager), in case they have to be read again.
bool keepOriginal2081(std::filesystem::path const& dir, bool move) {
    auto root = g_root;
    root += "-2.2081";
    root = disk::saveDir() / root;
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    auto to = root / dir.filename();
    for (int n = 1; std::filesystem::exists(to, ec); n++) {
        to = root / dir.filename();
        to += fmt::format("-{}", n);
    }
    if (move) std::filesystem::rename(dir, to, ec);
    else std::filesystem::copy(dir, to, std::filesystem::copy_options::recursive, ec);
    log::info("SPs of GD {} in {} kept as they were: {} ({})", GAME_2081, dir.filename(), to, ec ? ec.message() : "ok");
    return !ec;
}

// SPs of `dir`, a version of the open level made for another level version, each read with the objects it
// refers to found among the open version's by their prints. What it keeps of objects the update took away
// (or changed) goes; one that needs such an object in any other way (a trigger it was in the middle of,
// say) isn't moved over.
OtherVersion readOtherVersion(PlayLayer* pl, std::filesystem::path const& dir) {
    OtherVersion out;
    RealObjects real;
    auto info = disk::readJson(dir / "level.json");
    if (g_level.prints.empty() || info.isErr()) return out;
    out.from2081 = from2081(info.unwrap(), g_level.info);
    if (!out.from2081 && !sameGame(info.unwrap(), g_level.info)) return out;
    auto old = readPrints(dir / PRINTS_FILE);
    // SPs of GD 2.2081 of this very version: its objects at the same places (samePlaces2081), prints kept or not.
    if (out.from2081 && sameVersion(info.unwrap(), g_level.info) && samePlaces2081(old)) old = g_level.prints;
    if (old.empty() || static_cast<int64_t>(old.size()) != info.unwrap()["objects"].asInt().unwrapOr(-1)) return out;

    // The same print, the same object; objects alike (on top of each other) pair up in order.
    std::unordered_map<uint64_t, std::vector<unsigned int>> where;
    for (unsigned int i = 0; i < g_level.prints.size(); i++) where[g_level.prints[i].hash].push_back(i);
    std::unordered_map<uint64_t, size_t> taken;
    // That version's unique IDs: its objects' places + FIRST_OBJECT_ID (load() checks the order). SPs of GD 2.2081
    // refer to them by their places.
    std::unordered_map<int, GameObject*> byOldId;
    std::vector<GameObject*> byPlace(old.size());
    std::set<GameObject*> standIns;
    size_t found = 0;
    for (size_t i = 0; i < old.size(); i++) {
        int oldId = FIRST_OBJECT_ID + static_cast<int>(i);
        auto it = where.find(old[i].hash);
        auto& next = taken[old[i].hash];
        if (it != where.end() && next < it->second.size() && g_level.prints[it->second[next]].id == old[i].id) {
            byOldId[oldId] = byPlace[i] = static_cast<GameObject*>(pl->m_objects->objectAtIndex(it->second[next++]));
            found++;
            continue;
        }
        auto obj = standIn(old[i].id);
        if (!obj) return out;
        byOldId[oldId] = byPlace[i] = obj;
        standIns.insert(obj);
    }
    log::info("{}: {} of the {} objects of {} are in this version", g_level.key, found, old.size(), dir.filename());

    std::vector<std::pair<int, std::filesystem::path>> files;
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator(dir, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
        if (int id = idFromName(it->path()); id > 0) files.push_back({id, it->path()});
    }
    std::sort(files.begin(), files.end());
    auto names = disk::readJson(dir / "names.json").unwrapOr(matjson::Value::object());
    if (!names.isObject()) names = matjson::Value::object();
    uint64_t stamp = levelStamp(out.from2081 ? stampKey2081(info.unwrap()) : g_level.plainKey, info.unwrap());
    // Checkpoints of this game: the game finds their objects by that version's IDs. Of GD 2.2081: by their places.
    std::optional<UidMapOn> on;
    std::optional<OldPlaces> places;
    if (out.from2081) places.emplace(pl, std::move(byPlace));
    else on.emplace(pl, std::move(byOldId));
    out.known = true;
    for (auto const& [id, path] : files) {
        std::string error;
        ReadError kind;
        auto sp = readSP(path, id, stamp, error, kind, places ? &*places : nullptr);
        // The states of objects gone go; any other reference to a stand-in (the game writes references as unique
        // IDs) shows in the checkpoint written again.
        int dropped = sp ? dropStandIns(sp->cp, standIns) : 0;
        int left = sp ? standInIdsIn(serializeCheckpoint(sp->cp)) : 0;
        if (sp && left == 0) {
            auto name = names[fmt::to_string(id)].asString().unwrapOr("");
            if (name.size() <= MAX_NAME_BYTES) sp->name = std::move(name);
            log::info("SP file {} of {} moved over ({} states of objects gone dropped)", path.filename(), dir.filename(), dropped);
            out.sps.push_back(sp);
            continue;
        }
        log::info("SP file {} of {} not moved over: {}", path.filename(), dir.filename(),
            sp ? fmt::format("{} references to objects gone besides their states ({} states dropped)", left, dropped)
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
    if (g_level.prints.empty()) return false;
    auto info = disk::readJson(dir / "level.json");
    if (info.isErr()) return false;
    bool old2081 = from2081(info.unwrap(), g_level.info);
    if (!old2081 && !sameGame(info.unwrap(), g_level.info)) return false;
    auto count = info.unwrap()["objects"].asInt().unwrapOr(-1);
    std::error_code ec;
    auto size = std::filesystem::file_size(dir / PRINTS_FILE, ec);
    if (ec && old2081 && sameVersion(info.unwrap(), g_level.info)) return true;
    return !ec && count >= 0 && size == printsFileSize(static_cast<size_t>(count));
}

// The SP's file as save() writes it, for the open level version: sealed with its hash, which readSP checks
// before it reads anything.
Result<ByteVector> fileFor(StartPoint const& sp) {
    // The game writes references to objects by their unique IDs, not their places: but in case.
    RealObjects real;
    BodyWriter out;
    writeSP(out, sp);
    std::string error;
    auto file = compress(out.bytes(), error);
    if (!file) return Err(error);
    return Ok(std::move(*file));
}

// The SP's file at `path`, whole or not at all.
bool writeSPFile(StartPoint const& sp, std::filesystem::path const& path) {
    auto file = fileFor(sp);
    // Written in full (to path.tmp, which load() deletes if a crash leaves it), then put in place: a crash
    // while saving leaves no damaged SP behind.
    if (file.isOk()) {
        if (auto res = disk::writeBinarySafe(path, file.unwrap()); res.isErr()) file = Err(res.unwrapErr());
    }
    if (file.isErr()) {
        log::error("Can't save SP {} to {}: {}", sp.id, path, file.unwrapErr());
        return false;
    }
    return true;
}

// A version of the open level holding SPs of GD 2.2081 for this very level version: once its folder is kept as it was
// (keepOriginal2081), its SP files are written again as this game's, in place; it's a version of this game's then.
// One that can't be read stays there by another name (sp-<id>.spc.2081), out of the way. Versions for another level
// version stay as they are, for moving over (moveVersion()).
void convertVersion2081(PlayLayer* pl, std::filesystem::path const& dir) {
    auto saved = disk::readJson(dir / "level.json");
    if (saved.isErr() || !from2081(saved.unwrap(), g_level.info)) return;
    if (!sameVersion(saved.unwrap(), g_level.info)) {
        log::info("{}: SPs of GD {} for another level version, left for moving over", dir.filename(), GAME_2081);
        return;
    }
    if (!samePlaces2081(readPrints(dir / PRINTS_FILE))) {
        log::info("{}: SPs of GD {} whose objects aren't this version's, left for moving over", dir.filename(), GAME_2081);
        return;
    }
    if (!keepOriginal2081(dir, false)) return;
    RealObjects real;
    OldPlaces places(pl, objectsByPlace(pl));
    uint64_t stamp = levelStamp(stampKey2081(saved.unwrap()), saved.unwrap());
    auto start = std::chrono::steady_clock::now();
    int done = 0, failed = 0;
    std::error_code ec;
    for (auto const& path : spFilesOf(dir)) {
        int id = idFromName(path);
        std::string error;
        ReadError kind;
        auto sp = readSP(path, id, stamp, error, kind, &places);
        if (sp && writeSPFile(*sp, path)) {
            done++;
            continue;
        }
        failed++;
        auto aside = path;
        aside += ".2081";
        std::filesystem::rename(path, aside, ec);
        log::error("SP file {} of {} not converted: {} (kept as {})", path.filename(), dir.filename(), sp ? "not written" : error,
            aside.filename());
    }
    // A version of this level version now (it fits).
    if (auto res = disk::writeStringSafe(dir / "level.json", g_level.info.dump()); res.isErr()) {
        log::error("Can't write {}: {}", dir / "level.json", res.unwrapErr());
    }
    writePrints(dir / PRINTS_FILE, g_level.prints);
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    log::info("{}: {} SPs of GD {} written as this game's in {:.0f} ms, {} not", dir.filename(), done, GAME_2081, ms, failed);
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
    auto saved = disk::readJson(plain / "level.json");
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

std::vector<std::filesystem::path> spFilesOf(std::filesystem::path const& dir) {
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator(dir, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
        if (idFromName(it->path()) > 0) out.push_back(it->path());
    }
    return out;
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
// "-replaced" ones gave way to imported SPs, "-deleted" ones the player deleted.
constexpr std::string_view OLD = "old", REPLACED = "replaced", DELETED = "deleted";
constexpr std::array<std::pair<std::string_view, storage::AsideFor>, 3> ASIDE_KINDS = {{
    {OLD, storage::AsideFor::OtherLevelVersion},
    {REPLACED, storage::AsideFor::Replace},
    {DELETED, storage::AsideFor::Delete},
}};

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
    for (auto [why, _] : ASIDE_KINDS) {
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
        for (auto [why, kind] : ASIDE_KINDS) {
            auto prefix = fmt::format("{}-{}-", key, why);
            if (name.starts_with(prefix)) {
                v.why = kind;
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
        auto saved = disk::readJson(v.dir / "level.json");
        v.fits = saved.isOk() && sameLevel(saved.unwrap(), info);
        if (auto meta = disk::readJson(v.dir / VERSION_FILE); meta.isOk()) {
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
    for (auto [why, _] : ASIDE_KINDS) {
        if (name.starts_with(fmt::format("{}-{}-", g_level.key, why))) return true;
    }
    return false;
}

int& idCounter() {
    return *reinterpret_cast<int*>(geode::base::get() + UNIQUE_ID_COUNTER);
}

bool loading(PlayLayer* pl) {
    return g_loading.on && g_loading.layer == pl;
}

// The level's counter back, if the foreign one runs.
void levelCounterBack() {
    if (!g_loading.foreign) return;
    // Never back: another mod may have set the counter itself in between.
    if (int now = idCounter(); now > g_foreignNext) {
        g_loading.foreignMade += now - g_foreignNext;
        g_foreignNext = now;
    }
    idCounter() = g_loading.levelNext;
    g_loading.foreign = false;
}

#ifdef SPC_SMOKE
bool g_foreignCounterOn = true;
#else
constexpr bool g_foreignCounterOn = true;
#endif

}

namespace storage {

#ifdef SPC_SMOKE
void setRoot(std::string_view utf8) {
    g_root = utils::string::utf8ToWide(utf8);
}

void foreignCounter(bool on) {
    g_foreignCounterOn = on;
}

void dump2081(bool on) {
    g_dump2081 = on;
}
#endif

void beforeCreatingObjects(PlayLayer* pl) {
    // A level left while it was making its objects, with the foreign counter on.
    if (g_loading.foreign) g_foreignNext = idCounter();
    idCounter() = FIRST_OBJECT_ID;
    g_offBecause = Notice::None;
    g_loading = {};
    g_loading.layer = pl;
    g_loading.on = true;
    g_loading.batches = 1;
}

void beforeBatch(PlayLayer* pl) {
    if (!loading(pl)) return;
    levelCounterBack();
    g_loading.batches++;
}

void afterBatch(PlayLayer* pl) {
    // Not on: the game made its last objects in this batch (and went on to createObjectsFromSetupFinished).
    if (!loading(pl) || g_loading.foreign || !g_foreignCounterOn) return;
    g_loading.levelNext = idCounter();
    idCounter() = g_foreignNext;
    g_loading.foreign = true;
}

void objectsMade(PlayLayer* pl) {
    if (!loading(pl)) return;
    levelCounterBack();
    g_loading.on = false;
    if (g_loading.foreignMade > 0) {
        log::info("Other mods made objects while the level made its own, in {} batches: {} unique IDs of their own "
            "given to them", g_loading.batches, g_loading.foreignMade);
    }
}

text::Msg offMsg() {
    switch (g_offBecause) {
        case Notice::OutOfOrder: return text::Msg::OutOfOrder;
        case Notice::CantMoveAside: return text::Msg::CantMoveAside;
        case Notice::CantMoveLowDetail: return text::Msg::CantMoveLowDetail;
        default: return text::Msg::CantSave;
    }
}

Loaded load(PlayLayer* pl) {
    Loaded out;
    g_level = {};
    g_openKey.clear();
    g_offBecause = Notice::None;
    showHidden();
    auto plainKey = levelKey(pl);
    if (plainKey.empty()) return out;
    auto key = lowDetail(pl) ? plainKey + std::string(LOW_DETAIL_SUFFIX) : plainKey;
    g_openKey = key;
    // The game's anticheat spike (a spike at 0,105) comes after the level's objects with the ID of its place, as they
    // have theirs (2.2091; 2.2081 gave it one past). Not changed here: the game has found objects by their IDs since
    // it made them.
    int bad = firstObjectOutOfOrder(pl);
    if (bad >= 0) {
        log::error("SPs of {} can't be saved: object {} has unique ID {}, expected {}", key, bad,
            static_cast<GameObject*>(pl->m_objects->objectAtIndex(bad))->m_uniqueID, FIRST_OBJECT_ID + bad);
        out.notice = g_offBecause = Notice::OutOfOrder;
        return out;
    }

    auto root = disk::saveDir() / g_root;
    auto dir = root / key;
    auto info = identity(pl);
    std::error_code noFolder;
    if (key.starts_with("editor-") && !std::filesystem::exists(dir / "level.json", noFolder) &&
        !findOwnLevel2081(root, key, info, printsOf(pl))) {
        out.notice = g_offBecause = Notice::CantMoveAside;
        return out;
    }
    if (!moveLowDetailSPs(pl, root, plainKey, info)) {
        out.notice = g_offBecause = Notice::CantMoveLowDetail;
        return out;
    }
    auto infoPath = dir / "level.json";
    std::error_code ec;
    std::filesystem::path oldVersion;  // the SPs of another version of the level, set aside in it
    if (std::filesystem::exists(infoPath, ec)) {
        auto saved = disk::readJson(infoPath);
        if ((saved.isErr() || !sameLevel(saved.unwrap(), info)) && !hasSPFiles(dir)) {
            // Played before without SPs: nothing to keep, the folder is this version's now.
            auto res = disk::writeStringSafe(infoPath, info.dump());
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
                out.notice = g_offBecause = Notice::CantMoveAside;
                return out;
            }
            out.notice = Notice::LevelChanged;
            oldVersion = aside;
        }
    }
    std::filesystem::create_directories(dir, ec);
    if (!std::filesystem::exists(infoPath, ec)) {
        auto res = disk::writeStringSafe(infoPath, info.dump());
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
        out.from2081 = other.from2081;
        // Moved over: those that load below (`moved`).
        for (auto const& sp : other.sps) {
            if (!save(*sp)) other.notMoved.push_back(sp->percent);
        }
        if (!other.sps.empty()) saveNames(other.sps);
        // Of GD 2.2081, those that couldn't be moved over at all don't stay in Versions either.
        if (other.from2081 && !other.known) {
            for (auto const& path : spFilesOf(oldVersion)) other.notMoved.push_back(percentIn(path));
        }
        sortPercents(other.notMoved);
        out.notMoved = std::move(other.notMoved);
        if (other.from2081) keepOriginal2081(oldVersion, true);
    }
    // Versions of the level set aside on GD 2.2081, for this very version: this game's SPs from now on.
    for (auto const& v : versionsOf(dir, info)) convertVersion2081(pl, v.dir);
    // Once: what was saved before SP files were compressed, here and in the level's versions.
    compressOlder(dir);
    for (auto const& v : versionsOf(dir, info)) compressOlder(v.dir);

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

void numberObject(PlayLayer*, GameObject* obj) {
    static int next = FIRST_SP_OBJECT_ID;
    if (!obj || (obj->m_uniqueID >= FIRST_SP_OBJECT_ID && obj->m_uniqueID < FIRST_STAND_IN_ID)) return;
    obj->m_uniqueID = next++;
}

GameObject* createKeepingIds(int key) {
    int next = idCounter();
    auto obj = GameObject::createWithKey(key);
    idCounter() = next;
    return obj;
}

std::vector<float> peekPositions(PlayLayer* pl) {
    std::vector<float> out;
    auto plainKey = levelKey(pl);
    if (plainKey.empty()) return out;
    auto key = lowDetail(pl) ? plainKey + std::string(LOW_DETAIL_SUFFIX) : plainKey;
    auto dir = disk::saveDir() / g_root / key;
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator(dir, ec); !ec && it != std::filesystem::directory_iterator();
         it.increment(ec)) {
        if (!idFromName(it->path())) continue;
        // The header is where it is whether the rest is compressed or not (see writeSP, compress).
        char head[HEADER_SIZE] = {};
        std::ifstream in(it->path(), std::ios::binary);
        if (!in.read(head, sizeof(head))) continue;
        bool plain = std::memcmp(head, MAGIC, sizeof(MAGIC)) == 0;
        if (!plain && std::memcmp(head, COMPRESSED_MAGIC, sizeof(COMPRESSED_MAGIC)) != 0) continue;
        int format = 0;
        float x = NAN;
        std::memcpy(&format, head + FORMAT_AT, sizeof(format));
        std::memcpy(&x, head + X_AT, sizeof(x));
        if (format == FORMAT_VERSION && std::isfinite(x)) out.push_back(x);
    }
    return out;
}

GameObject* blankFor(GameObject* startPos) {
    // Made while the level makes its objects: their unique IDs go on from where they were.
    auto blank = createKeepingIds(1);
    if (!blank) return nullptr;
    // A decoration, which nothing collides with or draws a hitbox for.
    blank->m_objectType = GameObjectType::Decoration;
    // In no section, as the game sees it: going through m_objects it moves objects between their sections.
    blank->m_innerSectionIndex = blank->m_outerSectionIndex = blank->m_middleSectionIndex = -1;
    blank->m_uniqueID = startPos->m_uniqueID;
    blank->setPosition(startPos->getPosition());
    blank->m_startPosition = startPos->m_startPosition;
    return blank;
}

void hide(PlayLayer* pl, std::vector<std::pair<GameObject*, GameObject*>> const& blanks) {
    showHidden();
    if (blanks.empty()) return;
    std::unordered_map<GameObject*, GameObject*> blankOf(blanks.begin(), blanks.end());
    g_hidden.objects = pl->m_objects;
    for (unsigned int i = 0; i < pl->m_objects->count(); i++) {
        auto obj = static_cast<GameObject*>(pl->m_objects->objectAtIndex(i));
        auto it = blankOf.find(obj);
        if (it == blankOf.end()) continue;
        it->second->m_uniqueID = obj->m_uniqueID;
        g_hidden.hidden.push_back({i, obj, it->second});
        g_hidden.reals.insert(obj);
    }
    log::info("{} startposes of the level hidden from other mods", g_hidden.hidden.size());
}

void showBlanks(bool on) {
    if (g_hidden.blanksIn == on) return;
    g_hidden.blanksIn = on;
    if (g_hidden.shown == 0) putHidden(on);
}

bool isHidden(CCObject* obj) {
    return g_hidden.reals.contains(obj);
}

bool blanksShown() {
    return g_hidden.blanksIn && !g_hidden.hidden.empty();
}

void unhide(PlayLayer* pl) {
    if (g_hidden.objects.data() != pl->m_objects) return;
    if (!g_hidden.hidden.empty()) log::info("The level's startposes back in m_objects: it goes");
    showHidden();
}

void saveNames(std::vector<std::shared_ptr<StartPoint>> const& sps) {
    if (g_level.dir.empty()) return;
    auto names = matjson::Value::object();
    for (auto const& sp : sps) {
        if (!sp->name.empty()) names[fmt::to_string(sp->id)] = sp->name;
    }
    auto res = disk::writeStringSafe(namesPath(), names.dump());
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

bool fits2081(matjson::Value const& identity) {
    return !g_level.dir.empty() && PlayLayer::get() && from2081(identity, g_level.info) && sameVersion(identity, g_level.info);
}

std::vector<std::optional<ByteVector>> convertFiles2081(matjson::Value const& identity, std::vector<ByteVector> const& files) {
    std::vector<std::optional<ByteVector>> out(files.size());
    auto pl = PlayLayer::get();
    if (!fits2081(identity)) return out;
    RealObjects real;
    OldPlaces places(pl, objectsByPlace(pl));
    uint64_t stamp = levelStamp(g_level.plainKey, identity);
    for (size_t f = 0; f < files.size(); f++) {
        std::string error;
        ByteVector unpacked;
        auto body = bodyOf(files[f], unpacked, error);
        std::shared_ptr<StartPoint> sp;
        if (!body.empty()) {
            try {
                int id = 0;
                if (body.size() >= ID_AT + sizeof(id)) std::memcpy(&id, body.data() + ID_AT, sizeof(id));
                ReadError kind;
                sp = readBody(body, id, stamp, error, kind, &places);
            } catch (std::exception const& e) {
                error = fmt::format("not readable: {}", e.what());
            }
        }
        auto file = sp ? fileFor(*sp) : Result<ByteVector>(Err(error));
        if (file.isErr()) {
            log::warn("Import: SP {} of GD {} not converted: {}", f + 1, GAME_2081, file.unwrapErr());
            continue;
        }
        out[f] = std::move(file).unwrap();
    }
    return out;
}

Result<ByteVector> fileOf(StartPoint const& sp) {
    if (g_level.dir.empty()) return Err("SPs of this level aren't saved");
    return disk::readBinary(spPath(sp.id));
}

Result<Header> readHeader(ByteSpan bytes) {
    std::string error;
    ByteVector unpacked;
    auto body = bodyOf(bytes, unpacked, error);
    if (body.empty()) return Err(error);
    if (std::memcmp(body.data(), MAGIC, sizeof(MAGIC)) != 0) return Err("not an SP file");
    int format = 0;
    uint64_t stamp = 0;
    Header header;
    std::memcpy(&format, body.data() + FORMAT_AT, sizeof(format));
    std::memcpy(&stamp, body.data() + STAMP_AT, sizeof(stamp));
    std::memcpy(&header.x, body.data() + X_AT, sizeof(header.x));
    std::memcpy(&header.percent, body.data() + PERCENT_AT, sizeof(header.percent));
    if (format != FORMAT_VERSION) return Err(fmt::format("format {}, not {}", format, FORMAT_VERSION));
    if (stamp != g_level.stamp) return Err("made for another level or version");
    if (!std::isfinite(header.x) || !std::isfinite(header.percent)) return Err("no position");
    header.content = contentHash(body);
    return Ok(header);
}

size_t unpackedSize(ByteSpan bytes) {
    return isCompressed(bytes) ? sizeToUnpack(bytes) : bytes.size();
}

uint64_t contentOf(StartPoint const& sp) {
    auto bytes = fileOf(sp);
    if (bytes.isErr()) return 0;
    std::string error;
    ByteVector unpacked;
    auto body = bodyOf(bytes.unwrap(), unpacked, error);
    return body.empty() ? 0 : contentHash(body);
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
        auto kept = keptFile(bytes, error);
        auto written = kept ? disk::writeBinary(temp, *kept) : Result<>(Err(error));
        if (written.isErr()) {
            error = written.unwrapErr();
        } else {
            ReadError kind;
            sp = readSP(temp, id, error, kind);
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

// A version that fits holding just `sps`: files with the same bodies (compressed or not), under the same
// names. A copy damaged on disk doesn't match, so the SPs never go in favour of a broken one.
std::optional<Version> sameVersion(std::vector<std::shared_ptr<StartPoint>> const& sps) {
    // A body, id and all.
    using Entry = std::pair<ByteVector, std::string>;
    auto entryOf = [](ByteSpan file, std::string name) -> std::optional<Entry> {
        std::string error;
        ByteVector unpacked;
        auto body = bodyOf(file, unpacked, error);
        if (body.empty()) return std::nullopt;
        if (unpacked.empty()) unpacked.assign(body.begin(), body.end());
        return Entry{std::move(unpacked), std::move(name)};
    };
    std::vector<Entry> own;
    for (auto const& sp : sps) {
        auto bytes = disk::readBinary(spPath(sp->id));
        auto entry = bytes.isOk() ? entryOf(bytes.unwrap(), sp->name) : std::nullopt;
        if (!entry) return std::nullopt;
        own.push_back(std::move(*entry));
    }
    std::sort(own.begin(), own.end());
    for (auto const& v : versions()) {
        if (!v.fits || v.sps != static_cast<int>(sps.size())) continue;
        auto files = readVersion(v);
        if (files.isErr() || files.unwrap().size() != sps.size()) continue;
        std::vector<Entry> theirs;
        for (auto& f : files.unwrap()) {
            auto entry = entryOf(f.file, std::move(f.name));
            if (!entry) break;
            theirs.push_back(std::move(*entry));
        }
        std::sort(theirs.begin(), theirs.end());
        if (theirs == own) return v;
    }
    return std::nullopt;
}

Aside setAside(std::vector<std::shared_ptr<StartPoint>> const& sps, AsideFor why) {
    Aside out;
    auto& moved = out.moved;
    if (g_level.dir.empty() || sps.empty()) return out;
    auto mark = std::find_if(ASIDE_KINDS.begin(), ASIDE_KINDS.end(), [why](auto const& k) { return k.second == why; })->first;
    std::error_code ec;
    if (auto same = sameVersion(sps)) {
        // Renamed as if made now (as one before what sets them aside now, whatever it was): the newest, where
        // the player looks for the SPs that just went. Not renamed, it's a copy of them all the same.
        auto newest = asideName(g_level.dir, mark);
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
    auto aside = asideName(g_level.dir, mark);
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
    auto res = disk::writeStringSafe(aside / "names.json", names.dump());
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
ByteVector variantOf(ByteSpan file, std::string_view how) {
    std::string error;
    ByteVector unpacked;
    auto body = bodyOf(file, unpacked, error);
    if (body.empty() || !isCompressed(file)) {
        log::error("Smoke test: no variant {}: not a compressed SP file ({})", how, error);
        return ByteVector(file.begin(), file.end());
    }
    // Whole and sealed, of this level, but its checkpoint string (the game's own, read by deserializeCheckpoint) made
    // wrong: "cpgarbage" (every character one of 0-9 , | : ~ . -), "cphalf" (cut to half), "cpnines" (every number
    // 999999999), "cpzero" (just "0"); or "nanframes": the auto part's frames with NaN for player 1's y and speed.
    if (how.starts_with("cp") || how == "nanframes") {
        ByteVector b(body.begin(), body.end());
        auto u32 = [&b](size_t at) {
            uint32_t v = 0;
            if (at + 4 <= b.size()) std::memcpy(&v, b.data() + at, 4);
            return v;
        };
        constexpr size_t FRAME_BYTES = 4 + 2 * (4 + 8 + 4) + 1 + 4 + 4;
        constexpr size_t INPUT_BYTES = 4 + 4 + 1 + 1;
        size_t at = HEADER_SIZE + 4 + 6;
        uint32_t frames = u32(at);
        size_t framesAt = at + 4;
        at = framesAt + frames * FRAME_BYTES;
        uint32_t inputs = u32(at);
        at += 4 + inputs * INPUT_BYTES;
        uint32_t size = u32(at);
        size_t textAt = at + 4;
        if (textAt + size > b.size()) {
            log::error("Smoke test: no variant {}: the checkpoint isn't where it should be", how);
            return ByteVector(file.begin(), file.end());
        }
        std::string text(b.begin() + textAt, b.begin() + textAt + size);
        if (how == "cpgarbage") {
            uint32_t x = 7;
            for (auto& c : text) c = "0123456789,|:~.-"[(x = x * 1664525 + 1013904223) >> 28];
        }
        else if (how == "cphalf") text.resize(text.size() / 2);
        else if (how == "cpnines") {
            std::string out;
            for (size_t i = 0; i < text.size();) {
                if (text[i] >= '0' && text[i] <= '9') {
                    while (i < text.size() && text[i] >= '0' && text[i] <= '9') i++;
                    out += "999999999";
                }
                else out += text[i++];
            }
            text = std::move(out);
        }
        else if (how == "cpzero") text = "0";
        else if (how == "nanframes") {
            float nanF = NAN;
            double nanD = NAN;
            for (uint32_t k = 0; k < frames; k++) {
                std::memcpy(b.data() + framesAt + k * FRAME_BYTES + 4, &nanF, 4);
                std::memcpy(b.data() + framesAt + k * FRAME_BYTES + 8, &nanD, 8);
            }
        }
        else {
            log::error("Smoke test: no variant {}", how);
            return ByteVector(file.begin(), file.end());
        }
        ByteVector changed(b.begin(), b.begin() + at);
        auto newSize = static_cast<uint32_t>(text.size());
        auto sizeBytes = reinterpret_cast<uint8_t const*>(&newSize);
        changed.insert(changed.end(), sizeBytes, sizeBytes + 4);
        changed.insert(changed.end(), text.begin(), text.end());
        changed.insert(changed.end(), b.begin() + textAt + size, b.end());
        log::info("Smoke test: variant {}: checkpoint of {} chars -> {} (\"{}...\"), {} frames", how, size, text.size(),
            text.substr(0, 40), frames);
        auto kept = compress(changed, error);
        if (!kept) log::error("Smoke test: variant {}: {}", how, error);
        return kept ? std::move(*kept) : ByteVector(file.begin(), file.end());
    }
    if (how == "flat") {
        // Its header, then 4 MB of zeros: compresses far more than SPs do, so it's kept as it is.
        ByteVector flat(body.begin(), body.begin() + HEADER_SIZE);
        flat.resize(HEADER_SIZE + 4 * 1024 * 1024);
        auto kept = compress(flat, error);
        log::info("Smoke test: a flat body: {}", !kept ? error : isCompressed(*kept) ? "compressed" : "kept as it is");
        return kept ? std::move(*kept) : ByteVector(file.begin(), file.end());
    }
    ByteVector out;
    if (how == "older") out.assign(body.begin(), body.end());
    else {
        out.assign(file.begin(), file.end() - SEAL_SIZE);
        uint32_t size = 0;
        std::memcpy(&size, out.data() + HEADER_SIZE, sizeof(size));
        auto give = [&out](uint32_t size) { std::memcpy(out.data() + HEADER_SIZE, &size, sizeof(size)); };
        if (how == "garbled") out[COMPRESSED_AT + (out.size() - COMPRESSED_AT) / 2] ^= 0x5a;
        else if (how == "cut") out.resize(COMPRESSED_AT + (out.size() - COMPRESSED_AT) / 2);
        else if (how == "trailing") out.insert(out.end(), 64, 0x41);
        else if (how == "bigger") give(size + 1);
        else if (how == "smaller") give(size - 1);
        else if (how == "huge") give(0xffffffffu);
        else if (how == "dense") give(static_cast<uint32_t>(MAX_FILE_SIZE));
        else if (how == "noise") {
            // 1 MB that doesn't compress, giving the most a file may hold: unpacked, and found short.
            ByteVector noise(1024 * 1024);
            uint32_t x = 1;
            for (auto& b : noise) b = static_cast<uint8_t>((x = x * 1664525 + 1013904223) >> 24);
            uLongf packed = compressBound(static_cast<uLong>(noise.size()));
            out.resize(COMPRESSED_AT + packed);
            compress2(out.data() + COMPRESSED_AT, &packed, noise.data(), static_cast<uLong>(noise.size()), 1);
            out.resize(COMPRESSED_AT + packed);
            give(static_cast<uint32_t>(MAX_FILE_SIZE));
        }
        else if (how == "bomb") {
            // 64 MB of zeros, some 64 KB compressed, for the size of the body.
            ByteVector zeros(64 * 1024 * 1024);
            uLongf packed = compressBound(static_cast<uLong>(zeros.size()));
            out.resize(COMPRESSED_AT + packed);
            compress2(out.data() + COMPRESSED_AT, &packed, zeros.data(), static_cast<uLong>(zeros.size()), 9);
            out.resize(COMPRESSED_AT + packed);
        }
        else log::error("Smoke test: no variant {}", how);
    }
    seal(out);
    return out;
}

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
        std::string error;
        ReadError kind;
        auto sp = readSP(path, id, error, kind);
        if (!sp) log::warn("Smoke test: SP file {}: {}", path.filename(), error);
        auto index = [](GameObject* obj) { return static_cast<unsigned int>(obj->m_uniqueID - FIRST_OBJECT_ID); };
        if (sp) {
            for (auto const& s : sp->cp->m_vectorSavedObjectStateRef) refs.states.push_back(index(s.m_gameObject));
            for (auto const& s : sp->cp->m_vectorActiveSaveObjectState) refs.states.push_back(index(s.m_gameObject));
            for (auto const& s : sp->cp->m_vectorSpecialSaveObjectState) refs.states.push_back(index(s.m_gameObject));
            // The game finds objects by their IDs, not through m_objects (2.2091): all = the states and the
            // players' own references (the portal last gone through, the object snapped to).
            refs.all = refs.states;
            for (auto p : {sp->cp->m_player1Checkpoint, sp->cp->m_player2Checkpoint}) {
                if (!p) continue;
                for (auto obj : {p->m_lastActivatedPortal, p->m_objectSnappedTo}) {
                    if (obj) refs.all.push_back(index(obj));
                }
            }
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
    auto names = disk::readJson(version.dir / "names.json").unwrapOr(matjson::Value::object());
    std::vector<std::pair<int, VersionSP>> found;
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator(version.dir, ec); !ec && it != std::filesystem::directory_iterator();
         it.increment(ec)) {
        int id = idFromName(it->path());
        std::error_code ec2;
        if (id == 0 || it->file_size(ec2) > MAX_FILE_SIZE) continue;
        auto bytes = disk::readBinary(it->path());
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
    auto res = disk::writeStringSafe(path, meta.dump());
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
    return disk::saveDir() / g_root;
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
    // The level lets go of its objects in m_objects.
    showHidden();
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
        auto info = disk::readJson(level.dir / "level.json");
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
            if (auto saved = disk::readJson(v.dir / "level.json"); saved.isOk()) level.name = saved.unwrap()["name"].asString().unwrapOr("");
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
