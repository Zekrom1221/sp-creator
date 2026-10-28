#include <Geode/Geode.hpp>
#include <Geode/modify/AppDelegate.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/utils/file.hpp>

#include "Core.hpp"
#include "SPList.hpp"
#include "StartPoint.hpp"
#include "Storage.hpp"
#include "Text.hpp"
#include "Transfer.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <deque>
#include <memory>

using namespace geode::prelude;
using text::Msg;

// SP Creator: lets the player place their own startpos (SP) in any online level while playing.
//
// - While the player plays, the mod keeps the last couple of seconds of the run: the player's state
//   before every physics step, the button presses, and a hidden checkpoint every few steps (the same
//   CheckpointObject practice mode uses: camera, moved objects, triggers, music).
// - The "place SP" key turns the checkpoint taken about 0.6 s earlier into the SP's spawn, and the
//   recorded steps since then into its auto part.
// - Every SP has a startpos object in the level, added after the level's own objects at the point
//   where the player takes control. Other mods that look for startposes find the SPs there: startpos
//   switchers list them, Blitzkrieg makes stages of them.
// - Starting from an SP goes through the game's own startpos retry path: with a startpos set and
//   PlayLayer::m_currentCheckpoint filled, resetLevel loads that checkpoint in normal mode. With an SP's
//   object as the startpos, set by this mod or by another mod's switcher, the mod puts the SP's
//   checkpoint there, then replays the auto part and hands control to the player.
// - Test mode is on for these attempts, so like any startpos attempt they count as attempts but never
//   save a new best or a completion (destroyPlayer and levelComplete skip progress in test mode).

namespace {

constexpr int STEPS_PER_SECOND = 240;
// Steps of the original run kept after placing an SP, to compare the handoff with it.
constexpr int TAIL_STEPS = 60;
// How often the cost of the hidden checkpoints goes to the log, in steps.
constexpr int STATS_EVERY = 5 * STEPS_PER_SECOND;

int g_probeGroup = 0;  // smoke test: group whose first object Frame::probeY follows

// A hidden checkpoint taken before `step` of the current attempt.
struct Snapshot {
    int step = 0;
    Ref<CheckpointObject> cp;
    Holds holds{};          // buttons held at that moment
    uint64_t inputSeq = 0;  // inputs from this one on came after the checkpoint
};

// The current attempt, as played.
struct Recording {
    int step = 0;              // physics steps played in this attempt
    std::deque<Frame> frames;  // frames[i]: state before step firstStep + i
    int firstStep = 0;
    std::deque<Input> inputs;
    uint64_t inputSeq = 0;
    std::deque<Snapshot> snapshots;
    Holds holds{};
    std::shared_ptr<StartPoint> tailFor;  // SP placed in this attempt that still collects its tail
    int hits = 0;
};

// The auto part of the attempt, if it started from an SP.
struct Playback {
    std::shared_ptr<StartPoint> sp;
    int step = 0;
    size_t nextInput = 0;
    bool controlling = false;  // the auto part owns the buttons until the handoff
    bool injecting = false;
    Holds gameHolds{};  // given to the game
    Holds userHolds{};  // what the player physically holds meanwhile
    // Largest difference from the original run: in the auto part before guiding, then after the handoff.
    float autoX = 0, autoY = 0, autoCam = 0;
    float tailX = 0, tailY = 0, tailCam = 0;
    int traced = 0;  // differing steps written to the log
};

// The open level's SPs (Storage.cpp keeps them between sessions).
struct Session {
    std::vector<std::shared_ptr<StartPoint>> sps;  // sorted by x
    std::shared_ptr<StartPoint> active;            // null: the level's own start
    // `active` was picked by this mod (placing, switching, deleting) since the last restart, and wins at
    // the next one, unless another mod sets a startpos in between.
    // Another mod's switcher changes the startpos, and nulls m_currentCheckpoint (capeling's does): both are
    // kept to see that. (setStartPosObject is inlined on Windows: there's no call to catch.)
    bool chosen = false;
    bool chosenNow = false;                        // for a restart right away: nobody can come in between
    StartPosObject* startPosWhenChosen = nullptr;
    CheckpointObject* checkpointWhenChosen = nullptr;  // only compared
    std::shared_ptr<StartPoint> spawning;          // set during a restart from an SP
    Ref<CheckpointObject> lastCheckpoint;          // the SP checkpoint the mod last put into the layer
    unsigned int lastCheckpointRetains = 0;        // its retain count once the attempt started
    bool ownTestMode = false;                      // test mode is on for an attempt from an SP
    // Objects of deleted SPs, out of the level: another mod's switcher may still have one and pick it.
    std::vector<Ref<StartPosObject>> retired;
    StartPosObject* levelStartPos = nullptr;       // the startpos the level itself picked (owned by the level)
    bool levelTestMode = false;
    int nextId = 1;
    bool creatingObjects = false;                  // the level has started creating its objects
    // Placed SPs not written yet: writing one takes ~10 ms, so it waits for a moment without gameplay
    // (restart, pause, quit) instead of stuttering right where the player pressed the key.
    std::vector<std::shared_ptr<StartPoint>> unsaved;
};

struct Stats {
    int steps = 0;
    int checkpoints = 0;
    double checkpointMs = 0, maxCheckpointMs = 0;
    int frames = 0;
    double frameMs = 0, maxFrameMs = 0;
};

Session g_session;
Recording g_rec;
Playback g_play;
Stats g_stats;

float autoPartSeconds() {
    return static_cast<float>(Mod::get()->getSettingValue<double>("auto-part-length"));
}

int checkpointInterval() {
    return static_cast<int>(Mod::get()->getSettingValue<int64_t>("checkpoint-interval"));
}

bool g_smokeTakeover = false;  // smoke test: a press ends the auto part whatever the setting says

bool blockInput() {
    return !g_smokeTakeover && Mod::get()->getSettingValue<bool>("block-input");
}

constexpr float NOTICE_SECONDS = 1.5f;

// The mod keeps one notification: Geode queues notifications and shows each for its full time, so
// switching quickly through SPs would show every SP passed on the way, one after another.
Ref<Notification> g_notice;
std::chrono::steady_clock::time_point g_noticeAt;
unsigned int g_noticeFrame = 0;  // frame it was shown in

// Notification has its label in bigFont, which has no Cyrillic letters: text that needs the mod's font
// goes into a label of the mod's beside it, with the notification's own one left empty.
void setNoticeText(Notification* n, std::string const& text) {
    constexpr auto OWN_LABEL = "text"_spr;
    auto own = findFirstChildRecursive<CCLabelBMFont>(n, [&](CCLabelBMFont* l) { return l->getID() == OWN_LABEL; });
    auto font = text::fontFor(text);
    if (std::string_view(font) == "bigFont.fnt") {
        if (own) own->removeFromParent();
        n->setString(text);
        return;
    }
    if (!own) {
        auto label = findFirstChildRecursive<CCLabelBMFont>(n, [](auto) { return true; });
        if (!label || !label->getParent()) return n->setString(text);
        own = CCLabelBMFont::create("", font);
        own->setID(OWN_LABEL);
        own->setScale(label->getScale());
        // After the icon, which setIcon adds at the end.
        label->getParent()->addChild(own, 1);
    }
    own->setString(text.c_str());
    n->setString("");  // lays the notification out again, around the mod's label
}

void notify(std::string const& text, NotificationIcon icon = NotificationIcon::Info) {
    log::info("Notice: {}", text);
    auto now = std::chrono::steady_clock::now();
    // On screen and not fading out yet: change it in place, which also restarts its timer.
    if (g_notice && g_notice->isShowing() && now - g_noticeAt < std::chrono::milliseconds(1200)) {
        setNoticeText(g_notice, text);
        g_notice->setIcon(icon);
        // Not in the frame it was shown in: setTime stops its actions, and its fade-in starts a frame later.
        if (CCDirector::get()->getTotalFrames() != g_noticeFrame) g_notice->setTime(NOTICE_SECONDS);
        g_noticeAt = now;
        return;
    }
    // Still waiting in the queue: drop it; fading out: let it go.
    if (g_notice) g_notice->cancel();
    g_notice = Notification::create(text, icon, NOTICE_SECONDS);
    setNoticeText(g_notice, text);
    g_notice->show();
    g_noticeAt = now;
    g_noticeFrame = CCDirector::get()->getTotalFrames();
}

template <class... Args>
void notify(NotificationIcon icon, Msg msg, Args&&... args) {
    notify(text::format(msg, std::forward<Args>(args)...), icon);
}

PlayerFrame capturePlayer(PlayerObject* p) {
    return {p->m_position.y, p->m_yVelocity, p->getRotation()};
}

Frame captureFrame(PlayLayer* pl) {
    auto const& gs = pl->m_gameState;
    Frame f;
    f.x = pl->m_player1->m_position.x;
    f.p1 = capturePlayer(pl->m_player1);
    f.dual = gs.m_isDualMode && pl->m_player2;
    if (f.dual) f.p2 = capturePlayer(pl->m_player2);
    f.camX = gs.m_cameraPosition.x;
    f.camY = gs.m_cameraPosition.y;
    f.speed = pl->m_player1->m_playerSpeed;
    f.time = gs.m_levelTime;
    if (g_probeGroup > 0) {
        auto objs = pl->getGroup(g_probeGroup);
        if (objs && objs->count() > 0) f.probeY = static_cast<GameObject*>(objs->objectAtIndex(0))->m_positionY;
    }
    f.hits = g_rec.hits;
    return f;
}

std::string describe(Frame const& f) {
    return fmt::format("x {:.3f} y {:.3f} yv {:.4f} cam {:.2f},{:.2f} speed {:.3f} time {:.4f} probe {:.3f}",
        f.x, f.p1.y, f.p1.yVel, f.camX, f.camY, f.speed, f.time, f.probeY);
}

bool differs(Frame const& a, Frame const& b) {
    return std::abs(a.x - b.x) > 0.01f || std::abs(a.p1.y - b.p1.y) > 0.01f ||
        std::abs(a.camX - b.camX) > 0.01f || std::abs(a.camY - b.camY) > 0.01f || std::abs(a.probeY - b.probeY) > 0.01;
}

void forcePlayer(PlayerObject* p, PlayerFrame const& f) {
    p->m_position.y = f.y;
    p->setPositionY(f.y);
    p->m_yVelocity = f.yVel;
    p->setRotation(f.rot);
}

// Compares with refs[i], or a step either side of it: PlayerObject::m_position sometimes shows the
// previous step's position for a step (in the recording or now), without the run being off.
void trackDiff(Frame const& now, std::vector<Frame> const& refs, size_t i, float& x, float& y, float& cam) {
    float bestX = 0, bestY = 0, bestCam = 0, best = INFINITY;
    for (size_t j = i > 0 ? i - 1 : 0; j <= i + 1 && j < refs.size(); j++) {
        auto const& ref = refs[j];
        float dx = std::abs(now.x - ref.x), dy = std::abs(now.p1.y - ref.p1.y);
        float dc = std::max(std::abs(now.camX - ref.camX), std::abs(now.camY - ref.camY));
        if (std::max(dx, dy) < best) {
            best = std::max(dx, dy);
            bestX = dx;
            bestY = dy;
            bestCam = dc;
        }
    }
    x = std::max(x, bestX);
    y = std::max(y, bestY);
    cam = std::max(cam, bestCam);
}

// m_currentCheckpoint is retained by the layer (startMusic retains the one it creates).
void setCurrentCheckpoint(PlayLayer* pl, CheckpointObject* cp) {
    if (pl->m_currentCheckpoint == cp) return;
    CC_SAFE_RETAIN(cp);
    CC_SAFE_RELEASE(pl->m_currentCheckpoint);
    pl->m_currentCheckpoint = cp;
}

// The SP whose object `obj` is, if it's one.
std::shared_ptr<StartPoint> spOf(StartPosObject* obj) {
    if (!obj) return nullptr;
    for (auto const& sp : g_session.sps) {
        if (sp->object == obj) return sp;
    }
    return nullptr;
}

bool isRetired(StartPosObject* obj) {
    auto const& r = g_session.retired;
    return obj && std::find(r.begin(), r.end(), obj) != r.end();
}

// Where the SP's startpos object stands: where the player takes control.
CCPoint objectPosition(StartPoint const& sp) {
    return {sp.x, sp.frames.back().p1.y};
}

// Restarts move objects back to where they started, which for the SPs' objects (added by the mod, not
// loaded with the level) is 0,0 in some levels (Jumper). setStartPos would fix that, but then the player
// crashes into the object (BPATA): they're put back in place after each restart instead.
void placeObjects() {
    for (auto const& sp : g_session.sps) {
        if (sp->object) sp->object->setPosition(objectPosition(*sp));
    }
}

// Adds the SP's startpos object to the level, where the player takes control. Going through addObject
// lets other mods' addObject hooks (startpos switchers) collect it like the level's startposes.
void addObjectFor(PlayLayer* pl, StartPoint& sp) {
    // Made the way the level's own startposes are (object 31): StartPosObject::create() alone leaves its
    // object ID at 0, which switchers skip.
    auto obj = typeinfo_cast<StartPosObject*>(GameObject::createWithKey(31));
    if (!obj || !obj->m_startSettings) {
        log::error("SP {}: no startpos object", sp.id);
        return;
    }
    obj->setPosition(objectPosition(sp));
    auto startPos = pl->m_startPosObject;
    bool testMode = pl->m_isTestMode;
    pl->addObject(obj);
    // The game takes an added startpos for the level's start when it's further on: keep the one it has.
    if (pl->m_startPosObject != startPos) pl->setStartPosObject(startPos);
    pl->m_isTestMode = testMode;
    storage::numberObject(pl, obj);
    sp.object = obj;
}

// Takes a deleted SP's object out of the level.
void retireObject(PlayLayer* pl, StartPoint& sp) {
    if (!sp.object) return;
    pl->removeObjectFromSection(sp.object);
    g_session.retired.push_back(sp.object);
    pl->m_objects->removeObject(sp.object);
    // The SPs' objects after it moved up a place.
    for (auto const& other : g_session.sps) {
        if (other->object) storage::numberObject(pl, other->object);
    }
}

// Restarts go to `sp` (null: the level's own start) from the next one on. `now`: the restart follows
// right away.
void choose(std::shared_ptr<StartPoint> sp, bool now = false) {
    auto& s = g_session;
    s.active = std::move(sp);
    s.chosen = true;
    s.chosenNow = now;
    auto pl = PlayLayer::get();
    s.startPosWhenChosen = pl ? pl->m_startPosObject : nullptr;
    s.checkpointWhenChosen = pl ? pl->m_currentCheckpoint : nullptr;
}

// Before resetLevel. Whoever set the startpos last decides where the attempt starts: this mod, or
// another mod's switcher, which may pick an SP's object too. Then the game's startpos retry path gets
// the SP's checkpoint, or the level its own start back.
void applyActiveSP(PlayLayer* pl) {
    auto& s = g_session;
    bool foreign = pl->m_startPosObject != s.startPosWhenChosen || pl->m_currentCheckpoint != s.checkpointWhenChosen;
    if (!s.chosen || (foreign && !s.chosenNow)) s.active = spOf(pl->m_startPosObject);
    s.chosen = s.chosenNow = false;
    // A switcher that nulls m_currentCheckpoint without releasing it (capeling's) leaves the layer's retain on
    // the SP's checkpoint, which would then never be freed: give it back, if nobody did.
    if (s.lastCheckpoint && pl->m_currentCheckpoint != s.lastCheckpoint &&
        s.lastCheckpoint->retainCount() == s.lastCheckpointRetains) {
        log::info("SP checkpoint dropped by another mod without a release: released");
        s.lastCheckpoint->release();
    }
    s.spawning = nullptr;
    if (s.active && s.active->object) {
        pl->setStartPosObject(s.active->object);
        setCurrentCheckpoint(pl, s.active->cp);
        s.lastCheckpoint = s.active->cp;
        pl->m_isTestMode = true;
        s.ownTestMode = true;
        s.spawning = s.active;
        return;
    }
    s.active = nullptr;
    // Off the SPs (also when a switcher picks a deleted SP's object): the level's own start.
    bool retired = isRetired(pl->m_startPosObject);
    if (spOf(pl->m_startPosObject) || retired) pl->setStartPosObject(s.levelStartPos);
    // Test mode was on for the SP (or the switcher that picked the deleted SP turned it on).
    if (s.ownTestMode || retired) {
        if (pl->m_startPosObject == s.levelStartPos) pl->m_isTestMode = s.levelTestMode;
        else if (!pl->m_startPosObject) pl->m_isTestMode = false;
    }
    s.ownTestMode = false;
    // An SP's checkpoint never loads at another startpos: the game makes its own in startMusic.
    if (s.lastCheckpoint && pl->m_currentCheckpoint == s.lastCheckpoint) setCurrentCheckpoint(pl, nullptr);
    s.lastCheckpoint = nullptr;
}

// After resetLevel: the attempt spawned from the SP (and not from a practice checkpoint).
void startPlayback(PlayLayer* pl) {
    auto sp = std::exchange(g_session.spawning, nullptr);
    if (!sp || pl->m_startPosObject != sp->object || pl->m_currentCheckpoint != sp->cp) return;
    if (pl->m_checkpointArray->count() > 0) return;
    g_play.sp = sp;
    g_play.controlling = true;
    log::info("SP {}: spawned, practice {}, test mode {}, level attempts {}, best {}%", sp->id,
        pl->m_isPracticeMode, pl->m_isTestMode, pl->m_level->m_attempts.value(), pl->m_level->m_normalPercent.value());
}

// The percent another mod computes for a startpos at x (Blitzkrieg's stages): by position, or by time
// in levels whose percent goes by time.
float startPosPercent(PlayLayer* pl, float x) {
    float percent;
    if (pl->m_level->m_timestamp > 0) {
        float length = pl->timeForPos({pl->m_levelLength, 0.f}, 0, 0, true, 0);
        percent = length > 0 ? pl->timeForPos({x, 0.f}, 0, 0, true, 0) / length * 100.f : 0.f;
    }
    else {
        percent = pl->m_levelLength > 0 ? x / pl->m_levelLength * 100.f : 0.f;
    }
    return std::clamp(percent, 0.f, 100.f);
}

void inject(GJBaseGameLayer* layer, int index, bool down) {
    bool p1 = index <= 3;
    int button = p1 ? index : index - 3;
    g_play.injecting = true;
    layer->handleButton(down, button, p1);
    g_play.injecting = false;
    g_play.gameHolds[index] = down;
}

// Hands the buttons to the player, as they actually hold them, through `press` (inject by default).
// `early`: they pressed in the auto part with control not blocked, and play on from here.
void endAutoPart(PlayLayer* pl, bool early, geode::Function<void(int index, bool down)> press = {}) {
    auto& pb = g_play;
    auto const& sp = *pb.sp;
    pb.controlling = false;
    std::string changed;
    for (int i = 1; i < 7; i++) {
        if (pb.userHolds[i] == pb.gameHolds[i]) continue;
        changed += fmt::format(" {}{}", pb.userHolds[i] ? "D" : "U", i);
        if (press) press(i, pb.userHolds[i]);
        else inject(pl, i, pb.userHolds[i]);
    }
    if (early) {
        log::info("SP {}: the player took over at step {} of {}, buttons changed:{}", sp.id, pb.step, sp.handoff(),
            changed.empty() ? " none" : changed);
        // Nothing left to compare with the original run.
        pb = Playback{};
        return;
    }
    int originalHits = (sp.tail.empty() ? sp.frames.back().hits : sp.tail.front().hits) - sp.frames.front().hits;
    log::info("SP {}: handoff after {} steps; largest drift before guiding: x {:.3f}, y {:.3f}, camera {:.3f}; "
        "hits in the auto part {} (original run {})", sp.id, sp.handoff(), pb.autoX, pb.autoY, pb.autoCam,
        g_rec.hits, originalHits);
}

// Before a step of an attempt that started from an SP.
void playStep(PlayLayer* pl) {
    auto& pb = g_play;
    auto const& sp = *pb.sp;
    int k = pb.step++;
    if (k == 0) {
        // Loading the checkpoint puts the camera on its target instead of where it was (up to 15 units
        // off in BPATA), and the next frames would ease back from there: start where the run had it.
        auto const& f = sp.frames.front();
        auto& gs = pl->m_gameState;
        log::info("SP {}: camera after loading off by {:.2f},{:.2f}", sp.id, gs.m_cameraPosition.x - f.camX,
            gs.m_cameraPosition.y - f.camY);
        gs.m_cameraPosition = gs.m_cameraLastPosition = CCPoint(f.camX, f.camY);
    }
    auto now = captureFrame(pl);
    // Debug: the first differing steps, then every 8th.
    auto trace = [&](Frame const& ref) {
        if (!differs(now, ref) || (pb.traced >= 6 && k % 8 != 0) || pb.traced >= 40) return;
        pb.traced++;
        log::info("SP {} step {}: now {} | rec {}", sp.id, k, describe(now), describe(ref));
    };
    if (pb.controlling && k < sp.handoff()) {
        auto const& f = sp.frames[k];
        trackDiff(now, sp.frames, k, pb.autoX, pb.autoY, pb.autoCam);
        trace(f);
        if (k == 0) {
            for (int i = 1; i < 7; i++) {
                if (sp.holds[i]) inject(pl, i, true);
            }
        }
        if (Mod::get()->getSettingValue<bool>("guide-player")) {
            forcePlayer(pl->m_player1, f.p1);
            if (f.dual && pl->m_player2) forcePlayer(pl->m_player2, f.p2);
        }
        while (pb.nextInput < sp.inputs.size() && sp.inputs[pb.nextInput].step <= k) {
            auto const& in = sp.inputs[pb.nextInput++];
            inject(pl, holdIndex(in.button, in.p1), in.down);
        }
        return;
    }
    if (pb.controlling) endAutoPart(pl, false);
    size_t t = k - sp.handoff();
    if (t < sp.tail.size()) {
        trackDiff(now, sp.tail, t, pb.tailX, pb.tailY, pb.tailCam);
        trace(sp.tail[t]);
        return;
    }
    log::info("SP {}: {} steps after the handoff, largest difference from the original run: x {:.3f}, y {:.3f}, camera {:.3f}",
        sp.id, sp.tail.size(), pb.tailX, pb.tailY, pb.tailCam);
    pb = Playback{};
}

void takeSnapshot(PlayLayer* pl) {
    auto start = std::chrono::steady_clock::now();
    auto cp = pl->createCheckpoint();
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    g_stats.checkpoints++;
    g_stats.checkpointMs += ms;
    g_stats.maxCheckpointMs = std::max(g_stats.maxCheckpointMs, ms);
    if (cp) g_rec.snapshots.push_back({g_rec.step, cp, g_rec.holds, g_rec.inputSeq});
}

void logStats(PlayLayer* pl) {
    auto& st = g_stats;
    if (++st.steps < STATS_EVERY) return;
    log::info("Hidden checkpoints: {} in {} steps, avg {:.2f} ms, max {:.2f} ms | frames: {}, avg {:.2f} ms, max {:.2f} ms | {} objects",
        st.checkpoints, st.steps, st.checkpoints ? st.checkpointMs / st.checkpoints : 0.0, st.maxCheckpointMs,
        st.frames, st.frames ? st.frameMs / st.frames : 0.0, st.maxFrameMs, pl->m_objects->count());
    st = Stats{};
}

// Before a step: record the state the step starts from, and keep only what an SP placed now could need.
void recordStep(PlayLayer* pl) {
    auto& r = g_rec;
    auto f = captureFrame(pl);
    if (r.tailFor) {
        r.tailFor->tail.push_back(f);
        if (r.tailFor->tail.size() >= TAIL_STEPS) r.tailFor = nullptr;
    }
    if (r.frames.empty()) r.firstStep = r.step;
    r.frames.push_back(f);
    int interval = checkpointInterval();
    if (r.step % interval == 0) takeSnapshot(pl);
    size_t keep = static_cast<size_t>(std::ceil(autoPartSeconds() * STEPS_PER_SECOND)) + interval + 2;
    while (r.frames.size() > keep) {
        r.frames.pop_front();
        r.firstStep++;
    }
    while (!r.inputs.empty() && r.inputs.front().step < r.firstStep) r.inputs.pop_front();
    while (!r.snapshots.empty() && r.snapshots.front().step < r.firstStep) r.snapshots.pop_front();
}

// Position of an SP in g_session.sps, -1 for the level's own start.
int indexOf(std::shared_ptr<StartPoint> const& sp) {
    auto const& sps = g_session.sps;
    auto it = std::find(sps.begin(), sps.end(), sp);
    return it == sps.end() ? -1 : static_cast<int>(it - sps.begin());
}

int activeIndex() {
    return indexOf(g_session.active);
}

// "SP 2/5 (4.8%)": SP ids skip numbers once SPs get deleted, so players see positions.
std::string labelOf(std::shared_ptr<StartPoint> const& sp) {
    int i = indexOf(sp);
    if (i < 0) return std::string(text::tr(Msg::LevelStart));
    int n = static_cast<int>(g_session.sps.size());
    if (sp->name.empty()) return text::format(Msg::SPLabel, i + 1, n, sp->percent);
    return text::format(Msg::SPLabelNamed, sp->name, i + 1, n, sp->percent);
}

std::string activeLabel() {
    return labelOf(g_session.active);
}

bool canUseKeys(PlayLayer* pl) {
    return pl && pl->m_player1 && !pl->m_isPaused && !pl->m_hasCompletedLevel && !pl->m_isPlatformer;
}

bool placeSP() {
    auto pl = PlayLayer::get();
    if (!canUseKeys(pl) || pl->m_player1->m_isDead) return false;
    if (g_play.sp && g_play.controlling) {
        notify(NotificationIcon::Warning, Msg::WaitAutoPart);
        return true;
    }
    auto& r = g_rec;
    int now = r.step;  // the player is in control from this step on
    int autoSteps = static_cast<int>(std::lround(autoPartSeconds() * STEPS_PER_SECOND));
    Snapshot const* snap = nullptr;
    for (auto it = r.snapshots.rbegin(); it != r.snapshots.rend(); ++it) {
        if (it->step <= now - autoSteps && it->step >= r.firstStep) {
            snap = &*it;
            break;
        }
    }
    if (!snap) {
        notify(NotificationIcon::Warning, Msg::TooEarly, autoPartSeconds());
        return true;
    }
    auto sp = std::make_shared<StartPoint>();
    sp->id = g_session.nextId++;
    sp->cp = snap->cp;
    sp->holds = snap->holds;
    sp->spawnStep = snap->step;
    for (int s = snap->step; s < now; s++) sp->frames.push_back(r.frames[s - r.firstStep]);
    for (auto const& in : r.inputs) {
        if (in.seq < snap->inputSeq || in.step >= now) continue;
        auto rel = in;
        rel.step -= snap->step;
        sp->inputs.push_back(rel);
    }
    sp->x = pl->m_player1->m_position.x;
    sp->percent = pl->getCurrentPercent();

    auto& sps = g_session.sps;
    auto at = std::upper_bound(sps.begin(), sps.end(), sp->x, [](float x, auto const& o) { return x < o->x; });
    sps.insert(at, sp);
    addObjectFor(pl, *sp);
    choose(sp);
    r.tailFor = sp;
    g_session.unsaved.push_back(sp);
    notify(NotificationIcon::Success, Msg::Placed, activeLabel());
    log::info("SP {} placed at x {:.1f} ({:.2f}%): spawn from step {}, auto part {} steps, {} inputs, practice {}",
        sp->id, sp->x, sp->percent, snap->step, sp->handoff(), sp->inputs.size(), pl->m_isPracticeMode);
    if (auto pc = sp->cp->m_player1Checkpoint) {
        log::info("SP {}: spawn frame {} | checkpoint player at {:.3f},{:.3f}", sp->id, describe(sp->frames.front()),
            pc->m_position.x, pc->m_position.y);
    }
    return true;
}

void saveUnsaved() {
    for (auto const& sp : g_session.unsaved) storage::save(*sp);
    g_session.unsaved.clear();
}

// Restart from the active SP, as startpos switchers do.
// Restart from `sp` (null: the level start), as startpos switchers do.
void restartFrom(PlayLayer* pl, std::shared_ptr<StartPoint> sp) {
    // Practice checkpoints would win over the startpos in resetLevel.
    if (pl->m_isPracticeMode) pl->removeAllCheckpoints();
    choose(std::move(sp), true);
    pl->resetLevel();
}

// dir -1 / +1 through: level start, SPs by position.
// False when the key isn't for the mod right now (paused, say), so it goes on to the game.
bool switchSP(int dir) {
    auto pl = PlayLayer::get();
    if (!canUseKeys(pl)) return false;
    auto& s = g_session;
    if (s.sps.empty()) {
        notify(NotificationIcon::Warning, Msg::NoSPs);
        return true;
    }
    int n = static_cast<int>(s.sps.size());
    int i = (activeIndex() + 1 + dir + n + 1) % (n + 1) - 1;
    auto sp = i < 0 ? nullptr : s.sps[i];
    notify(labelOf(sp));
    restartFrom(pl, sp);
    return true;
}

// Takes an SP out of the level, its file left as it is; restarts that went to it go to the one before it
// (or the level start).
void dropSP(PlayLayer* pl, std::shared_ptr<StartPoint> const& sp) {
    auto& s = g_session;
    int i = indexOf(sp);
    if (i < 0) return;
    if (s.active == sp) choose(i > 0 ? s.sps[i - 1] : nullptr);
    s.sps.erase(s.sps.begin() + i);
    retireObject(pl, *sp);
    if (g_rec.tailFor == sp) g_rec.tailFor = nullptr;
    std::erase(s.unsaved, sp);
}

// Deletes an SP; restarts that went to it go to the one before it (or the level start).
void removeSP(PlayLayer* pl, std::shared_ptr<StartPoint> const& sp) {
    auto& s = g_session;
    if (indexOf(sp) < 0) return;
    dropSP(pl, sp);
    storage::remove(*sp);
    if (!sp->name.empty()) storage::saveNames(s.sps);
    log::info("SP {} deleted", sp->id);
}

// Deletes the SP the player starts from, and restarts from the one before it (or the level start).
bool deleteSP() {
    auto pl = PlayLayer::get();
    if (!canUseKeys(pl)) return false;
    auto sp = g_session.active;
    if (!sp) {
        notify(NotificationIcon::Warning, Msg::NoSPSelected);
        return true;
    }
    removeSP(pl, sp);
    notify(NotificationIcon::Success, Msg::Deleted, sp->percent);
    restartFrom(pl, g_session.active);
    return true;
}

// Smoke test, for development: with <mod save dir>/smoketest.json present at the main menu, the mod
// opens the level, plays it with noclip, places SPs when the player passes the given x, starts from
// each of them, goes back to the level start and closes the game without saving. The file is renamed
// before the test starts, so a crash doesn't repeat it.
// The player follows a reference run (a Trajectory Logger CSV: state and inputs per step, matched by x)
// when one is given, and jumps blindly every half second otherwise.
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
    // The level's Low Detail Mode as it was, given back at the end.
    Ref<GJGameLevel> level;
    std::optional<bool> lowDetailWas;
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

SmokeTest g_smoke;

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
    g_probeGroup = static_cast<int>(root["probe_group"].asInt().unwrapOr(0));
    auto ref = root["reference"].asString();
    if (ref.isOk()) g_smoke.reference = loadReference(ref.unwrap());
    // Its SPs go to a folder of their own; without SPs to place it tests the ones saved there.
    storage::setRoot(root["storage_root"].asString().unwrapOr("levels-smoketest"));
    g_smoke.cleanup = root["cleanup"].asBool().unwrapOr(false);
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
    if (g_smoke.placeAtX.empty()) g_smoke.stepsLeft = 60;
    log::info("Smoke test: level {} \"{}\" ({} stars), {} SPs to place, reference of {} rows, cleanup {}, Russian {}", id,
        level->m_levelName, level->m_stars.value(), g_smoke.placeAtX.size(), g_smoke.reference.size(), g_smoke.cleanup,
        text::russian());
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
    auto& t = g_smoke;
    if (t.level && t.lowDetailWas) t.level->m_lowDetailModeToggled = *t.lowDetailWas;
    t.lowDetailWas.reset();
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
        log::info("Smoke test: import of {}: {} SPs, {} already here, {} near one, {} damaged", what, items.size(),
            count([](auto const& i) { return i.duplicate; }), count([](auto const& i) { return i.nearby > 0; }),
            pack.unwrap().damaged);
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
    // As a file of other SPs: none of the level's are in it, so all of them go aside.
    for (auto& item : pack->items) item.duplicate = false;
    pack->inFile.clear();
    auto result = transfer::take(*pack, std::vector<bool>(pack->items.size(), true), true);
    log::info("Smoke test: replaced by the import: {} added, {} failed, {} not set aside, SPs now {}", result.added,
        result.failed, result.notMoved, ids());
    // The last one deleted, the file again: replacing keeps the two the file holds and adds the third.
    if (g_session.sps.size() == 3) {
        removeSP(PlayLayer::get(), g_session.sps.back());
        auto again = transfer::open(path);
        if (again.isErr()) return log::error("Smoke test: the export refused: {}", again.unwrapErr());
        std::vector<bool> picked;
        for (auto const& item : again.unwrap().items) picked.push_back(!item.duplicate);
        auto kept = transfer::take(again.unwrap(), picked, true);
        log::info("Smoke test: replace with 2 of 3 here: {} in the file kept, {} added, {} not set aside, SPs now {}",
            again.unwrap().inFile.size(), kept.added, kept.notMoved, ids());
    }
}

// At the level start: SPs from a file, as on another computer.
void smokeImport() {
    auto path = Mod::get()->getSaveDir() / g_smoke.importFrom;
    auto pack = transfer::open(path);
    if (pack.isErr()) return log::error("Smoke test: import of {} refused: {}", path, pack.unwrapErr());
    std::vector<bool> picked;
    for (auto const& item : pack.unwrap().items) picked.push_back(!item.duplicate);
    auto result = transfer::take(pack.unwrap(), picked, false);
    log::info("Smoke test: imported from {}: {} added, {} failed, {} SPs now", path, result.added, result.failed,
        g_session.sps.size());
}

void finishSmokeTest() {
    saveUnsaved();
    restoreSmokeLevel();
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
        for (auto label : labels) {
            auto size = label->getScaledContentSize();
            log::info("Smoke test: notice label \"{}\" font {} visible {} opacity {} displayed {} size {:.1f}x{:.1f}",
                label->getString(), label->getFntFile(), label->isVisible(), label->getOpacity(),
                label->getDisplayedOpacity(), size.width, size.height);
        }
    };
    switch (stage) {
        case 0:
            notify("Notice test: SP 1/2 (11.2%)", NotificationIcon::Info);
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
            return finishSmokeTest();
    }
}

void nextSmokePhase() {
    auto pl = PlayLayer::get();
    auto& t = g_smoke;
    if (!pl) return;
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
        return;
    }
    if (t.ui && !t.uiDone) {
        t.uiDone = true;
        smokeUI(0);
        return;
    }
    finishSmokeTest();
}

// Before each step of the smoke test.
void smokeStep(PlayLayer* pl) {
    auto& t = g_smoke;
    if (t.dying) {
        // No input: the player crashes soon after the handoff.
    }
    else if (!t.reference.empty()) {
        // During the auto part the SP's own replay drives.
        if (!(g_play.sp && g_play.controlling)) followReference(pl);
    }
    else {
        int run = t.base + g_rec.step;
        if (run % 120 == 0) pl->handleButton(true, 1, true);
        if (run % 120 == 30) pl->handleButton(false, 1, true);
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
    if (t.tested == 0) t.firstRun.push_back({pl->m_player1->m_position.x, g_rec.hits});
    if (t.placed < t.placeAtX.size()) {
        if (pl->m_player1->m_position.x >= t.placeAtX[t.placed]) {
            if (++t.placed == t.placeAtX.size()) t.stepsLeft = TAIL_STEPS + 20;
            Loader::get()->queueInMainThread([] { placeSP(); });
        }
        return;
    }
    if (t.stepsLeft > 0 && --t.stepsLeft == 0) Loader::get()->queueInMainThread(nextSmokePhase);
}

bool switcherOn() {
    return Mod::get()->getSettingValue<bool>("switcher");
}

// After load(): `loaded.sps` may be moved out already, so the count comes apart.
void notifyLoaded(storage::Loaded const& loaded, size_t count) {
    using storage::Notice;
    auto warn = NotificationIcon::Warning;
    switch (loaded.notice) {
        case Notice::None: break;
        case Notice::Loaded: notify(NotificationIcon::Info, Msg::Loaded, count); break;
        case Notice::Failed: notify(warn, Msg::Failed, loaded.failed); break;
        case Notice::LevelChanged: notify(warn, Msg::LevelChanged); break;
        case Notice::CantSave: notify(warn, Msg::CantSave); break;
        case Notice::CantMoveAside: notify(warn, Msg::CantMoveAside); break;
        case Notice::SPsWithLowDetail: notify(warn, Msg::SPsWithLowDetail); break;
        case Notice::SPsWithoutLowDetail: notify(warn, Msg::SPsWithoutLowDetail); break;
    }
}

}

namespace core {

std::vector<SP> const& sps() {
    return g_session.sps;
}

SP const& active() {
    return g_session.active;
}

std::string label(SP const& sp) {
    return labelOf(sp);
}

void select(SP const& sp) {
    choose(sp, true);
}

void prepareRestart() {
    auto pl = PlayLayer::get();
    if (pl && pl->m_isPracticeMode) pl->removeAllCheckpoints();
}

void remove(SP const& sp) {
    if (auto pl = PlayLayer::get()) removeSP(pl, sp);
}

void rename(SP const& sp, std::string name) {
    sp->name = std::move(name);
    storage::saveNames(g_session.sps);
}

void notify(NotificationIcon icon, std::string const& text) {
    ::notify(text, icon);
}

void saveAll() {
    saveUnsaved();
}

ImportResult import(std::vector<Incoming> const& files, bool replace, std::vector<SP> const& keep) {
    ImportResult out;
    auto pl = PlayLayer::get();
    if (!pl || files.empty()) return out;
    auto& s = g_session;
    saveUnsaved();
    std::vector<ByteSpan> bytes;
    for (auto const& f : files) bytes.push_back(f.file);
    auto added = storage::add(bytes, s.nextId);
    for (auto const& sp : added) (sp ? out.added : out.failed)++;
    // Nothing came in: the level's SPs stay, even when replacing.
    if (out.added == 0) return out;
    if (replace) {
        std::vector<SP> aside;
        for (auto const& sp : s.sps) {
            if (std::find(keep.begin(), keep.end(), sp) == keep.end()) aside.push_back(sp);
        }
        auto moved = storage::setAside(aside);
        out.notMoved = static_cast<int>(aside.size() - moved.size());
        for (auto const& sp : moved) dropSP(pl, sp);
    }
    for (size_t i = 0; i < added.size(); i++) {
        auto const& sp = added[i];
        if (!sp) continue;
        sp->name = files[i].name;
        auto at = std::upper_bound(s.sps.begin(), s.sps.end(), sp->x, [](float x, auto const& o) { return x < o->x; });
        s.sps.insert(at, sp);
        addObjectFor(pl, *sp);
    }
    storage::saveNames(s.sps);
    log::info("Imported {} SPs ({} failed){}: {} in the level", out.added, out.failed,
        replace ? fmt::format(", replacing all but {} ({} not set aside)", keep.size(), out.notMoved) : "", s.sps.size());
    return out;
}

}

// The game is closing (or saving), possibly right from a level with SPs not written yet.
$on_mod(DataSaved) {
    if (PlayLayer::get()) saveUnsaved();
}

$on_mod(Loaded) {
    log::info("SP Creator {} loaded", Mod::get()->getVersion().toVString());
    listenForKeybindSettingPresses("place-sp", [](Keybind const&, bool down, bool repeat, double) {
        if (!down || repeat || !PlayLayer::get()) return false;
        return placeSP();
    });
    listenForKeybindSettingPresses("prev-sp", [](Keybind const&, bool down, bool repeat, double) {
        if (!down || repeat || !PlayLayer::get() || !switcherOn()) return false;
        return switchSP(-1);
    });
    listenForKeybindSettingPresses("delete-sp", [](Keybind const&, bool down, bool repeat, double) {
        if (!down || repeat || !PlayLayer::get()) return false;
        return deleteSP();
    });
    listenForKeybindSettingPresses("next-sp", [](Keybind const&, bool down, bool repeat, double) {
        if (!down || repeat || !PlayLayer::get() || !switcherOn()) return false;
        return switchSP(1);
    });
}

class $modify(SPCBaseLayer, GJBaseGameLayer) {
    bool isPlayLayer() {
        auto pl = PlayLayer::get();
        return pl && static_cast<GJBaseGameLayer*>(pl) == this;
    }

    void update(float dt) {
        auto start = std::chrono::steady_clock::now();
        GJBaseGameLayer::update(dt);
        if (!isPlayLayer()) return;
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        g_stats.frames++;
        g_stats.frameMs += ms;
        g_stats.maxFrameMs = std::max(g_stats.maxFrameMs, ms);
    }

    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        if (!isPlayLayer()) return GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        auto pl = PlayLayer::get();
        if (pl->m_player1 && !pl->m_isPlatformer) {
            // The smoke test plays like a player would: before the mod looks at the step.
            if (g_smoke.active) smokeStep(pl);
            if (g_play.sp) playStep(pl);
            recordStep(pl);
            logStats(pl);
        }
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        g_rec.step++;
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        if (!isPlayLayer()) return GJBaseGameLayer::handleButton(down, button, isPlayer1);
        int index = holdIndex(button, isPlayer1);
        if (g_play.sp && g_play.controlling && !g_play.injecting) {
            // The auto part owns the buttons: remember what the player holds for the handoff.
            g_play.userHolds[index] = down;
            // Unless control isn't blocked: then the first press ends the auto part and goes in. From inside this
            // hook a handleButton call goes to the hooks after this one, so the buttons are recorded here.
            if (down && !blockInput()) {
                endAutoPart(PlayLayer::get(), true, [this](int i, bool down) {
                    bool p1 = i <= 3;
                    this->press(down, p1 ? i : i - 3, p1);
                });
            }
            return;
        }
        this->press(down, button, isPlayer1);
    }

    void press(bool down, int button, bool isPlayer1) {
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
        int index = holdIndex(button, isPlayer1);
        g_rec.holds[index] = down;
        g_rec.inputs.push_back({g_rec.step, button, isPlayer1, down, g_rec.inputSeq++});
    }
};

class $modify(SPCPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        g_session = Session{};
        g_rec = Recording{};
        g_play = Playback{};
        g_stats = Stats{};
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        log::info("PlayLayer::init: level {} \"{}\"", level->m_levelID.value(), level->m_levelName);
        return true;
    }

    void processCreateObjectsFromSetup() {
        if (!g_session.creatingObjects) {
            g_session.creatingObjects = true;
            storage::beforeCreatingObjects();
        }
        PlayLayer::processCreateObjectsFromSetup();
    }

    void setupHasCompleted() {
        // The level has created its objects and picked its own startpos, if it has one.
        g_session.levelStartPos = m_startPosObject;
        g_session.levelTestMode = m_isTestMode;
        PlayLayer::setupHasCompleted();
        auto loaded = storage::load(this);
        g_session.sps = std::move(loaded.sps);
        g_session.nextId = loaded.nextId;
        // After the level's objects and its anticheat spike: theirs keep their places in m_objects (and so
        // their unique IDs) whatever the SPs.
        for (auto const& sp : g_session.sps) addObjectFor(this, *sp);
        notifyLoaded(loaded, g_session.sps.size());
        if (g_smoke.active && !g_smoke.importFrom.empty()) smokeImport();
    }

    void resetLevel() {
        if (g_play.sp && g_play.controlling) {
            log::info("SP {}: restart in the auto part at step {} of {}", g_play.sp->id, g_play.step, g_play.sp->handoff());
        }
        g_play = Playback{};
        saveUnsaved();
        applyActiveSP(this);
        PlayLayer::resetLevel();
        g_rec = Recording{};
        placeObjects();
        startPlayback(this);
        auto& s = g_session;
        if (s.lastCheckpoint) s.lastCheckpointRetains = s.lastCheckpoint->retainCount();
        if (g_smoke.active) g_smoke.refIndex = -1;  // the bot finds its place in the run again
        if (g_smoke.externalOn) logRestart(this, "restart");
    }

    float getCurrentPercent() {
        // An attempt from an SP starts at its spawn, an auto part before the SP. Until its first step, mods
        // that note where an attempt starts (Blitzkrieg, in resetLevel) get the percent of the SP's object,
        // as they count it: otherwise the attempt would also touch the stretch before the SP.
        auto sp = g_session.spawning;
        if (!sp && g_play.sp && g_play.controlling && g_play.step == 0) sp = g_play.sp;
        // From where the object stands, not where it is: the restart may have moved it (see placeObjects).
        if (sp && sp->object && m_checkpointArray->count() == 0) return startPosPercent(this, objectPosition(*sp).x);
        return PlayLayer::getCurrentPercent();
    }

    void onQuit() {
        if (g_smoke.active) {
            // Left midway: its noclip and the rest end here.
            log::warn("Smoke test: the level was quit, test stopped");
            restoreSmokeLevel();
            g_smoke = SmokeTest{};
            g_smokeTakeover = false;
        }
        saveUnsaved();
        // Checkpoints point into the level's objects: let go of them while the level still exists.
        g_play = Playback{};
        g_rec = Recording{};
        g_session = Session{};
        PlayLayer::onQuit();
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        // The anticheat spike is a fake death at the level start.
        if (object != m_anticheatSpike) {
            g_rec.hits++;
            if (g_play.sp && g_play.controlling && !g_smoke.active) {
                // Another mod's noclip may still save the player after this.
                log::warn("SP {}: hit object {} at {:.1f},{:.1f} in the auto part, step {} of {}", g_play.sp->id,
                    object ? object->m_objectID : 0, object ? object->getPositionX() : 0.f,
                    object ? object->getPositionY() : 0.f, g_play.step, g_play.sp->handoff());
            }
            if (g_smoke.dying) {
                g_smoke.dying = false;
                PlayLayer::destroyPlayer(player, object);
                log::info("Smoke test: died from SP at {:.2f}%: best now {}%, level attempts {}, test mode {}",
                    getCurrentPercent(), m_level->m_normalPercent.value(), m_level->m_attempts.value(), m_isTestMode);
                Loader::get()->queueInMainThread(nextSmokePhase);
                return;
            }
            if (g_smoke.active) {
                // Smoke test noclip; the first hits of each attempt go to the log.
                if (g_rec.hits <= 3) {
                    log::info("Smoke test: hit object {} at {:.1f},{:.1f}, player at {:.1f},{:.1f}", object ? object->m_objectID : 0,
                        object ? object->getPositionX() : 0.f, object ? object->getPositionY() : 0.f,
                        player->m_position.x, player->m_position.y);
                }
                return;
            }
        }
        PlayLayer::destroyPlayer(player, object);
    }

    void pauseGame(bool unfocused) {
        // The smoke test keeps playing while the window is in the background.
        if (g_smoke.active && !g_smoke.pausing) return;
        PlayLayer::pauseGame(unfocused);
        saveUnsaved();
    }
};

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
