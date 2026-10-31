// Smoke test, for development: built in with SPC_SMOKE only (build.bat smoke), as a part of main.cpp,
// whose gameplay code it drives. With <mod save dir>/smoketest.json present at the main menu, the mod
// opens the level, plays it with noclip, places SPs when the player passes the given x, starts from
// each of them, goes back to the level start and closes the game without saving. The file is renamed
// before the test starts, so a crash doesn't repeat it. tools/run_smoke.ps1 runs one.
// The player follows a reference run (a Trajectory Logger CSV: state and inputs per step, matched by x)
// when one is given, and jumps blindly every half second otherwise.

#include <Geode/modify/AppDelegate.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/cocos/support/zip_support/ZipUtils.h>
#include <Geode/utils/file.hpp>

#include "SPList.hpp"
#include "Transfer.hpp"

#include <fstream>
#include <set>

namespace {

bool g_smokeTakeover = false;  // a press ends the auto part whatever the setting says
std::string g_lastNotice;      // the test waits for some

struct RefRow {
    float x = 0, y = 0, rot = 0;
    double yVel = 0;
    std::vector<std::pair<int, bool>> inputs;  // player 1: button, down
};

struct SmokeTest {
    bool active = false;
    std::vector<float> placeAtX;
    size_t placed = 0;
    size_t tested = 0;         // SPs started from so far
    bool backAtStart = false;
    int base = 0;              // blind jumps: step of the first attempt that this attempt's step 0 matches
    int stepsLeft = -1;        // in the current phase
    std::vector<RefRow> reference;
    int refIndex = -1;         // row the player is at, -1 at the start of an attempt
    std::vector<std::pair<float, int>> firstRun;  // x and hits so far, per step of the first attempt
    float phaseStartX = 0;
    bool deathPhase = false;   // an attempt from an SP that ends in a real death (no noclip, no input)
    bool dying = false;
    bool died = false;     // that death came, in the game's destroyPlayer now
    bool cleanup = false;      // delete the saved SPs at the end
    int compat = 0;            // phases with other mods' ways in (see smokeCompat)
    int takeoverAt = -1;       // auto part step to press at, ending the auto part
    bool ui = false;           // look at the mod's UI at the end (pictures in the mod save dir)
    bool uiDone = false;
    bool pausing = false;      // the smoke test opens the pause menu itself
    bool checkHold = false;    // after the takeover press: is the button held a step later
    // A time for other mods' keys: pressed (as from the keyboard) one every EXTERNAL_KEY_STEPS.
    int externalSeconds = 0;
    std::vector<enumKeyCodes> externalKeys;
    size_t externalPressed = 0;
    int externalStep = 0;
    bool externalOn = false;
    bool externalDone = false;
    // Files in the mod save dir: the SPs exported to one once tested (and its variants checked, and the SPs
    // replaced by it), or imported from one at the level start.
    std::string exportTo;
    bool exported = false;
    std::string importFrom;
    bool checkSaved = false;  // at the level start: what an earlier run saved (SPs, versions, names)
    // The storage manager on a copy of a root (the player's SPs), through its buttons; nothing else.
    bool storageUI = false;
    std::string storageRoot;
    // At the level start: out of it, as a player leaves it; what stays of the root's folders.
    bool quit = false;
    // UI: the file dialogs through the buttons instead of renaming and the lists (tools/answer_dialogs.ps1).
    bool dialogs = false;
    // Moving SPs over to a new level version: the level updated in memory before it's entered (the real
    // one given back once the SPs are loaded); what the move-over stands on, logged; the version for the
    // level before the update taken back through the Versions list's way; that list, pictured.
    matjson::Value update;
    bool updated = false;
    gd::string realLevelString;
    int realLevelVersion = 0;
    bool probe = false;
    bool moveVersion = false;
    bool versionsUI = false;
    bool versionsUIDone = false;
    unsigned int waitFrom = 0;
    int waits = 0;
    // The level's Low Detail Mode as it was, given back at the end.
    Ref<GJGameLevel> level;
    std::optional<bool> lowDetailWas;
    // "resave": a level's string and version before the editor first saved it, given back at the end.
    struct Resaved {
        Ref<GJGameLevel> level;
        gd::string string;
        int version = 0;
    };
    std::optional<Resaved> beforeResave;
    // A script ("actions"): steps run one after another, each logged (see nextAction).
    std::vector<std::string> actions;
    size_t actionAt = 0;
    bool scripted = false;
    bool started = false;         // the first step ran
    bool reentering = false;      // out of the level to enter it (or another) again: the test goes on
    enum class Wait { None, Steps, Placing, Playing } wait = Wait::None;
    bool placeQueued = false;     // a placing press waits for the frame: one SP a frame
    bool recordFirst = false;     // the first pass of a "place": hits and player modes by x
    std::vector<std::pair<float, std::string>> firstModes;  // x where the mode became this
    int waitAuto = -1;            // "at": the auto part step to wait for
    int waitAutoSteps = 0;
    std::vector<std::shared_ptr<StartPoint>> playList;  // "play": SPs to start from, in turn
    size_t playAt = 0;
    float handoffX = NAN;         // of the attempt being played, once handed over
    std::string handoffMode;
    std::vector<std::function<void()>> restores;  // settings changed by "setting", put back at the end
    int referenceLevel = 0;       // the level the reference run is of: other levels get blind jumps
    // Blind jumps miss most portals: with "steer_portals" the player is put at a portal's height just before
    // it (outside auto parts), so every mode of the level comes up.
    bool steer = false;
    std::vector<CCPoint> portals;  // by x
    // A watchdog: placing gives up when x stops growing (2.2 levels turn the gameplay, and x stands still),
    // and a step that waits too long ends the test, so the game never stays open on its own.
    float furthestX = 0;
    int stillSteps = 0;
    int idleSteps = 0;
};

constexpr int EXTERNAL_KEY_STEPS = 600;

// A key press and release passed on the way Geode passes on keyboard input (raw input on Windows): its
// event first, which keybinds listen to, then the game's keyboard dispatcher.
void pressKey(enumKeyCodes key) {
    for (bool down : {true, false}) {
        KeyboardInputData data(key, down ? KeyboardInputData::Action::Press : KeyboardInputData::Action::Release,
            {static_cast<uint64_t>(key), 0}, 0.0, KeyboardModifier::None);
        if (KeyboardInputEvent(key).send(data) == ListenerResult::Propagate) {
            CCKeyboardDispatcher::get()->dispatchKeyboardMSG(key, down, false, 0.0);
        }
    }
}

// One half of a press, the same way.
void sendKey(enumKeyCodes key, bool down) {
    KeyboardInputData data(key, down ? KeyboardInputData::Action::Press : KeyboardInputData::Action::Release,
        {static_cast<uint64_t>(key), 0}, 0.0, KeyboardModifier::None);
    if (KeyboardInputEvent(key).send(data) == ListenerResult::Propagate) {
        CCKeyboardDispatcher::get()->dispatchKeyboardMSG(key, down, false, 0.0);
    }
}

SmokeTest g_smoke;

void nextAction();

// What the player is: form, size, gravity, dual, mirror, speed ("ship+mini+upside@0.90").
std::string modeOf(PlayLayer* pl) {
    auto p = pl->m_player1;
    std::string m = p->m_isShip ? "ship" : p->m_isBird ? "ufo" : p->m_isBall ? "ball" : p->m_isDart ? "wave"
        : p->m_isRobot ? "robot" : p->m_isSpider ? "spider" : p->m_isSwing ? "swing" : "cube";
    if (p->m_vehicleSize < 1.f) m += "+mini";
    if (p->m_isUpsideDown) m += "+upside";
    if (pl->m_gameState.m_isDualMode) m += "+dual";
    if (pl->m_cameraFlip < 0.f) m += "+mirror";
    return fmt::format("{}@{:.2f}", m, p->m_playerSpeed);
}

// The first pass's mode at x.
std::string firstModeAt(float x) {
    auto const& modes = g_smoke.firstModes;
    auto it = std::upper_bound(modes.begin(), modes.end(), x, [](float x, auto const& e) { return x < e.first; });
    return it == modes.begin() ? "?" : std::prev(it)->second;
}

// Hits of the first attempt between two x.
int firstRunHits(float from, float to) {
    auto const& run = g_smoke.firstRun;
    auto at = [&](float x) {
        auto it = std::lower_bound(run.begin(), run.end(), x, [](auto const& e, float x) { return e.first < x; });
        return it == run.end() ? (run.empty() ? 0 : run.back().second) : it->second;
    };
    return at(to) - at(from);
}

std::filesystem::path smokeTestPath(std::string_view state) {
    return Mod::get()->getSaveDir() / fmt::format("smoketest{}.json", state);
}

std::vector<RefRow> loadReference(std::filesystem::path const& path) {
    std::vector<RefRow> rows;
    auto res = geode::utils::file::readString(path);
    if (!res) {
        log::error("Smoke test: reference {}: {}", path, res.unwrapErr());
        return rows;
    }
    auto lines = geode::utils::string::split(res.unwrap(), "\n");
    if (lines.empty()) return rows;
    auto header = geode::utils::string::split(lines[0], ",");
    auto column = [&](std::string_view name) {
        auto it = std::find(header.begin(), header.end(), name);
        return it == header.end() ? -1 : static_cast<int>(it - header.begin());
    };
    int cx = column("x"), cy = column("y"), cv = column("yvel"), cr = column("rot"), ci = column("inputs");
    if (cx < 0 || cy < 0 || cv < 0 || cr < 0 || ci < 0) {
        log::error("Smoke test: reference {} lacks a column", path);
        return rows;
    }
    for (size_t i = 1; i < lines.size(); i++) {
        auto cells = geode::utils::string::split(lines[i], ",");
        if (static_cast<int>(cells.size()) <= ci) continue;
        RefRow r;
        r.x = std::strtof(cells[cx].c_str(), nullptr);
        r.y = std::strtof(cells[cy].c_str(), nullptr);
        r.yVel = std::strtod(cells[cv].c_str(), nullptr);
        r.rot = std::strtof(cells[cr].c_str(), nullptr);
        for (auto const& token : geode::utils::string::split(cells[ci], " ")) {
            // D1, U1, D1p2, ...: player 2 is left out.
            if (token.size() != 2 || (token[0] != 'D' && token[0] != 'U')) continue;
            r.inputs.push_back({token[1] - '0', token[0] == 'D'});
        }
        rows.push_back(std::move(r));
    }
    return rows;
}

// Before a step: put the player on the reference row it has reached and press what the run pressed.
void followReference(PlayLayer* pl) {
    auto& t = g_smoke;
    auto const& ref = t.reference;
    auto p = pl->m_player1;
    float x = p->m_position.x + 0.01f;
    if (t.refIndex < 0) {
        // Join the run where the player is, without the inputs before it.
        auto it = std::upper_bound(ref.begin(), ref.end(), x, [](float x, RefRow const& r) { return x < r.x; });
        t.refIndex = std::max(0, static_cast<int>(it - ref.begin()) - 1);
    }
    while (t.refIndex + 1 < static_cast<int>(ref.size()) && ref[t.refIndex + 1].x <= x) {
        t.refIndex++;
        for (auto [button, down] : ref[t.refIndex].inputs) pl->handleButton(down, button, true);
    }
    auto const& r = ref[t.refIndex];
    p->m_position.y = r.y;
    p->setPositionY(r.y);
    p->m_yVelocity = r.yVel;
    p->setRotation(r.rot);
}

// An object of a level string ("1,8,2,105,3,15,...") with its y moved by `dy`.
std::string movedUp(std::string const& object, float dy) {
    auto parts = utils::string::split(object, ",");
    for (size_t i = 0; i + 1 < parts.size(); i += 2) {
        if (parts[i] == "3") parts[i + 1] = fmt::format("{}", utils::numFromString<float>(parts[i + 1]).unwrapOr(0.f) + dy);
    }
    return fmt::format("{}", fmt::join(parts, ","));
}

// The level as its author might update it, in memory only: objects of its string deleted ("delete": indices,
// which are m_objects ones: see smokeProbe), copies of one put in far above the level ("insert": {"at",
// "of", "copies"}, original indices), shifting those after them; the version raised ("bump", on by
// default). The real level comes back once the SPs are loaded (restoreSmokeUpdate).
void smokeUpdate(GJGameLevel* level, matjson::Value const& update) {
    auto raw = std::string(ZipUtils::decompressString(level->m_levelString, false, 0));
    auto parts = utils::string::split(raw, ";");
    if (parts.size() < 2) return log::error("Smoke test: update: no objects in the level string");
    std::vector<std::string> objects(parts.begin() + 1, parts.end());
    std::erase_if(objects, [](auto const& o) { return o.empty(); });
    std::set<size_t> gone;
    for (auto const& v : update["delete"]) gone.insert(static_cast<size_t>(v.asInt().unwrapOr(-1)));
    // Or by what they are: [id, x, y], every object like that.
    for (auto const& like : update["delete_like"]) {
        auto id = fmt::to_string(like[0].asInt().unwrapOr(0));
        auto x = static_cast<float>(like[1].asDouble().unwrapOr(0)), y = static_cast<float>(like[2].asDouble().unwrapOr(0));
        for (size_t i = 0; i < objects.size(); i++) {
            auto fields = utils::string::split(objects[i], ",");
            bool sameId = false, sameX = false, sameY = false;
            for (size_t k = 0; k + 1 < fields.size(); k += 2) {
                if (fields[k] == "1") sameId = fields[k + 1] == id;
                if (fields[k] == "2") sameX = std::abs(utils::numFromString<float>(fields[k + 1]).unwrapOr(NAN) - x) < 0.01f;
                if (fields[k] == "3") sameY = std::abs(utils::numFromString<float>(fields[k + 1]).unwrapOr(NAN) - y) < 0.01f;
            }
            if (sameId && sameX && sameY) gone.insert(i);
        }
    }
    std::vector<std::string> kept;
    for (size_t i = 0; i < objects.size(); i++) {
        if (!gone.contains(i)) kept.push_back(objects[i]);
    }
    if (update.contains("insert")) {
        auto const& insert = update["insert"];
        auto at = static_cast<size_t>(insert["at"].asInt().unwrapOr(0));
        auto of = static_cast<size_t>(insert["of"].asInt().unwrapOr(0));
        auto copies = static_cast<size_t>(insert["copies"].asInt().unwrapOr(1));
        size_t pos = 0;
        for (size_t i = 0; i < at && i < objects.size(); i++) pos += !gone.contains(i);
        if (of < objects.size()) kept.insert(kept.begin() + pos, copies, movedUp(objects[of], 30000.f));
    }
    std::string joined = parts[0] + ";";
    for (auto const& o : kept) joined += o + ";";
    auto& t = g_smoke;
    t.realLevelString = level->m_levelString;
    t.realLevelVersion = level->m_levelVersion;
    t.updated = true;
    level->m_levelString = ZipUtils::compressString(joined, false, 0);
    if (update["bump"].asBool().unwrapOr(true)) level->m_levelVersion += 1;
    log::info("Smoke test: update: {} objects -> {}, version {} -> {}", objects.size(), kept.size(), t.realLevelVersion,
        level->m_levelVersion);
}

void restoreSmokeUpdate() {
    auto& t = g_smoke;
    if (!t.updated || !t.level) return;
    t.level->m_levelString = t.realLevelString;
    t.level->m_levelVersion = t.realLevelVersion;
    t.updated = false;
    log::info("Smoke test: update: the level is as it was");
}

void startSmokeTest() {
    auto path = smokeTestPath("");
    std::error_code ec;
    if (g_smoke.active || !std::filesystem::exists(path, ec)) return;
    auto res = geode::utils::file::readJson(path);
    std::filesystem::rename(path, smokeTestPath(".done"), ec);
    if (!res) {
        log::error("smoketest.json: {}", res.unwrapErr());
        return;
    }
    auto root = std::move(res).unwrap();
    int id = static_cast<int>(root["level"].asInt().unwrapOr(1));
    auto level = id < 100 ? GameLevelManager::get()->getMainLevel(id, false) : GameLevelManager::get()->getSavedLevel(id);
    // Or one of the player's own levels, by name.
    if (auto name = root["editor_level"].asString(); name.isOk()) {
        level = nullptr;
        for (auto local : CCArrayExt<GJGameLevel*>(LocalLevelManager::get()->m_localLevels)) {
            if (std::string(local->m_levelName) == name.unwrap()) {
                level = local;
                break;
            }
        }
    }
    if (!level || std::string(level->m_levelString).empty()) {
        log::error("Smoke test: level {} is not downloaded", id);
        return;
    }
    g_smoke = SmokeTest{};
    g_smoke.active = true;
    for (auto const& v : root["place_at_x"]) g_smoke.placeAtX.push_back(static_cast<float>(v.asDouble().unwrapOr(0)));
    std::sort(g_smoke.placeAtX.begin(), g_smoke.placeAtX.end());
    auto ref = root["reference"].asString();
    if (ref.isOk()) g_smoke.reference = loadReference(ref.unwrap());
    g_smoke.referenceLevel = level->m_levelID.value();
    // Its SPs go to a folder of their own; without SPs to place it tests the ones saved there.
    g_smoke.storageRoot = root["storage_root"].asString().unwrapOr("levels-smoketest");
    storage::setRoot(g_smoke.storageRoot);
    g_smoke.cleanup = root["cleanup"].asBool().unwrapOr(false);
    g_smoke.quit = root["quit"].asBool().unwrapOr(false);
    g_smoke.ui = root["ui"].asBool().unwrapOr(false);
    g_smoke.externalSeconds = static_cast<int>(root["external_seconds"].asInt().unwrapOr(0));
    // Letters and digits: "E,E,Q".
    for (auto const& key : geode::utils::string::split(root["external_keys"].asString().unwrapOr(""), ",")) {
        if (key.size() == 1 && std::isalnum(static_cast<unsigned char>(key[0]))) {
            g_smoke.externalKeys.push_back(static_cast<enumKeyCodes>(std::toupper(static_cast<unsigned char>(key[0]))));
        }
    }
    g_smoke.level = level;
    if (auto ldm = root["low_detail"].asBool(); ldm.isOk()) {
        log::info("Smoke test: Low Detail Mode {} -> {} (the level has the option: {})", level->m_lowDetailModeToggled,
            ldm.unwrap(), level->m_lowDetailMode);
        g_smoke.lowDetailWas = level->m_lowDetailModeToggled;
        level->m_lowDetailModeToggled = ldm.unwrap();
    }
    g_smoke.exportTo = root["export_to"].asString().unwrapOr("");
    g_smoke.importFrom = root["import_from"].asString().unwrapOr("");
    g_smoke.checkSaved = root["check_saved"].asBool().unwrapOr(false);
    if (root["storage_ui"].asBool().unwrapOr(false)) {
        auto dir = Mod::get()->getSaveDir();
        auto name = root["storage_root"].asString().unwrapOr("");
        auto from = root["storage_from"].asString().unwrapOr("levels");
        // Its copy is deleted from: never a root the player's SPs are in.
        if (name.starts_with("levels-smoketest")) {
            std::error_code ec;
            std::filesystem::remove_all(dir / name, ec);
            std::filesystem::copy(dir / from, dir / name, std::filesystem::copy_options::recursive, ec);
            log::info("Smoke test: storage: {} copied to {} ({})", from, name, ec ? ec.message() : "ok");
            g_smoke.storageUI = !ec;
        }
        else log::error("Smoke test: storage_ui needs a storage_root of its own (levels-smoketest...)");
    }
    g_smoke.dialogs = root["dialogs"].asBool().unwrapOr(false);
    g_smoke.probe = root["probe"].asBool().unwrapOr(false);
    g_smoke.moveVersion = root["move_version"].asBool().unwrapOr(false);
    g_smoke.versionsUI = root["versions_ui"].asBool().unwrapOr(false);
    for (auto const& step : root["actions"]) {
        if (auto s = step.asString(); s.isOk()) g_smoke.actions.push_back(s.unwrap());
    }
    g_smoke.scripted = !g_smoke.actions.empty();
    g_smoke.steer = root["steer_portals"].asBool().unwrapOr(false);
    if (root.contains("update")) smokeUpdate(level, root["update"]);
    if (g_smoke.placeAtX.empty() && !g_smoke.scripted) g_smoke.stepsLeft = 60;
    log::info("Smoke test: level {} \"{}\" ({} stars), {} SPs to place, reference of {} rows, cleanup {}, Russian {}, {} steps, "
        "block input {}, auto part {:.2f} s", id, level->m_levelName, level->m_stars.value(), g_smoke.placeAtX.size(),
        g_smoke.reference.size(), g_smoke.cleanup, text::russian(), g_smoke.actions.size(),
        Mod::get()->getSettingValue<bool>("block-input"), autoPartSeconds());
    CCDirector::get()->replaceScene(PlayLayer::scene(level, false, false));
}

constexpr char const* CAPELING_SWITCHER = "capeling.startpos_switcher";

// What another mod's switcher does: capeling's Startpos Switcher through its own keys if it's
// installed, otherwise the same steps by hand.
void foreignSwitch(PlayLayer* pl, int dir) {
    if (Loader::get()->getLoadedMod(CAPELING_SWITCHER)) {
        KeybindSettingPressedEventV3(CAPELING_SWITCHER, dir > 0 ? "rightSwitch" : "leftSwitch").send(Keybind(), true, false, 0.0);
        return;
    }
    StartPosObject* obj = dir > 0 && !g_session.sps.empty() ? g_session.sps.front()->object.data() : nullptr;
    pl->m_isTestMode = obj != nullptr;
    setCurrentCheckpoint(pl, nullptr);
    pl->setStartPosObject(obj);
    pl->resetLevel();
}

void logRestart(PlayLayer* pl, std::string_view what) {
    auto sp = spOf(pl->m_startPosObject);
    log::info("Smoke test: {}: {}, startpos {}, our checkpoint {}, auto part {}, test mode {}, percent {:.3f}{}", what,
        activeLabel(), sp ? fmt::format("of SP {}", sp->id) : (pl->m_startPosObject ? "of the level" : "none"),
        g_session.active && pl->m_currentCheckpoint == g_session.active->cp ? "loaded" : "not loaded",
        g_play.sp ? fmt::format("of SP {}", g_play.sp->id) : "none", pl->m_isTestMode, pl->getCurrentPercent(),
        sp ? fmt::format(" (its object's {:.3f})", startPosPercent(pl, sp->object->getPositionX())) : "");
}

// Startpos switchers' "3/5" labels on screen (MegaHack's, at the bottom), and the SPs' objects: the
// switchers' lists grow by each one made, not by those that came back.
void logSwitchers(std::string_view when) {
    std::vector<std::string> labels;
    std::function<void(CCNode*)> walk = [&](CCNode* node) {
        if (auto label = typeinfo_cast<CCLabelBMFont*>(node)) {
            std::string_view s = label->getString() ? label->getString() : "";
            auto slash = s.find('/');
            auto digits = [](std::string_view part) {
                return !part.empty() && std::all_of(part.begin(), part.end(), [](char c) { return c >= '0' && c <= '9'; });
            };
            if (slash != std::string_view::npos && digits(s.substr(0, slash)) && digits(s.substr(slash + 1))) {
                labels.push_back(fmt::format("{}{}", s, label->isVisible() ? "" : " (hidden)"));
            }
        }
        if (!node->getChildren()) return;
        for (auto child : CCArrayExt<CCNode*>(node->getChildren())) walk(child);
    };
    if (auto scene = CCDirector::get()->getRunningScene()) walk(scene);
    log::info("Smoke test: switchers {}: labels [{}], {} SPs, {} objects made, {} deleted SPs' objects spare", when,
        fmt::join(labels, ", "), g_session.sps.size(), g_session.objectsMade, g_session.retired.size());
}

// Other mods' ways in: how Blitzkrieg sees the SPs, another switcher picking an SP and the level start,
// and a press ending the auto part with control not blocked. Returns false once done.
bool smokeCompat(PlayLayer* pl) {
    auto& t = g_smoke;
    auto& sps = g_session.sps;
    switch (t.compat++) {
        case 0: {
            // Blitzkrieg makes its stages of the StartPosObjects in m_objects.
            std::vector<std::string> found;
            for (auto obj : CCArrayExt<GameObject*>(pl->m_objects)) {
                if (auto spo = typeinfo_cast<StartPosObject*>(obj)) {
                    auto sp = spOf(spo);
                    found.push_back(fmt::format("{:.2f}%{}", spo->getPositionX() / pl->m_levelLength * 100.f,
                        sp ? fmt::format(" (SP {}, {:.2f}%)", sp->id, sp->percent) : ""));
                }
            }
            log::info("Smoke test: startposes in the level: {} [{}], object ID {}, type {}, level length {:.1f}, timestamp {}",
                found.size(), fmt::join(found, ", "), sps.empty() ? 0 : sps.front()->object->m_objectID,
                sps.empty() ? -1 : static_cast<int>(sps.front()->object->m_objectType),
                pl->m_levelLength, pl->m_level->m_timestamp);
            foreignSwitch(pl, 1);
            logRestart(pl, "another switcher, forward");
            t.refIndex = -1;
            t.stepsLeft = (g_play.sp ? g_play.sp->handoff() : 0) + TAIL_STEPS + 100;
            t.phaseStartX = pl->m_player1->m_position.x;
            return true;
        }
        case 1: {
            log::info("Smoke test: that attempt, x {:.1f} to {:.1f}: {} hits, original run {}", t.phaseStartX,
                pl->m_player1->m_position.x, g_rec.hits, firstRunHits(t.phaseStartX, pl->m_player1->m_position.x));
            // An SP placed now goes first at the next restart, unless another switcher picks a startpos before
            // it, even the one it's on now: the way capeling's does, nulling the checkpoint without a release.
            if (sps.size() >= 2 && pl->m_startPosObject == sps.front()->object) {
                CheckpointObject* cp = sps.front()->cp;  // not a Ref: it would add a retain
                unsigned int before = cp->retainCount();
                choose(sps.back());
                pl->m_currentCheckpoint = nullptr;
                pl->setStartPosObject(sps.front()->object);
                pl->m_isTestMode = true;
                pl->resetLevel();
                logRestart(pl, "another switcher picks the startpos it was on, an SP placed before");
                log::info("Smoke test: retains of that SP's checkpoint: {} before, {} after", before, cp->retainCount());
            }
            foreignSwitch(pl, -1);
            logRestart(pl, "another switcher, back");
            t.refIndex = -1;
            t.stepsLeft = 100;
            return true;
        }
        case 2: {
            g_smokeTakeover = true;
            choose(sps.back(), true);
            t.takeoverAt = 50;
            t.base = sps.back()->spawnStep;
            t.refIndex = -1;
            pl->resetLevel();
            logRestart(pl, "takeover");
            t.phaseStartX = pl->m_player1->m_position.x;
            t.stepsLeft = sps.back()->handoff() + TAIL_STEPS + 100;
            return true;
        }
        default:
            log::info("Smoke test: takeover attempt, x {:.1f} to {:.1f}: {} hits, original run {}", t.phaseStartX,
                pl->m_player1->m_position.x, g_rec.hits, firstRunHits(t.phaseStartX, pl->m_player1->m_position.x));
            g_smokeTakeover = false;
            return false;
    }
}

void restoreSmokeLevel() {
    restoreSmokeUpdate();
    auto& t = g_smoke;
    if (t.level && t.lowDetailWas) t.level->m_lowDetailModeToggled = *t.lowDetailWas;
    t.lowDetailWas.reset();
    if (auto const& r = t.beforeResave) {
        r->level->m_levelString = r->string;
        r->level->m_levelVersion = r->version;
        log::info("Smoke test: resave: the level is as it was");
    }
    t.beforeResave.reset();
}

// What made a version, for the log.
char const* asideKind(storage::AsideFor why) {
    switch (why) {
        case storage::AsideFor::Replace: return "replaced";
        case storage::AsideFor::Delete: return "deleted";
        case storage::AsideFor::OtherLevelVersion: break;
    }
    return "old";
}

// Export and import: the SPs go to a file, which is checked against the level, then variants of it that
// must be refused (another level, mode, game or level version; damaged), then the file replaces the
// SPs (forced: they're the same SPs), and the next attempts start from the imported ones.
void smokeTransfer() {
    auto& t = g_smoke;
    auto dir = Mod::get()->getSaveDir();
    auto path = dir / t.exportTo;
    int packed = 0;
    auto exported = transfer::pack(g_session.sps, packed);
    if (exported.isErr()) {
        log::error("Smoke test: export failed: {}", exported.unwrapErr());
        return;
    }
    auto const& bytes = exported.unwrap();
    auto written = file::writeBinarySafe(path, bytes);
    log::info("Smoke test: exported {} of {} SPs to {}: {} KB, written {}", packed, g_session.sps.size(), path,
        bytes.size() / 1024, written.isOk());
    auto check = [](std::string_view what, std::filesystem::path const& file) -> std::optional<transfer::Pack> {
        auto pack = transfer::open(file);
        if (pack.isErr()) {
            log::info("Smoke test: import of {}: refused: {}", what, pack.unwrapErr());
            return std::nullopt;
        }
        auto const& items = pack.unwrap().items;
        auto count = [&](auto pred) { return std::count_if(items.begin(), items.end(), pred); };
        log::info("Smoke test: import of {}: {} SPs, {} already here, {} repeated, {} near one, {} damaged, the level's own {}",
            what, items.size(), count([](auto const& i) { return i.here; }), count([](auto const& i) { return i.repeated; }),
            count([](auto const& i) { return i.nearby > 0; }), pack.unwrap().damaged, pack.unwrap().sameAsLevel);
        return std::move(pack).unwrap();
    };
    auto pack = check("the export", path);
    // The export again, changed on the way.
    auto variant = [&](std::string_view what, geode::Function<void(transfer::Raw&)> change) {
        auto raw = transfer::split(bytes);
        if (raw.isErr()) return log::error("Smoke test: no variant {}: {}", what, raw.unwrapErr());
        change(raw.unwrap());
        auto file = dir / "smoke-variant.spcreator";
        (void)file::writeBinary(file, transfer::join(raw.unwrap()));
        check(what, file);
    };
    variant("another level", [](transfer::Raw& r) { r.manifest["level"]["key"] = "online-1"; });
    variant("Low Detail Mode", [](transfer::Raw& r) {
        r.manifest["level"]["key"] = r.manifest["level"]["key"].asString().unwrapOr("") + "-ldm";
    });
    variant("another game version", [](transfer::Raw& r) { r.manifest["level"]["identity"]["game"] = "2.2074"; });
    variant("another level version", [](transfer::Raw& r) {
        r.manifest["level"]["identity"]["version"] = r.manifest["level"]["identity"]["version"].asInt().unwrapOr(0) + 1;
    });
    variant("a newer format", [](transfer::Raw& r) { r.manifest["format"] = 2; });
    variant("one SP damaged", [](transfer::Raw& r) {
        if (!r.files.empty()) r.files[0][r.files[0].size() / 2] ^= 0x5a;
    });
    variant("an SP more than pack.json lists", [](transfer::Raw& r) {
        if (!r.files.empty()) r.files.push_back(r.files[0]);
    });
    // Whole, but the compressed SP file inside isn't right; and one SP as a file of before compression, the same.
    for (auto how : {"garbled", "cut", "trailing", "bigger", "smaller", "huge", "dense", "bomb", "older", "flat"}) {
        variant(fmt::format("one SP file {}", how), [how](transfer::Raw& r) {
            if (!r.files.empty()) r.files[0] = storage::variantOf(r.files[0], how);
        });
    }
    // More than a pack's SPs may unpack to: those past it aren't unpacked.
    variant("40 SP files unpacking to 32 MB each", [](transfer::Raw& r) {
        if (r.files.empty()) return;
        auto noise = storage::variantOf(r.files[0], "noise");
        auto entry = r.manifest["sps"][0];
        r.files.assign(40, noise);
        auto list = matjson::Value::array();
        for (int i = 0; i < 40; i++) list.push(entry);
        r.manifest["sps"] = list;
    });
    for (auto [what, keep] : {std::pair{"cut off", 0.5}, std::pair{"not a file of the mod", 0.0}}) {
        auto file = dir / "smoke-variant.spcreator";
        ByteVector cut(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(bytes.size() * keep));
        if (keep == 0.0) cut.assign(4096, 0x41);
        (void)file::writeBinary(file, cut);
        check(what, file);
    }
    {
        // pack.json nested a million deep: matjson would recurse until the stack runs out.
        ByteVector deep(bytes.begin(), bytes.begin() + 8);
        uint32_t size = 1000000;
        deep.insert(deep.end(), reinterpret_cast<uint8_t*>(&size), reinterpret_cast<uint8_t*>(&size) + 4);
        deep.insert(deep.end(), size, '[');
        auto file = dir / "smoke-variant.spcreator";
        (void)file::writeBinary(file, deep);
        check("pack.json nested deep", file);
    }
    std::error_code ec;
    std::filesystem::remove(dir / "smoke-variant.spcreator", ec);
    if (!pack) return;
    auto ids = [] {
        std::vector<int> ids;
        for (auto const& sp : g_session.sps) ids.push_back(sp->id);
        return fmt::format("{} ({})", ids.size(), fmt::join(ids, ","));
    };
    auto versions = [] {
        std::vector<std::string> list;
        for (auto const& v : storage::versions()) {
            list.push_back(fmt::format("{}{}: {} SPs, {} KB, {}{}", utils::string::pathToString(v.dir.filename()),
                v.name.empty() ? "" : fmt::format(" \"{}\"", v.name), v.sps, v.bytes / 1024, asideKind(v.why),
                v.fits ? "" : ", doesn't fit"));
        }
        return fmt::format("{} [{}]", list.size(), fmt::join(list, "; "));
    };
    auto all = [](transfer::Pack const& p) { return std::vector<bool>(p.items.size(), true); };
    logSwitchers("before replacing");
    // The file in place of the level's SPs, the same ones: they go to a version, and come in again.
    auto result = transfer::take(*pack, all(*pack), core::Replace::SaveVersion);
    log::info("Smoke test: replaced by the import, saving a version: {} added, {} failed, {} not gone, SPs now {}, versions {}",
        result.added, result.failed, result.notMoved, ids(), versions());
    auto level = storage::level();
    auto list = storage::versions();
    if (!level || list.empty()) return log::error("Smoke test: no version");
    // The last one deleted: the version brings it back.
    if (g_session.sps.size() == 3) {
        // A copy: removing takes the SP out of the vector the reference would point into.
        auto last = g_session.sps.back();
        removeSP(PlayLayer::get(), last);
        auto version = transfer::openVersion(list.front());
        if (version.isErr()) return log::error("Smoke test: the version refused: {}", version.unwrapErr());
        auto const& items = version.unwrap().items;
        log::info("Smoke test: the version: {} SPs, {} already here, the level's own {}", items.size(),
            std::count_if(items.begin(), items.end(), [](auto const& i) { return i.here; }), version.unwrap().sameAsLevel);
        auto added = transfer::take(version.unwrap(), all(version.unwrap()), core::Replace::No);
        log::info("Smoke test: the version added: {} added, SPs now {}", added.added, ids());
        auto again = transfer::openVersion(list.front());
        if (again.isErr()) return log::error("Smoke test: the version refused: {}", again.unwrapErr());
        log::info("Smoke test: the version again: the level's own {}", again.unwrap().sameAsLevel);
        // In place of the level's, deleting them: no other version.
        auto replaced = transfer::take(again.unwrap(), all(again.unwrap()), core::Replace::Delete);
        log::info("Smoke test: replaced by the version, deleting: {} added, {} not gone, SPs now {}, versions {}", replaced.added,
            replaced.notMoved, ids(), versions());
    }
    // Deleting the level's SPs while one of the new ones fails: they're saved instead.
    if (!g_session.sps.empty()) {
        auto file = storage::fileOf(*g_session.sps.front());
        ByteVector junk(4096, 0x41);
        if (file.isOk()) {
            auto partly = core::import({{file.unwrap(), ""}, {junk, ""}}, core::Replace::Delete);
            log::info("Smoke test: deleting with one of two failing (the same SPs as the version): {} added, {} failed, saved {}, "
                "in a version already {}, SPs now {}, versions {}", partly.added, partly.failed, partly.saved, partly.existing,
                ids(), versions());
        }
    }
    // Saving SPs a version has already makes no copy: that one goes up, its name kept. Not when a name of
    // theirs differs, or that version's copy is damaged.
    auto saveAgain = [&](std::string_view what) {
        auto again = transfer::take(*pack, all(*pack), core::Replace::SaveVersion);
        log::info("Smoke test: {}: {} added, saved {}, in a version already {}, SPs now {}, versions {}", what, again.added,
            again.saved, again.existing, ids(), versions());
    };
    if (auto named = storage::versions(); !named.empty()) {
        log::info("Smoke test: version renamed: {}, versions {}", storage::renameVersion(named.front(), "Smoke версия"), versions());
    }
    saveAgain("the file in place of 1 SP");
    if (!g_session.sps.empty()) core::rename(g_session.sps.front(), "Named");
    saveAgain("the file in place of the same SPs, one named");
    saveAgain("the file in place of the same SPs");
    if (auto newest = storage::versions(); !newest.empty()) {
        std::error_code ec3;
        for (auto it = std::filesystem::directory_iterator(newest.front().dir, ec3); !ec3 && it != std::filesystem::directory_iterator();
             it.increment(ec3)) {
            if (it->path().extension() != ".spc") continue;
            auto bytes = file::readBinary(it->path());
            if (bytes.isOk() && !bytes.unwrap().empty()) {
                bytes.unwrap()[bytes.unwrap().size() / 2] ^= 0x5a;
                (void)file::writeBinary(it->path(), bytes.unwrap());
                log::info("Smoke test: damaged {} of {}", it->path().filename(), newest.front().dir.filename());
            }
            break;
        }
    }
    saveAgain("the file in place of the same SPs, the version's copy damaged");
    list = storage::versions();
    if (list.empty()) return log::error("Smoke test: no version");
    // One made for another version of the level: listed, refused, deleted like the others.
    std::error_code ec2;
    auto old = list.front().dir.parent_path() / (level->key + "-old-1000000000");
    std::filesystem::create_directories(old, ec2);
    auto identity = level->identity;
    identity["version"] = identity["version"].asInt().unwrapOr(0) + 1;
    (void)file::writeString(old / "level.json", identity.dump());
    for (auto it = std::filesystem::directory_iterator(list.front().dir, ec2); !ec2 && it != std::filesystem::directory_iterator();
         it.increment(ec2)) {
        if (it->path().extension() == ".spc") {
            std::filesystem::copy_file(it->path(), old / it->path().filename(), ec2);
            break;
        }
    }
    log::info("Smoke test: with one for another level version: versions {}", versions());
    for (auto const& v : storage::versions()) {
        if (v.fits) continue;
        auto refused = transfer::openVersion(v);
        log::info("Smoke test: opening it: {}", refused.isErr() ? refused.unwrapErr() : std::string("opened"));
    }
    // Never the level's own folder.
    storage::Version own;
    own.dir = list.front().dir.parent_path() / level->key;
    log::info("Smoke test: deleting the level's own folder as a version: {}, it's there {}", storage::removeVersion(own),
        std::filesystem::exists(own.dir / "level.json", ec2));
    int removed = 0;
    for (auto const& v : storage::versions()) removed += storage::removeVersion(v);
    log::info("Smoke test: deleted {} versions, versions now {}, folders there {} {}", removed, versions(),
        std::filesystem::exists(list.front().dir, ec2), std::filesystem::exists(old, ec2));
    logSwitchers("after replacing");
}

// At the level start, after a run that saved SPs and versions: what came back, and a version that holds the
// level's SPs again is found as such (it's all on disk: nothing of the last visit is left in memory).
void smokeCheckSaved() {
    auto describe = [] {
        std::vector<std::string> sps, list;
        for (auto const& sp : g_session.sps) sps.push_back(fmt::format("{} \"{}\" {:.1f}%", sp->id, sp->name, sp->percent));
        for (auto const& v : storage::versions()) {
            std::vector<std::string> names;
            if (auto files = storage::readVersion(v); files.isOk()) {
                for (auto const& f : files.unwrap()) names.push_back(fmt::format("\"{}\"", f.name));
            }
            list.push_back(fmt::format("{} \"{}\": {} SPs [{}]{}", utils::string::pathToString(v.dir.filename()), v.name, v.sps,
                fmt::join(names, ", "), v.fits ? "" : ", doesn't fit"));
        }
        return fmt::format("SPs [{}], versions {} [{}]", fmt::join(sps, ", "), list.size(), fmt::join(list, "; "));
    };
    log::info("Smoke test: saved before: {}", describe());
    auto list = storage::versions();
    if (list.empty()) return log::error("Smoke test: no version saved before");
    auto all = [](transfer::Pack const& p) { return std::vector<bool>(p.items.size(), true); };
    auto version = transfer::openVersion(list.front());
    if (version.isErr()) return log::error("Smoke test: the version refused: {}", version.unwrapErr());
    auto put = transfer::take(version.unwrap(), all(version.unwrap()), core::Replace::Delete);
    log::info("Smoke test: the version in place of the SPs: {} added, {}", put.added, describe());
    auto again = transfer::openVersion(storage::versions().front());
    if (again.isErr()) return log::error("Smoke test: the version refused: {}", again.unwrapErr());
    auto saved = transfer::take(again.unwrap(), all(again.unwrap()), core::Replace::SaveVersion);
    log::info("Smoke test: saving them again: saved {}, in a version already {}, {}", saved.saved, saved.existing, describe());
}

// At the level start: SPs from a file, as on another computer.
void smokeImport() {
    auto path = Mod::get()->getSaveDir() / g_smoke.importFrom;
    auto pack = transfer::open(path);
    if (pack.isErr()) return log::error("Smoke test: import of {} refused: {}", path, pack.unwrapErr());
    std::vector<bool> picked;
    for (auto const& item : pack.unwrap().items) picked.push_back(!item.here && !item.repeated);
    auto result = transfer::take(pack.unwrap(), picked, core::Replace::No);
    log::info("Smoke test: imported from {}: {} added, {} failed, {} SPs now", path, result.added, result.failed,
        g_session.sps.size());
}

// Last changed first: a setting changed twice ends as it was before both.
void restoreSmokeSettings() {
    auto& restores = g_smoke.restores;
    for (auto it = restores.rbegin(); it != restores.rend(); ++it) (*it)();
    restores.clear();
}

void finishSmokeTest() {
    saveUnsaved();
    restoreSmokeLevel();
    restoreSmokeSettings();
    if (g_smoke.cleanup) storage::removeAll();
    log::info("Smoke test done");
    geode::utils::game::exit(false);
}

// What's on screen, overlays (notifications) included, as <mod save dir>/<name>.
void captureScreen(std::string const& name) {
    auto director = CCDirector::get();
    auto size = director->getWinSize();
    auto rt = CCRenderTexture::create(static_cast<int>(size.width), static_cast<int>(size.height));
    rt->begin();
    director->getRunningScene()->visit();
    OverlayManager::get()->visit();
    rt->end();
    auto image = rt->newCCImage(true);
    auto path = Mod::get()->getSaveDir() / name;
    bool ok = image && image->saveToFile(utils::string::pathToString(path).c_str(), false);
    CC_SAFE_RELEASE(image);
    log::info("Smoke test: screen saved to {}: {}", path, ok);
}

// Touch priorities of the popups on screen, the top one last: menus of the popups below it must come after
// its own layer (a higher number), or they'd take clicks meant for it.
void logTouchPriorities(std::string_view when) {
    std::vector<FLAlertLayer*> popups;
    for (auto child : CCArrayExt<CCNode*>(CCScene::get()->getChildren())) {
        if (auto popup = typeinfo_cast<FLAlertLayer*>(child)) popups.push_back(popup);
    }
    if (popups.empty()) return;
    auto priority = [](CCTouchDelegate* d) {
        auto handler = CCTouchDispatcher::get()->findHandler(d);
        return handler ? handler->getPriority() : INT_MAX;
    };
    int top = priority(popups.back());
    bool fine = true;
    std::vector<std::string> parts;
    for (size_t i = 0; i < popups.size(); i++) {
        int low = INT_MAX, high = INT_MIN;
        std::function<void(CCNode*)> walk = [&](CCNode* node) {
            if (!node->getChildren()) return;
            for (auto child : CCArrayExt<CCNode*>(node->getChildren())) {
                if (auto menu = typeinfo_cast<CCMenu*>(child)) {
                    low = std::min(low, priority(menu));
                    high = std::max(high, priority(menu));
                }
                walk(child);
            }
        };
        walk(popups[i]);
        if (i + 1 < popups.size() && low <= top) fine = false;
        parts.push_back(fmt::format("{} {} (menus {} to {})", typeid(*popups[i]).name(), priority(popups[i]), low, high));
    }
    log::info("Smoke test: touch priorities {}: {}: {}", when, fine ? "fine" : "a popup below takes clicks", fmt::join(parts, "; "));
}

void pressButton(std::string_view id) {
    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(CCScene::get()->getChildByIDRecursive(id));
    log::info("Smoke test: pressing {}: {}", id, btn ? "found" : "not there");
    if (btn) btn->activate();
}

void afterFrames(int frames, std::function<void()> then);

// The end of the UI part: in a script, its popups closed and the game going on, then the next step.
void smokeUIDone() {
    if (!g_smoke.scripted) return finishSmokeTest();
    std::vector<FLAlertLayer*> popups;
    for (auto child : CCArrayExt<CCNode*>(CCScene::get()->getChildren())) {
        if (auto popup = typeinfo_cast<FLAlertLayer*>(child)) popups.push_back(popup);
    }
    for (size_t i = popups.size(); i-- > 0;) popups[i]->keyBackClicked();
    afterFrames(10, [] {
        if (auto pause = CCScene::get()->getChildByType<PauseLayer>(0)) pause->onResume(nullptr);
        afterFrames(30, nextAction);
    });
}

// The mod's UI, one step every half second: a notification, the pause menu, the SP list, renaming.
void smokeUI(int stage) {
    auto pl = PlayLayer::get();
    if (!pl) return finishSmokeTest();
    auto& sps = g_session.sps;
    auto next = [stage](float delay = 0.6f) {
        CCScene::get()->runAction(CCSequence::create(CCDelayTime::create(delay),
            CallFuncExt::create([stage] { smokeUI(stage + 1); }), nullptr));
    };
    auto pause = CCScene::get()->getChildByType<PauseLayer>(0);
    // The notification's labels, for when a picture shows no text.
    auto describeNotice = [] {
        if (!g_notice) return;
        std::vector<CCLabelBMFont*> labels;
        std::function<void(CCNode*)> collect = [&](CCNode* node) {
            if (!node->getChildren()) return;
            for (auto child : CCArrayExt<CCNode*>(node->getChildren())) {
                if (auto label = typeinfo_cast<CCLabelBMFont*>(child)) labels.push_back(label);
                collect(child);
            }
        };
        collect(g_notice);
        // Its box against the screen: it has to fit.
        if (auto bg = g_notice->getChildByType<NineSlice>(0)) {
            auto from = bg->convertToWorldSpace({0.f, 0.f});
            auto to = bg->convertToWorldSpace({bg->getContentWidth(), bg->getContentHeight()});
            log::info("Smoke test: notice box x {:.0f} to {:.0f} of a screen {:.0f} wide", from.x, to.x,
                CCDirector::get()->getWinSize().width);
        }
        for (auto label : labels) {
            auto size = label->getScaledContentSize();
            log::info("Smoke test: notice label \"{}\" font {} visible {} opacity {} displayed {} size {:.1f}x{:.1f}",
                label->getString(), label->getFntFile(), label->isVisible(), label->getOpacity(),
                label->getDisplayedOpacity(), size.width, size.height);
        }
    };
    switch (stage) {
        case 0:
            // The longest kind: it has to fit the screen.
            notify(NotificationIcon::Warning, Msg::SPsWithoutLowDetail);
            return next(0.5f);
        case 1:
            describeNotice();
            captureScreen("smoke-notice-en.png");
            if (!sps.empty()) {
                // The SP's object should stay unseen in front of the player.
                core::rename(sps.front(), "Корабль ship");
                choose(sps.front(), true);
                pl->resetLevel();
            }
            notify(NotificationIcon::Success, Msg::Placed, labelOf(sps.empty() ? nullptr : sps.front()));
            // In the mod's own label too.
            notify("Твои SP здесь — без Low Detail Mode: выключи его, и ещё длиннее, чем бывает", NotificationIcon::Warning);
            return next(0.5f);
        case 2:
            describeNotice();
            captureScreen("smoke-notice.png");
            g_smoke.pausing = true;
            pl->pauseGame(false);
            g_smoke.pausing = false;
            return next();
        case 3:
            captureScreen("smoke-pause.png");
            logSwitchers("in the pause menu");
            if (auto btn = pause ? pause->getChildByIDRecursive("sp-list-button"_spr) : nullptr) {
                auto at = btn->getParent()->convertToWorldSpace(btn->getPosition());
                auto size = CCDirector::get()->getWinSize();
                log::info("Smoke test: SP list button at {:.0f},{:.0f} of {:.0f}x{:.0f}, visible {}", at.x, at.y, size.width,
                    size.height, btn->isVisible());
            }
            else log::warn("Smoke test: no SP list button in the pause menu");
            if (pause) splist::open(pause);
            else log::error("Smoke test: no pause menu");
            return next();
        case 4:
            captureScreen("smoke-list.png");
            if (g_smoke.dialogs) return smokeUI(20);
            if (!sps.empty()) splist::openRename(sps.front());
            return next();
        case 5: {
            // Typing goes through the IME dispatcher, as keyboard text does: does the name field take Cyrillic?
            auto input = findFirstChildRecursive<TextInput>(CCScene::get(), [](auto) { return true; });
            if (input) {
                input->setString("");
                input->focus();
                std::string typed = "Жёлтый ship 2";
                CCIMEDispatcher::sharedDispatcher()->dispatchInsertText(typed.c_str(), static_cast<int>(typed.size()), KEY_None);
                log::info("Smoke test: typed \"{}\" into the name field, it holds \"{}\"", typed, std::string(input->getString()));
            }
            else log::warn("Smoke test: no name field");
            return next(0.3f);
        }
        case 6:
            captureScreen("smoke-rename.png");
            pressButton("save-name-button"_spr);
            splist::openExport();
            return next();
        case 7: {
            captureScreen("smoke-export.png");
            // The SPs of an earlier test, placed at the same x: close to these, or the same.
            auto pack = transfer::open(Mod::get()->getSaveDir() / "smoke-export.spcreator");
            if (pack.isOk()) splist::openImport(std::move(pack).unwrap());
            else log::warn("Smoke test: no import list: {}", pack.unwrapErr());
            return next();
        }
        case 8:
            captureScreen("smoke-import.png");
            pressButton("replace-all-button"_spr);
            return next();
        case 9:
            captureScreen("smoke-replace.png");
            g_lastNotice.clear();
            pressButton("save-and-replace-button"_spr);
            return next();
        case 10:
            log::info("Smoke test: replaced: notice \"{}\", {} SPs", g_lastNotice, sps.size());
            describeNotice();
            pressButton("versions-button"_spr);
            return next();
        case 11:
            captureScreen("smoke-versions.png");
            pressButton(fmt::format("{}/rename-version-0", Mod::get()->getID()));
            return next();
        case 12: {
            auto input = findFirstChildRecursive<TextInput>(CCScene::get(), [](auto) { return true; });
            if (input) input->setString("Перед концовкой (ship)");
            else log::warn("Smoke test: no name field");
            captureScreen("smoke-rename-version.png");
            pressButton("save-name-button"_spr);
            return next();
        }
        case 13: {
            auto list = storage::versions();
            log::info("Smoke test: version renamed in the list: \"{}\"", list.empty() ? std::string("none") : list.front().name);
            captureScreen("smoke-versions-named.png");
            pressButton(fmt::format("{}/open-version-0", Mod::get()->getID()));
            return next();
        }
        case 14:
            captureScreen("smoke-version.png");
            logTouchPriorities("with a version open");
            // The popups below rebuild their lists meanwhile.
            pressButton("add-button"_spr);
            return next();
        case 15: {
            logTouchPriorities("after adding from the version");
            // A delete confirmation that runs out under the next popup rebuilds the list below it.
            pressButton(fmt::format("{}/delete-version-0", Mod::get()->getID()));
            pressButton(fmt::format("{}/open-version-0", Mod::get()->getID()));
            return next(3.2f);
        }
        case 16: {
            logTouchPriorities("after a delete confirmation ran out below");
            // Back to the SP list (the first popup): it rebuilds once on top.
            std::vector<FLAlertLayer*> above;
            for (auto child : CCArrayExt<CCNode*>(CCScene::get()->getChildren())) {
                if (auto popup = typeinfo_cast<FLAlertLayer*>(child)) above.push_back(popup);
            }
            for (size_t i = above.size(); i-- > 1;) above[i]->keyBackClicked();
            return next();
        }
        case 17: {
            logTouchPriorities("back at the SP list");
            auto popup = CCScene::get()->getChildByType<FLAlertLayer>(0);
            auto list = popup ? findFirstChildRecursive<ScrollLayer>(popup, [](auto) { return true; }) : nullptr;
            log::info("Smoke test: SP list rows: {} for {} SPs", list ? list->m_contentLayer->getChildrenCount() : 0, sps.size());
            captureScreen("smoke-list-end.png");
            logSwitchers("at the end");
            return smokeUIDone();
        }
        // The file dialogs through the buttons, as a player goes: export the SPs, then import that file (all
        // of it already here). tools/answer_dialogs.ps1 fills the dialogs in, from outside the game.
        case 20:
            pressButton("export-button"_spr);
            return next();
        case 21:
            g_lastNotice.clear();
            pressButton("export-picked-button"_spr);
            g_smoke.waitFrom = CCDirector::get()->getTotalFrames();
            return smokeUI(22);
        case 22:
        case 24: {
            // The game goes on while the dialog is open: frames keep coming.
            bool done = g_lastNotice == (stage == 22 ? text::format(Msg::Exported, sps.size()) : std::string(text::tr(Msg::AllAlreadyHere)));
            if (!done && ++g_smoke.waits < 120) {
                CCScene::get()->runAction(CCSequence::create(CCDelayTime::create(0.25f),
                    CallFuncExt::create([stage] { smokeUI(stage); }), nullptr));
                return;
            }
            log::info("Smoke test: dialog {}: {} after {} frames, notice \"{}\"", stage == 22 ? "export" : "import",
                done ? "done" : "timed out", CCDirector::get()->getTotalFrames() - g_smoke.waitFrom, g_lastNotice);
            g_smoke.waits = 0;
            if (stage == 24) {
                int open = 0;
                for (auto child : CCArrayExt<CCNode*>(CCScene::get()->getChildren())) {
                    open += typeinfo_cast<FLAlertLayer*>(child) != nullptr;
                }
                log::info("Smoke test: after the import: {} popups open, SP list {}, {} SPs", open,
                    CCScene::get()->getChildByIDRecursive("import-button"_spr) ? "there" : "gone", sps.size());
                captureScreen("smoke-dialog.png");
                return smokeUIDone();
            }
            return next();
        }
        case 23:
            g_lastNotice.clear();
            pressButton("import-button"_spr);
            // A second press while the dialog is open opens no other.
            pressButton("import-button"_spr);
            g_smoke.waitFrom = CCDirector::get()->getTotalFrames();
            return smokeUI(24);
    }
}

// Frames go on across scenes, unlike a scene's actions.
void afterFrames(int frames, std::function<void()> then) {
    Loader::get()->queueInMainThread([frames, then = std::move(then)]() mutable {
        if (frames <= 0) return then();
        afterFrames(frames - 1, std::move(then));
    });
}

void pressToggle(std::string_view id) {
    auto toggler = typeinfo_cast<CCMenuItemToggler*>(CCScene::get()->getChildByIDRecursive(id));
    log::info("Smoke test: ticking {}: {}", id, toggler ? "found" : "not there");
    if (toggler) toggler->activate();
}

std::string describeStorage() {
    std::vector<std::string> parts;
    for (auto const& l : storage::storedLevels()) {
        parts.push_back(fmt::format("{} \"{}\"{}{}: {} SPs, {} KB; {} versions, {} KB", l.key, l.name, l.lowDetail ? " LDM" : "",
            l.open ? " (open)" : "", l.sps, l.spBytes / 1024, l.versions.size(), l.versionBytes / 1024));
    }
    return fmt::format("{} [{}]", parts.size(), fmt::join(parts, "; "));
}

std::vector<std::string> storageFolders(std::string const& name = g_smoke.storageRoot) {
    std::vector<std::string> names;
    std::error_code ec;
    auto root = Mod::get()->getSaveDir() / name;
    for (auto it = std::filesystem::directory_iterator(root, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
        names.push_back(utils::string::pathToString(it->path().filename()));
    }
    std::sort(names.begin(), names.end());
    return names;
}

// Index in the storage list (biggest first) of the first level `pred` takes, -1 if none.
template <class F>
int storageIndex(F pred) {
    auto levels = storage::storedLevels();
    for (size_t i = 0; i < levels.size(); i++) {
        if (pred(levels[i])) return static_cast<int>(i);
    }
    return -1;
}

void closeTopPopup() {
    FLAlertLayer* top = nullptr;
    for (auto child : CCArrayExt<CCNode*>(CCScene::get()->getChildren())) {
        if (auto popup = typeinfo_cast<FLAlertLayer*>(child)) top = popup;
    }
    if (top) top->keyBackClicked();
}

// The storage manager on a copy of the player's SPs: from the mod's settings, through its buttons.
void smokeStorage(int stage) {
    static std::string key;  // the level with versions it works on
    auto next = [stage](float delay = 0.6f) {
        CCScene::get()->runAction(CCSequence::create(CCDelayTime::create(delay),
            CallFuncExt::create([stage] { smokeStorage(stage + 1); }), nullptr));
    };
    auto id = [](std::string_view what) { return fmt::format("{}/{}", Mod::get()->getID(), what); };
    switch (stage) {
        case 0:
            // From the pause menu, as a player would.
            if (auto pl = PlayLayer::get()) {
                g_smoke.pausing = true;
                pl->pauseGame(false);
                g_smoke.pausing = false;
            }
            log::info("Smoke test: storage folders: {}", fmt::join(storageFolders(), ", "));
            log::info("Smoke test: storage: {}", describeStorage());
            log::info("Smoke test: storage folders after looking: {}", fmt::join(storageFolders(), ", "));
            openSettingsPopup(Mod::get(), false);
            return next();
        case 1:
            captureScreen("smoke-settings.png");
            pressButton(id("storage-button"));
            return next();
        case 2: {
            captureScreen("smoke-storage.png");
            int i = storageIndex([](auto const& l) { return l.versions.size() >= 2 && !l.open; });
            if (i < 0) i = storageIndex([](auto const& l) { return !l.versions.empty() && !l.open; });
            if (i < 0) {
                log::error("Smoke test: storage: no level with versions");
                return finishSmokeTest();
            }
            key = storage::storedLevels()[i].key;
            log::info("Smoke test: storage: opening {}", key);
            pressButton(id(fmt::format("storage-open-{}", i)));
            return next();
        }
        case 3:
            captureScreen("smoke-storage-level.png");
            pressToggle(id("storage-pick-version-0"));
            g_lastNotice.clear();
            pressButton(id("storage-level-delete-picked"));
            return next(0.5f);
        case 4:
            log::info("Smoke test: storage: first click: notice \"{}\"", g_lastNotice);
            captureScreen("smoke-storage-confirm.png");
            pressButton(id("storage-level-delete-picked"));
            log::info("Smoke test: storage: second click: notice \"{}\", {}", g_lastNotice, describeStorage());
            return next();
        case 5:
            captureScreen("smoke-storage-level-after.png");
            closeTopPopup();
            return next();
        case 6: {
            int i = storageIndex([](auto const& l) { return l.key == key; });
            pressToggle(id(fmt::format("storage-pick-{}", i)));
            pressButton(id("storage-delete-versions"));
            pressButton(id("storage-delete-versions"));
            log::info("Smoke test: storage: versions of {} deleted: notice \"{}\", {}", key, g_lastNotice, describeStorage());
            int open = storageIndex([](auto const& l) { return l.open; });
            pressButton(id(fmt::format("storage-open-{}", open)));
            return next();
        }
        case 7:
            captureScreen("smoke-storage-open.png");
            pressButton(id("storage-level-delete-entirely"));
            pressButton(id("storage-level-delete-entirely"));
            log::info("Smoke test: storage: the open level entirely: notice \"{}\", {}", g_lastNotice, describeStorage());
            closeTopPopup();
            return next();
        case 8: {
            pressButton(id("storage-select-all"));
            pressButton(id("storage-delete-entirely"));
            pressButton(id("storage-delete-entirely"));
            log::info("Smoke test: storage: all levels entirely: notice \"{}\", {}, folders {}", g_lastNotice, describeStorage(),
                fmt::join(storageFolders(), ", "));
            auto levels = storage::storedLevels();
            int open = storageIndex([](auto const& l) { return l.open; });
            if (open >= 0) {
                auto const& l = levels[open];
                storage::Version outside;
                outside.dir = l.dir.parent_path().parent_path() / "levels";
                log::info("Smoke test: storage: deleting the open level's SPs: {}; a folder outside as a version: {}",
                    storage::removeLevelSPs(l), storage::removeStoredVersion(l, outside));
            }
            log::info("Smoke test: storage: the open level's SPs: {} in the session", g_session.sps.size());
            return next();
        }
        case 9: {
            captureScreen("smoke-storage-end.png");
            // Out of the level: none of them is the open one any more.
            if (auto pl = PlayLayer::get()) pl->onQuit();
            afterFrames(120, [] {
                log::info("Smoke test: storage from the menu: {}", describeStorage());
                finishSmokeTest();
            });
            return;
        }
    }
}

void smokeQuit() {
    auto root = g_smoke.storageRoot;  // the test's state goes with the level
    log::info("Smoke test: folders before leaving: {}", fmt::join(storageFolders(root), ", "));
    if (auto pl = PlayLayer::get()) pl->onQuit();
    afterFrames(120, [root] {
        log::info("Smoke test: folders after leaving: {}; storage: {}", fmt::join(storageFolders(root), ", "), describeStorage());
        finishSmokeTest();
    });
}

// What moving SPs over stands on, seen in the level right after its SPs are loaded: the level string's
// objects in m_objects order (the update test deletes by index), the prints' start fields set, and the
// objects each SP's checkpoint refers to (<mod save dir>/smoke-refs.json: indices, their object IDs and x).
void smokeProbe(PlayLayer* pl) {
    auto raw = std::string(ZipUtils::decompressString(pl->m_level->m_levelString, false, 0));
    auto parts = utils::string::split(raw, ";");
    std::vector<std::string> objects(parts.begin() + (parts.empty() ? 0 : 1), parts.end());
    std::erase_if(objects, [](auto const& o) { return o.empty(); });
    size_t count = pl->m_objects->count(), checked = std::min(objects.size(), count), sameId = 0, sameX = 0, unset = 0;
    long long firstOff = -1;
    for (size_t i = 0; i < checked; i++) {
        auto obj = static_cast<GameObject*>(pl->m_objects->objectAtIndex(i));
        auto fields = utils::string::split(objects[i], ",");
        int id = 0;
        float x = NAN;
        for (size_t k = 0; k + 1 < fields.size(); k += 2) {
            if (fields[k] == "1") id = utils::numFromString<int>(fields[k + 1]).unwrapOr(0);
            if (fields[k] == "2") x = utils::numFromString<float>(fields[k + 1]).unwrapOr(NAN);
        }
        bool same = id == obj->m_objectID && std::abs(x - obj->m_startPosition.x) < 0.01f;
        sameId += id == obj->m_objectID;
        sameX += std::abs(x - obj->m_startPosition.x) < 0.01f;
        if (!same && firstOff < 0) firstOff = static_cast<long long>(i);
        unset += obj->m_startPosition.x == 0.f && obj->m_startPosition.y == 0.f;
    }
    log::info("Smoke test: probe: {} objects in the string, {} in m_objects; of {}: {} same ID, {} same x (start position), "
        "first off at {}; {} with no start position", objects.size(), count, checked, sameId, sameX, firstOff, unset);

    auto dump = matjson::Value::object();
    for (auto const& refs : storage::referencesOfSPs()) {
        auto const& seen = refs.all;
        auto id = refs.id;
        auto list = matjson::Value::array();
        for (auto index : seen) {
            auto obj = index < count ? static_cast<GameObject*>(pl->m_objects->objectAtIndex(index)) : nullptr;
            list.push(matjson::makeObject({{"i", static_cast<int>(index)}, {"id", obj ? obj->m_objectID : -1},
                {"x", obj ? obj->m_startPosition.x : 0.f}, {"y", obj ? obj->m_startPosition.y : 0.f},
                {"t", obj && typeinfo_cast<EffectGameObject*>(obj) ? 1 : 0},
                {"s", std::binary_search(refs.states.begin(), refs.states.end(), index) ? 1 : 0}}));
        }
        log::info("Smoke test: probe: SP {} refers to {} objects{}, {} of them with an object state", id, seen.size(),
            seen.empty() ? std::string() : fmt::format(", {} to {}", seen.front(), seen.back()), refs.states.size());
        dump[fmt::to_string(id)] = std::move(list);
    }
    auto res = file::writeString(Mod::get()->getSaveDir() / "smoke-refs.json", dump.dump());
    if (res.isErr()) log::error("Smoke test: probe: {}", res.unwrapErr());
    // For picking objects to delete.
    (void)file::writeString(Mod::get()->getSaveDir() / "smoke-level.txt", raw);
}

// The version for the level before the update, taken back the Versions list's way, in place of the SPs
// moved over on entering: so the attempts after it are from these.
void smokeMoveVersion() {
    auto versions = storage::versions();
    for (auto const& v : versions) {
        log::info("Smoke test: version {}: {} SPs, fits {}, movable {}", v.dir.filename(), v.sps, v.fits, v.movable);
    }
    auto it = std::find_if(versions.begin(), versions.end(), [](auto const& v) { return v.movable; });
    if (it == versions.end()) return log::error("Smoke test: no version to move over");
    auto pack = transfer::openVersion(*it);
    if (pack.isErr()) return log::error("Smoke test: moving the version over refused: {}", pack.unwrapErr());
    auto const& p = pack.unwrap();
    size_t here = std::count_if(p.items.begin(), p.items.end(), [](auto const& item) { return item.here; });
    log::info("Smoke test: version moved over: {} SPs ({} like the level's), not moved {} ({})", p.items.size(), here,
        p.notMoved.size(), text::percents(p.notMoved));
    auto result = transfer::take(p, std::vector<bool>(p.items.size(), true), core::Replace::Delete);
    log::info("Smoke test: in place of the level's SPs: {} added, {} failed, {} SPs now", result.added, result.failed,
        g_session.sps.size());
}

// The Versions list with a version for the level before the update, and that version opened.
void smokeVersionsUI(int stage) {
    auto pl = PlayLayer::get();
    if (!pl) return finishSmokeTest();
    auto next = [stage](float delay = 0.6f) {
        CCScene::get()->runAction(CCSequence::create(CCDelayTime::create(delay),
            CallFuncExt::create([stage] { smokeVersionsUI(stage + 1); }), nullptr));
    };
    switch (stage) {
        case 0:
            g_smoke.pausing = true;
            pl->pauseGame(false);
            g_smoke.pausing = false;
            return next();
        case 1:
            pressButton("sp-list-button"_spr);
            return next();
        case 2:
            pressButton("versions-button"_spr);
            return next();
        case 3:
            captureScreen("smoke-versions-move.png");
            g_lastNotice.clear();
            pressButton(fmt::format("{}/move-version-0", Mod::get()->getID()));
            return next();
        case 4:
            log::info("Smoke test: moving from the list: notice \"{}\"", g_lastNotice);
            captureScreen("smoke-version-moved.png");
            return finishSmokeTest();
    }
}

void nextSmokePhase() {
    auto pl = PlayLayer::get();
    auto& t = g_smoke;
    if (!pl || t.storageUI || t.quit) return;
    if (t.externalSeconds > 0 && !t.externalDone) {
        t.externalDone = t.externalOn = true;
        log::info("Smoke test: {} s for other mods' keys, {} to press", t.externalSeconds, t.externalKeys.size());
        t.stepsLeft = t.externalSeconds * STEPS_PER_SECOND;
        return;
    }
    if (t.externalOn) {
        t.externalOn = false;
        t.refIndex = -1;
    }
    if (t.tested > 0 && !t.deathPhase && t.compat == 0) {
        float x = pl->m_player1->m_position.x;
        log::info("Smoke test: SP {} attempt, x {:.1f} to {:.1f}: {} hits, original run {}", g_session.sps[t.tested - 1]->id,
            t.phaseStartX, x, g_rec.hits, firstRunHits(t.phaseStartX, x));
    }
    if (!t.exportTo.empty() && !t.exported && t.tested >= g_session.sps.size() && !g_session.sps.empty()) {
        t.exported = true;
        smokeTransfer();
        t.tested = 0;  // the imported SPs get their attempts too
    }
    if (t.tested >= g_session.sps.size() && !t.deathPhase && !g_session.sps.empty() && smokeCompat(pl)) return;
    if (t.tested < g_session.sps.size()) {
        auto sp = g_session.sps[t.tested++];
        choose(sp, true);
        t.base = sp->spawnStep;
        t.refIndex = -1;
        t.stepsLeft = sp->handoff() + TAIL_STEPS + 200;
        log::info("Smoke test: start from SP {}", sp->id);
        pl->resetLevel();
        t.phaseStartX = pl->m_player1->m_position.x;
        return;
    }
    if (!t.deathPhase && !g_session.sps.empty()) {
        // Test mode must keep a real death from the SP from saving a new best.
        t.deathPhase = true;
        t.dying = true;
        t.stepsLeft = 2000;
        choose(g_session.sps.back(), true);
        log::info("Smoke test: dying from SP {}: best before {}%, level attempts {}", g_session.active->id,
            pl->m_level->m_normalPercent.value(), pl->m_level->m_attempts.value());
        pl->resetLevel();
        return;
    }
    if (t.dying) {
        t.dying = false;
        log::warn("Smoke test: no death from the SP within 2000 steps");
    }
    if (!t.backAtStart && g_session.sps.size() >= 2) {
        // Delete the second SP (restart from the first), then the first (restart from the level start).
        for (size_t i : {size_t(1), size_t(0)}) {
            choose(g_session.sps[i]);
            deleteSP();
            log::info("Smoke test: after deleting: {} SPs, {}, player at x {:.1f}, our checkpoint {}", g_session.sps.size(),
                activeLabel(), pl->m_player1->m_position.x,
                g_session.active && pl->m_currentCheckpoint == g_session.active->cp ? "loaded" : "not loaded");
        }
        // capeling's switcher keeps the deleted SPs' objects: its next one is the first SP, deleted now.
        if (Loader::get()->getLoadedMod(CAPELING_SWITCHER)) {
            foreignSwitch(pl, 1);
            logRestart(pl, "another switcher picks a deleted SP");
        }
    }
    if (!t.backAtStart) {
        t.backAtStart = true;
        t.base = 0;
        t.refIndex = -1;
        t.stepsLeft = 100;
        choose(nullptr, true);
        pl->resetLevel();
        log::info("Smoke test: back at the level start, player at x {:.1f}, startpos {}, test mode {}",
            pl->m_player1->m_position.x, pl->m_startPosObject ? "set" : "none", pl->m_isTestMode);
        logSwitchers("back at the level start");
        // MegaHack's switcher through its own key (E: next): its label shows how many startposes it has.
        if (Loader::get()->getLoadedMod("absolllute.megahack")) {
            t.stepsLeft = 600;
            for (int i = 1; i <= 2; i++) {
                CCScene::get()->runAction(CCSequence::create(CCDelayTime::create(0.5f * i), CallFuncExt::create([i] {
                    pressKey(KEY_E);
                    CCScene::get()->runAction(CCSequence::create(CCDelayTime::create(0.2f), CallFuncExt::create([i] {
                        if (auto pl = PlayLayer::get()) logRestart(pl, fmt::format("MegaHack's next, {}", i));
                        logSwitchers(fmt::format("after MegaHack's next, {}", i));
                    }), nullptr));
                }), nullptr));
            }
        }
        return;
    }
    if (t.versionsUI && !t.versionsUIDone) {
        t.versionsUIDone = true;
        smokeVersionsUI(0);
        return;
    }
    if (t.ui && !t.uiDone) {
        t.uiDone = true;
        smokeUI(0);
        return;
    }
    finishSmokeTest();
}

// A script: "actions" in smoketest.json, steps run in turn, each one logged ("Smoke test: ACTION n: ...").
// The state lines ("STATE <tag>") are what runs compare.

void smokeDescribe(std::string const& tag) {
    std::vector<std::string> sps, files, versions;
    for (auto const& sp : g_session.sps) {
        sps.push_back(fmt::format("{} \"{}\" {:.2f}% x {:.1f} h{}", sp->id, sp->name, sp->percent, sp->x, sp->handoff()));
    }
    std::string names = "-", key = "-";
    int older = 0;
    uintmax_t bytes = 0;
    if (auto level = storage::level()) {
        key = level->key;
        auto dir = Mod::get()->getSaveDir() / g_smoke.storageRoot / level->key;
        std::error_code ec;
        for (auto it = std::filesystem::directory_iterator(dir, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
            files.push_back(utils::string::pathToString(it->path().filename()));
            std::error_code ec2;
            bytes += it->file_size(ec2);
            // Not compressed: of before SP files were.
            char magic[8] = {};
            if (it->path().extension() == ".spc") std::ifstream(it->path(), std::ios::binary).read(magic, sizeof(magic));
            older += std::string_view(magic, sizeof(magic)) == "SPCREATR";
        }
        std::sort(files.begin(), files.end());
        if (auto res = file::readString(dir / "names.json"); res.isOk()) {
            names = res.unwrap();
            std::erase_if(names, [](char c) { return c == '\n' || c == '\r'; });
        }
    }
    for (auto const& v : storage::versions()) {
        versions.push_back(fmt::format("{} \"{}\" {} SPs{}{}", utils::string::pathToString(v.dir.filename()), v.name, v.sps,
            v.fits ? "" : " (doesn't fit)", v.movable ? " (movable)" : ""));
    }
    log::info("Smoke test: STATE {}: root {}, level {}, {} SPs [{}], active {}, files [{}] ({} KB, {} not compressed), "
        "names {}, versions {} [{}]", tag, g_smoke.storageRoot, key, sps.size(), fmt::join(sps, "; "), activeLabel(),
        fmt::join(files, ", "), bytes / 1024, older, names, versions.size(), fmt::join(versions, "; "));
    // What Blitzkrieg finds: the StartPosObjects in m_objects. And the level's string, which the editor writes anew.
    if (auto pl = PlayLayer::get()) {
        int startPoses = 0, ofSPs = 0;
        for (auto obj : CCArrayExt<GameObject*>(pl->m_objects)) {
            if (auto spo = typeinfo_cast<StartPosObject*>(obj)) {
                startPoses++;
                ofSPs += spOf(spo) != nullptr;
            }
        }
        std::string_view s(pl->m_level->m_levelString.c_str(), pl->m_level->m_levelString.size());
        auto start = !pl->m_startPosObject ? "none" : spOf(pl->m_startPosObject) ? "of an SP" : "its own";
        log::info("Smoke test: STATE {}: {} startposes in m_objects ({} of SPs), the level's startpos {}, string {} bytes, "
            "hash {:016x}", tag, startPoses, ofSPs, start, s.size(), std::hash<std::string_view>{}(s));
    }
}

GJGameLevel* smokeLevel(std::string const& what) {
    if (auto id = utils::numFromString<int>(what); id.isOk()) {
        int i = id.unwrap();
        return i < 100 ? GameLevelManager::get()->getMainLevel(i, false) : GameLevelManager::get()->getSavedLevel(i);
    }
    for (auto local : CCArrayExt<GJGameLevel*>(LocalLevelManager::get()->m_localLevels)) {
        if (std::string(local->m_levelName) == what) return local;
    }
    return nullptr;
}

// Out of the level the way a player leaves it, and into `level` (the same one: "reenter"), the script going on there.
void smokeEnter(GJGameLevel* level) {
    auto& t = g_smoke;
    t.reentering = true;
    t.wait = SmokeTest::Wait::None;
    t.stepsLeft = -1;
    t.placeAtX.clear();
    t.placed = 0;
    t.level = level;
    if (auto pl = PlayLayer::get()) pl->onQuit();
    afterFrames(150, [] {
        log::info("Smoke test: entering \"{}\"", g_smoke.level->m_levelName);
        CCDirector::get()->replaceScene(PlayLayer::scene(g_smoke.level, false, false));
    });
}

// Out of the level into the editor, and back the way its Save and Play goes: the editor writes the level's
// string anew. `edit`: one object moved first, a change to the level.
void smokeResave(bool edit) {
    auto& t = g_smoke;
    if (!t.beforeResave) t.beforeResave = SmokeTest::Resaved{t.level, t.level->m_levelString, t.level->m_levelVersion};
    t.reentering = true;
    t.wait = SmokeTest::Wait::None;
    t.stepsLeft = -1;
    t.placeAtX.clear();
    t.placed = 0;
    if (auto pl = PlayLayer::get()) pl->onQuit();
    afterFrames(150, [edit] {
        log::info("Smoke test: resave: into the editor");
        CCDirector::get()->replaceScene(LevelEditorLayer::scene(g_smoke.level, false));
        afterFrames(150, [edit] {
            auto editor = LevelEditorLayer::get();
            if (!editor) {
                log::error("Smoke test: resave: no editor");
                return finishSmokeTest();
            }
            if (edit && editor->m_objects->count() > 10) {
                auto obj = static_cast<GameObject*>(editor->m_objects->objectAtIndex(10));
                obj->setPosition(obj->getPosition() + CCPoint(0, 30));
                log::info("Smoke test: resave: object {} (ID {}) moved up", 10, obj->m_objectID);
            }
            // As EditorPauseLayer::saveLevel writes it, in memory only: that one writes CCLocalLevels.dat too.
            gd::string before = g_smoke.level->m_levelString;
            g_smoke.level->m_levelString = ZipUtils::compressString(editor->getLevelString(), false, 0);
            log::info("Smoke test: resave: the level's string {} ({} -> {} bytes), {} objects; save and play",
                before == g_smoke.level->m_levelString ? "the same" : "written anew", before.size(),
                g_smoke.level->m_levelString.size(), editor->m_objects->count());
            CCDirector::get()->replaceScene(PlayLayer::scene(g_smoke.level, false, false));
        });
    });
}

void startPlay(PlayLayer* pl) {
    auto& t = g_smoke;
    auto sp = t.playList[t.playAt];
    t.idleSteps = 0;
    choose(sp, true);
    t.base = sp->spawnStep;
    t.refIndex = -1;
    t.handoffX = NAN;
    t.handoffMode.clear();
    pl->resetLevel();
    t.phaseStartX = pl->m_player1->m_position.x;
    t.stepsLeft = sp->handoff() + TAIL_STEPS + 200;
    t.wait = SmokeTest::Wait::Playing;
}

void logPlay(PlayLayer* pl) {
    auto& t = g_smoke;
    auto const& sp = t.playList[t.playAt];
    float x = pl->m_player1->m_position.x;
    bool handed = !std::isnan(t.handoffX);
    log::info("Smoke test: PLAY SP {} \"{}\" x {:.1f}: auto part {} steps, handed over at x {}, as {} (first pass {}); x {:.1f} to "
        "{:.1f}: {} hits, first pass {}", sp->id, sp->name, sp->x, sp->handoff(), handed ? fmt::format("{:.1f}", t.handoffX) : "-",
        handed ? t.handoffMode : "-", handed ? firstModeAt(t.handoffX) : "-", t.phaseStartX, x, g_rec.hits,
        firstRunHits(t.phaseStartX, x));
}

// The time an attempt or a wait took is up.
void stepsDone() {
    auto& t = g_smoke;
    auto pl = PlayLayer::get();
    if (!pl) return;
    if (t.wait == SmokeTest::Wait::Placing) {
        t.recordFirst = false;
        std::vector<std::string> modes;
        for (auto const& [x, mode] : t.firstModes) modes.push_back(fmt::format("{:.0f} {}", x, mode));
        log::info("Smoke test: placing done: {} SPs, first pass modes [{}]", g_session.sps.size(), fmt::join(modes, ", "));
    }
    else if (t.wait == SmokeTest::Wait::Playing) {
        logPlay(pl);
        if (++t.playAt < t.playList.size()) return startPlay(pl);
    }
    t.wait = SmokeTest::Wait::None;
    nextAction();
}

enumKeyCodes keyNamed(std::string const& name) {
    if (name == "Comma") return KEY_OEMComma;
    if (name == "Period") return KEY_OEMPeriod;
    if (name == "Delete") return KEY_Delete;
    if (name == "Space") return KEY_Space;
    if (name.size() == 1 && std::isalnum(static_cast<unsigned char>(name[0]))) {
        return static_cast<enumKeyCodes>(std::toupper(static_cast<unsigned char>(name[0])));
    }
    return KEY_None;
}

// `left` presses, `gap` frames apart (0: in the same frame).
void smokeKeys(enumKeyCodes key, int left, int gap) {
    pressKey(key);
    if (--left <= 0) return afterFrames(2, nextAction);
    if (gap <= 0) return smokeKeys(key, left, gap);
    afterFrames(gap, [=] { smokeKeys(key, left, gap); });
}

// Deleting several SPs through the SP list's buttons: `picks` "all" (Select all), "all2" (it twice: none),
// "none", or row indices ("0,2"); then saved first ("save") or not.
void smokeDeleteUI(int stage, std::string picks, bool save) {
    auto pl = PlayLayer::get();
    if (!pl) return finishSmokeTest();
    auto next = [=](int frames) { afterFrames(frames, [=] { smokeDeleteUI(stage + 1, picks, save); }); };
    switch (stage) {
        case 0:
            g_smoke.pausing = true;
            pl->pauseGame(false);
            g_smoke.pausing = false;
            return next(20);
        case 1:
            if (auto pause = CCScene::get()->getChildByType<PauseLayer>(0)) splist::open(pause);
            else log::error("Smoke test: no pause menu");
            return next(30);
        case 2:
            captureScreen("smoke-delete-list.png");
            pressButton("delete-button"_spr);
            return next(30);
        case 3:
            if (picks == "all" || picks == "all2") pressButton("delete-select-all-button"_spr);
            if (picks == "all2") pressButton("delete-select-all-button"_spr);
            if (picks != "all" && picks != "all2" && picks != "none") {
                for (auto const& i : utils::string::split(picks, ",")) {
                    pressToggle(fmt::format("{}/pick-{}", Mod::get()->getID(), i));
                }
            }
            return next(10);
        case 4:
            logTouchPriorities("delete list");
            captureScreen("smoke-delete.png");
            g_lastNotice.clear();
            pressButton("delete-picked-button"_spr);
            return next(30);
        case 5:
            if (!g_lastNotice.empty()) {
                // Nothing picked: no question, the list stays.
                log::info("Smoke test: DELETE {}: notice \"{}\", {} SPs", picks, g_lastNotice, g_session.sps.size());
                return smokeUIDone();
            }
            logTouchPriorities("save first");
            captureScreen("smoke-delete-confirm.png");
            pressButton(save ? "save-and-delete-button"_spr : "delete-only-button"_spr);
            return next(30);
        case 6: {
            std::vector<std::string> versions;
            for (auto const& v : storage::versions()) versions.push_back(fmt::format("{} SPs {}", v.sps, asideKind(v.why)));
            log::info("Smoke test: DELETE {} {}: notice \"{}\", {} SPs left, active {}, versions [{}]", picks,
                save ? "saved" : "deleted", g_lastNotice, g_session.sps.size(), labelOf(g_session.active),
                fmt::join(versions, "; "));
            captureScreen("smoke-delete-end.png");
            pressButton("versions-button"_spr);
            return next(30);
        }
        case 7:
            // What the versions say they were made by.
            captureScreen("smoke-delete-versions.png");
            return smokeUIDone();
    }
}

void smokeSetting(std::string const& key, std::string const& value) {
    auto mod = Mod::get();
    auto& restores = g_smoke.restores;
    if (key == "auto-part-length") {
        auto was = mod->getSettingValue<double>(key);
        restores.push_back([key, was] { Mod::get()->setSettingValue<double>(key, was); });
        mod->setSettingValue<double>(key, utils::numFromString<double>(value).unwrapOr(was));
    }
    else if (key == "language") {
        auto was = mod->getSettingValue<std::string>(key);
        restores.push_back([key, was] { Mod::get()->setSettingValue<std::string>(key, was); });
        mod->setSettingValue<std::string>(key, value);
    }
    else {
        auto was = mod->getSettingValue<bool>(key);
        restores.push_back([key, was] { Mod::get()->setSettingValue<bool>(key, was); });
        mod->setSettingValue<bool>(key, value == "true");
    }
    log::info("Smoke test: setting {} is {} now", key, value);
}

// A name typed into the rename popup of SP i, as from the keyboard, and saved.
void smokeRename(size_t i, std::string const& typed) {
    auto const& sps = g_session.sps;
    if (i >= sps.size()) {
        log::warn("Smoke test: no SP {} to rename", i);
        return afterFrames(2, nextAction);
    }
    splist::openRename(sps[i]);
    afterFrames(10, [typed] {
        auto input = findFirstChildRecursive<TextInput>(CCScene::get(), [](auto) { return true; });
        if (!input) {
            log::warn("Smoke test: no name field");
            return afterFrames(2, nextAction);
        }
        input->setString("");
        input->focus();
        CCIMEDispatcher::sharedDispatcher()->dispatchInsertText(typed.c_str(), static_cast<int>(typed.size()), KEY_None);
        log::info("Smoke test: typed \"{}\" ({} bytes), the field holds \"{}\"", typed, typed.size(), std::string(input->getString()));
        pressButton("save-name-button"_spr);
        afterFrames(10, nextAction);
    });
}

void nextAction() {
    auto& t = g_smoke;
    auto pl = PlayLayer::get();
    if (!t.active || t.reentering || !pl) return;
    if (t.actionAt >= t.actions.size()) return finishSmokeTest();
    auto const line = t.actions[t.actionAt++];
    t.idleSteps = 0;
    log::info("Smoke test: ACTION {}/{}: {}", t.actionAt, t.actions.size(), line);
    auto colon = line.find(':');
    auto op = line.substr(0, colon);
    auto rest = colon == std::string::npos ? std::string() : line.substr(colon + 1);
    auto args = utils::string::split(rest, ":");
    auto arg = [&](size_t i, int fallback) { return i < args.size() ? utils::numFromString<int>(args[i]).unwrapOr(fallback) : fallback; };
    auto next = [] { Loader::get()->queueInMainThread(nextAction); };
    auto const& sps = g_session.sps;
    if (op == "describe") {
        smokeDescribe(rest);
        return next();
    }
    if (op == "restart") {
        pl->resetLevel();
        return afterFrames(30, nextAction);
    }
    if (op == "reenter") return smokeEnter(t.level);
    if (op == "level") {
        auto level = smokeLevel(rest);
        if (!level || std::string(level->m_levelString).empty()) {
            log::error("Smoke test: level {} is not there", rest);
            return next();
        }
        return smokeEnter(level);
    }
    if (op == "root") {
        t.storageRoot = rest;
        storage::setRoot(rest);
        log::info("Smoke test: SPs in {} from the next level entered", rest);
        return next();
    }
    if (op == "place") {
        t.placeAtX.clear();
        for (auto const& x : utils::string::split(rest, ",")) t.placeAtX.push_back(utils::numFromString<float>(x).unwrapOr(0.f));
        std::sort(t.placeAtX.begin(), t.placeAtX.end());
        t.placed = 0;
        t.placeQueued = false;
        t.furthestX = 0;
        t.stillSteps = 0;
        t.recordFirst = true;
        t.firstRun.clear();
        t.firstModes.clear();
        t.base = 0;
        t.refIndex = -1;
        t.stepsLeft = -1;
        t.wait = SmokeTest::Wait::Placing;
        choose(nullptr, true);
        pl->resetLevel();
        return;
    }
    if (op == "key") {
        auto key = keyNamed(args.empty() ? "" : args[0]);
        if (key == KEY_None) {
            log::error("Smoke test: no key {}", rest);
            return next();
        }
        return smokeKeys(key, std::max(1, arg(1, 1)), arg(2, 1));
    }
    if (op == "down" || op == "up") {
        sendKey(keyNamed(rest), op == "down");
        return afterFrames(1, nextAction);
    }
    // The jump button as the game passes it on (keyboard jumps don't come through pressKey: Click Between
    // Frames takes them from raw input): "jump" presses, "jump:up" lets go.
    if (op == "jump") {
        bool down = rest != "up";
        bool before = g_rec.holds[1];
        pl->handleButton(down, 1, true);
        log::info("Smoke test: jump {} at x {:.1f}: auto part {}, jump held {} -> {}", down ? "pressed" : "let go",
            pl->m_player1->m_position.x, g_play.sp && g_play.controlling ? "goes on" : "off", before, g_rec.holds[1]);
        return afterFrames(1, nextAction);
    }
    if (op == "from" || op == "fromx") {
        size_t i = static_cast<size_t>(arg(0, 0));
        // fromx: the SP nearest to that x (indices move as SPs come and go).
        if (op == "fromx") {
            float x = utils::numFromString<float>(rest).unwrapOr(0.f);
            i = 0;
            for (size_t k = 1; k < sps.size(); k++) {
                if (std::abs(sps[k]->x - x) < std::abs(sps[i]->x - x)) i = k;
            }
        }
        if (i >= sps.size()) {
            log::warn("Smoke test: no SP {}", i);
            return next();
        }
        choose(sps[i], true);
        t.base = sps[i]->spawnStep;
        t.refIndex = -1;
        pl->resetLevel();
        t.phaseStartX = pl->m_player1->m_position.x;
        return next();
    }
    if (op == "at") {
        t.waitAuto = arg(0, 0);
        t.waitAutoSteps = 0;
        return;
    }
    if (op == "wait") {
        t.stepsLeft = std::max(1, arg(0, 60));
        t.wait = SmokeTest::Wait::Steps;
        return;
    }
    if (op == "play") {
        t.playList = sps;
        t.playAt = 0;
        if (t.playList.empty()) {
            log::info("Smoke test: no SPs to play");
            return next();
        }
        return startPlay(pl);
    }
    if (op == "setting") {
        if (args.size() >= 2) smokeSetting(args[0], rest.substr(args[0].size() + 1));
        return next();
    }
    if (op == "open") {
        auto path = Mod::get()->getSaveDir() / std::filesystem::path(utils::string::utf8ToWide(args.empty() ? "" : args[0]));
        auto pack = transfer::open(path);
        if (pack.isErr()) {
            log::info("Smoke test: OPEN {}: refused: {}", args.empty() ? "" : args[0], pack.unwrapErr());
            return next();
        }
        auto const& items = pack.unwrap().items;
        std::vector<std::string> names;
        for (auto const& item : items) names.push_back(fmt::format("\"{}\" ({} bytes)", item.name, item.name.size()));
        auto count = [&](auto pred) { return std::count_if(items.begin(), items.end(), pred); };
        log::info("Smoke test: OPEN {}: {} SPs, {} already here, {} repeated, {} damaged, the level's own {}, names [{}]", args[0],
            items.size(), count([](auto const& i) { return i.here; }), count([](auto const& i) { return i.repeated; }),
            pack.unwrap().damaged, pack.unwrap().sameAsLevel, fmt::join(names, ", "));
        if (args.size() < 2 || args[1] != "show") return next();
        // The import list with those names on it, then closed.
        splist::openImport(std::move(pack).unwrap());
        return afterFrames(30, [] {
            closeTopPopup();
            afterFrames(10, nextAction);
        });
    }
    if (op == "ui") {
        t.ui = t.uiDone = true;
        return smokeUI(0);
    }
    // "Add" of the SPs of a file that aren't here yet, as import_from does.
    if (op == "import") {
        t.importFrom = rest;
        smokeImport();
        return next();
    }
    if (op == "transfer") {
        if (t.exportTo.empty()) t.exportTo = "smoke-qa-export.spcreator";
        if (!sps.empty()) smokeTransfer();
        return next();
    }
    if (op == "delete_list") {
        size_t i = static_cast<size_t>(arg(0, 0));
        // A copy, as the list's button has: core::remove takes the SP out of the vector `sps` is.
        if (i < sps.size()) {
            auto sp = sps[i];
            core::remove(sp);
        }
        return next();
    }
    if (op == "delete_ui") {
        return smokeDeleteUI(0, args.empty() ? "all" : args[0], args.size() > 1 && args[1] == "save");
    }
    if (op == "rename") {
        auto nameAt = rest.find(':');
        return smokeRename(static_cast<size_t>(arg(0, 0)), nameAt == std::string::npos ? std::string() : rest.substr(nameAt + 1));
    }
    if (op == "resave") return smokeResave(rest == "edit");
    // The pause menu, and out of it through its buttons.
    if (op == "pause") {
        t.pausing = true;
        pl->pauseGame(false);
        t.pausing = false;
        return afterFrames(20, nextAction);
    }
    // Out of the level from the pause menu into the main menu, past onQuit: the test ends there.
    if (op == "pause_menu") {
        log::info("Smoke test: into the main menu, the level not quit");
        CCDirector::get()->replaceScene(MenuLayer::scene(false));
        return afterFrames(120, finishSmokeTest);
    }
    if (op == "resume" || op == "pause_restart" || op == "pause_practice" || op == "pause_normal") {
        auto pause = CCScene::get()->getChildByType<PauseLayer>(0);
        if (!pause) log::error("Smoke test: no pause menu");
        else if (op == "resume") pause->onResume(nullptr);
        else if (op == "pause_restart") pause->onRestart(nullptr);
        else if (op == "pause_practice") pause->onPracticeMode(nullptr);
        else pause->onNormalMode(nullptr);
        return afterFrames(30, nextAction);
    }
    if (op == "switchers") {
        logSwitchers(rest);
        return next();
    }
    if (op == "damage") {
        // The file of SP i made wrong inside (storage::variantOf), for the next load to set aside or read.
        size_t i = static_cast<size_t>(arg(0, 0));
        auto how = args.size() > 1 ? args[1] : std::string("garbled");
        auto level = storage::level();
        if (i < sps.size() && level) {
            core::saveAll();
            auto path = Mod::get()->getSaveDir() / t.storageRoot / level->key / fmt::format("sp-{}.spc", sps[i]->id);
            auto bytes = file::readBinary(path);
            auto res = bytes.isOk() ? file::writeBinary(path, storage::variantOf(bytes.unwrap(), how))
                                    : Result<>(Err(bytes.unwrapErr()));
            log::info("Smoke test: SP {} file {}: {}", sps[i]->id, how, res.isOk() ? "written" : res.unwrapErr());
        }
        return next();
    }
    log::error("Smoke test: no such step: {}", op);
    next();
}

// Before each step of the smoke test.
void smokeStep(PlayLayer* pl) {
    auto& t = g_smoke;
    if (t.dying) {
        // No input: the player crashes soon after the handoff.
    }
    else if (!t.reference.empty() && pl->m_level->m_levelID.value() == t.referenceLevel) {
        // During the auto part the SP's own replay drives.
        if (!(g_play.sp && g_play.controlling)) followReference(pl);
    }
    else {
        // Blind jumps: held 30 steps of every 120, counted in the steps of the first attempt. Never pressed in
        // the auto part (with control not blocked a press would end it): held as a player would, so the handoff
        // gives the game what the first attempt held there.
        int run = t.base + g_rec.step;
        bool hold = run % 120 < 30;
        if (g_play.sp && g_play.controlling) g_play.userHolds[1] = hold;
        else if (hold != g_rec.holds[1]) pl->handleButton(hold, 1, true);
        if (t.steer && !(g_play.sp && g_play.controlling)) {
            auto p = pl->m_player1;
            float x = p->m_position.x;
            auto it = std::lower_bound(t.portals.begin(), t.portals.end(), x - 5.f, [](CCPoint const& a, float x) { return a.x < x; });
            if (it != t.portals.end() && it->x - x < 45.f) {
                p->m_position.y = it->y;
                p->setPositionY(it->y);
                p->m_yVelocity = 0;
            }
        }
    }
    if (g_play.sp && g_play.controlling && g_play.step == g_play.sp->handoff()) {
        t.handoffX = pl->m_player1->m_position.x;
        t.handoffMode = modeOf(pl);
    }
    if (t.waitAuto >= 0) {
        if (g_play.sp && g_play.controlling && g_play.step >= t.waitAuto) {
            log::info("Smoke test: at step {} of the auto part of SP {}, x {:.1f}", g_play.step, g_play.sp->id, pl->m_player1->m_position.x);
            t.waitAuto = -1;
            Loader::get()->queueInMainThread(nextAction);
        }
        else if (++t.waitAutoSteps > 3000) {
            log::warn("Smoke test: no auto part step {} came", t.waitAuto);
            t.waitAuto = -1;
            Loader::get()->queueInMainThread(nextAction);
        }
    }
    if (t.externalOn && ++t.externalStep % EXTERNAL_KEY_STEPS == 0 && t.externalPressed < t.externalKeys.size()) {
        auto key = t.externalKeys[t.externalPressed++];
        log::info("Smoke test: pressing {}", static_cast<char>(key));
        // Out of the step: the key may restart the level.
        Loader::get()->queueInMainThread([key] { pressKey(key); });
    }
    if (t.takeoverAt >= 0 && g_play.sp && g_play.controlling && g_play.step == t.takeoverAt) {
        t.takeoverAt = -1;
        pl->handleButton(true, 1, true);
        log::info("Smoke test: pressed in the auto part: auto part {}, jump held {}", g_play.sp ? "goes on" : "ended",
            g_rec.holds[1]);
        t.checkHold = true;
    }
    else if (t.checkHold) {
        t.checkHold = false;
        log::info("Smoke test: a step after the press: jump held {}, auto part {}", g_rec.holds[1], g_play.sp ? "on" : "off");
    }
    if (t.recordFirst || (!t.scripted && t.tested == 0)) {
        float x = pl->m_player1->m_position.x;
        t.firstRun.push_back({x, g_rec.hits});
        if (t.recordFirst) {
            auto mode = modeOf(pl);
            if (t.firstModes.empty() || t.firstModes.back().second != mode) t.firstModes.push_back({x, mode});
        }
    }
    if (t.scripted && ++t.idleSteps > 240 * 180) {
        log::error("Smoke test: STUCK: a step waited 3 minutes, test ended (step {} of {})", t.actionAt, t.actions.size());
        t.idleSteps = INT_MIN;
        Loader::get()->queueInMainThread(finishSmokeTest);
        return;
    }
    if (t.placed < t.placeAtX.size() && t.scripted) {
        float x = pl->m_player1->m_position.x;
        if (x > t.furthestX + 1.f) {
            t.furthestX = x;
            t.stillSteps = 0;
        }
        else if (++t.stillSteps > 240 * 10) {
            log::error("Smoke test: STUCK: x stays at {:.1f} for 10 s, {} SPs left unplaced", x, t.placeAtX.size() - t.placed);
            t.placed = t.placeAtX.size();
            t.stepsLeft = TAIL_STEPS + 20;
        }
    }
    if (t.placed < t.placeAtX.size()) {
        // One SP a frame: a press waits for the frame, and two in one would be the same SP.
        if (!t.placeQueued && pl->m_player1->m_position.x >= t.placeAtX[t.placed]) {
            t.placeQueued = true;
            if (++t.placed == t.placeAtX.size()) t.stepsLeft = TAIL_STEPS + 20;
            Loader::get()->queueInMainThread([] {
                g_smoke.placeQueued = false;
                // A script places SPs through the key, as a player does.
                if (g_smoke.scripted) pressKey(KEY_V);
                else placeSP();
            });
        }
        return;
    }
    if (t.stepsLeft > 0 && --t.stepsLeft == 0) Loader::get()->queueInMainThread(t.scripted ? stepsDone : nextSmokePhase);
}

}

namespace smoke {

bool takesOver() {
    return g_smokeTakeover;
}

bool keepsPlaying() {
    return g_smoke.active && !g_smoke.pausing;
}

void noticed(std::string const& text) {
    g_lastNotice = text;
}

void beforeStep(PlayLayer* pl) {
    if (g_smoke.active) smokeStep(pl);
}

void loaded(PlayLayer* pl, storage::Loaded const& loaded) {
    if (!g_smoke.active) return;
    restoreSmokeUpdate();
    if (loaded.movedOver) {
        log::info("Smoke test: SPs moved over to this version: {}, not: {} ({})", loaded.moved, loaded.notMoved.size(),
            text::percents(loaded.notMoved));
    }
    if (g_smoke.probe) smokeProbe(pl);
}

void ready(PlayLayer* pl) {
    auto& t = g_smoke;
    if (!t.active) return;
    if (t.scripted) {
        if (t.steer) {
            // Mode, gravity, size, dual, mirror, speed portals and teleports.
            static std::set<int> const ids = {12, 13, 47, 111, 660, 745, 1331, 1933, 10, 11, 2926, 99, 101, 286, 287, 45, 46,
                200, 201, 202, 203, 1334, 747};
            t.portals.clear();
            for (auto obj : CCArrayExt<GameObject*>(pl->m_objects)) {
                if (ids.contains(obj->m_objectID)) t.portals.push_back(obj->m_startPosition);
            }
            std::sort(t.portals.begin(), t.portals.end(), [](CCPoint const& a, CCPoint const& b) { return a.x < b.x; });
            log::info("Smoke test: steering into {} portals", t.portals.size());
        }
        // The first step a second in, or the next one once in the level again.
        bool again = std::exchange(t.reentering, false);
        if (!t.started || again) {
            t.started = true;
            pl->runAction(CCSequence::create(CCDelayTime::create(1.f), CallFuncExt::create([] { nextAction(); }), nullptr));
        }
        return;
    }
    if (!t.importFrom.empty()) smokeImport();
    if (t.moveVersion) smokeMoveVersion();
    if (t.checkSaved) smokeCheckSaved();
    if (t.storageUI) pl->runAction(CCSequence::create(CCDelayTime::create(1.f), CallFuncExt::create([] { smokeStorage(0); }), nullptr));
    if (t.quit) pl->runAction(CCSequence::create(CCDelayTime::create(1.f), CallFuncExt::create([] { smokeQuit(); }), nullptr));
}

void restarted(PlayLayer* pl) {
    if (g_smoke.active) g_smoke.refIndex = -1;  // the bot finds its place in the run again
    if (g_smoke.externalOn) logRestart(pl, "restart");
}

bool hit(PlayLayer* pl, PlayerObject* player, GameObject* object) {
    auto& t = g_smoke;
    if (t.dying) {
        // A real death: see died().
        t.dying = false;
        t.died = true;
        return false;
    }
    if (!t.active) return false;
    // Noclip; the first hits of each attempt go to the log.
    if (g_rec.hits <= 3) {
        log::info("Smoke test: hit object {} at {:.1f},{:.1f}, player at {:.1f},{:.1f}", object ? object->m_objectID : 0,
            object ? object->getPositionX() : 0.f, object ? object->getPositionY() : 0.f, player->m_position.x,
            player->m_position.y);
    }
    return true;
}

void died(PlayLayer* pl) {
    if (!std::exchange(g_smoke.died, false)) return;
    log::info("Smoke test: died from SP at {:.2f}%: best now {}%, level attempts {}, test mode {}", pl->getCurrentPercent(),
        pl->m_level->m_normalPercent.value(), pl->m_level->m_attempts.value(), pl->m_isTestMode);
    Loader::get()->queueInMainThread(nextSmokePhase);
}

void quitting() {
    if (!g_smoke.active) return;
    if (g_smoke.reentering) {
        log::info("Smoke test: out of the level, to enter one again");
        return;
    }
    // Left midway: its noclip and the rest end here.
    log::warn("Smoke test: the level was quit, test stopped");
    restoreSmokeLevel();
    restoreSmokeSettings();
    g_smoke = SmokeTest{};
    g_smokeTakeover = false;
}

}

// Minimized, the game stops its main loop: a smoke test (waiting to start or running) goes on in the
// background instead, so the window can stay minimized while it runs.
class $modify(SPCAppDelegate, AppDelegate) {
    void applicationDidEnterBackground() {
        std::error_code ec;
        if (g_smoke.active || std::filesystem::exists(smokeTestPath(""), ec)) {
            log::info("Smoke test: the window went to the background, the game goes on");
            return;
        }
        AppDelegate::applicationDidEnterBackground();
    }
};

class $modify(SPCMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        Loader::get()->queueInMainThread(startSmokeTest);
        return true;
    }
};
