#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>

#include "Core.hpp"
#include "Smoke.hpp"
#include "StartPoint.hpp"
#include "Storage.hpp"
#include "Text.hpp"

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
// Physics steps between hidden checkpoints: the auto part comes out up to this much longer than set.
// Fewer would take more work while playing.
constexpr int CHECKPOINT_INTERVAL = 24;

// A hidden checkpoint taken before `step` of the current attempt.
struct Snapshot {
    int step = 0;
    Ref<CheckpointObject> cp;
    Holds holds{};          // buttons held at that moment
    uint64_t inputSeq = 0;  // inputs from this one on came after the checkpoint
    std::array<Pressed, 2> pressed{};  // the players'
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
    bool holdOnly = false;  // the buttons injected now are held, not pressed (holdAgain)
    Holds gameHolds{};  // given to the game
    Holds userHolds{};  // what the player physically holds meanwhile
    // Largest difference from the original run: in the auto part before guiding, then after the handoff.
    float autoX = 0, autoY = 0, autoCam = 0;
    float tailX = 0, tailY = 0, tailCam = 0;
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
    // New SPs get them first (addObjectFor).
    std::vector<Ref<StartPosObject>> retired;
    int objectsMade = 0;                           // through addObject
    StartPosObject* levelStartPos = nullptr;       // the startpos the level itself picked (owned by the level)
    bool levelTestMode = false;
    int nextId = 1;
    bool creatingObjects = false;                  // the level has started creating its objects
    // Placed SPs not written yet: writing one takes ~10 ms, so it waits for a moment without gameplay
    // (restart, pause, quit) instead of stuttering right where the player pressed the key.
    std::vector<std::shared_ptr<StartPoint>> unsaved;
};

Session g_session;
int g_sessionCount = 0;  // levels entered
Recording g_rec;
Playback g_play;

float autoPartSeconds() {
    return static_cast<float>(Mod::get()->getSettingValue<double>("auto-part-length"));
}

bool blockInput() {
    return !smoke::takesOver() && Mod::get()->getSettingValue<bool>("block-input");
}

constexpr float NOTICE_SECONDS = 1.5f;

// The mod keeps one notification: Geode queues notifications and shows each for its full time, so
// switching quickly through SPs would show every SP passed on the way, one after another.
Ref<Notification> g_notice;
std::chrono::steady_clock::time_point g_noticeAt;
unsigned int g_noticeFrame = 0;  // frame it was shown in

// Notification has its label in bigFont, which has no Cyrillic letters: text that needs the mod's font
// goes into a label of the mod's beside it, with the notification's own one left empty.
// Long text is made smaller to fit the screen: the notification grows with its label, past the screen's edges.
// Geode's label scale, and what's left of the notification's width for the label (it has its icon too).
constexpr float NOTICE_LABEL_SCALE = 0.6f;
constexpr float NOTICE_ICON_WIDTH = 40.f;

void setNoticeText(Notification* n, std::string const& text) {
    constexpr auto OWN_LABEL = "text"_spr;
    float maxWidth = CCDirector::get()->getWinSize().width * 0.9f / n->getScale() - NOTICE_ICON_WIDTH;
    auto own = findFirstChildRecursive<CCLabelBMFont>(n, [&](CCLabelBMFont* l) { return l->getID() == OWN_LABEL; });
    auto label = findFirstChildRecursive<CCLabelBMFont>(n, [&](CCLabelBMFont* l) { return l->getID() != OWN_LABEL; });
    auto font = text::fontFor(text);
    if (std::string_view(font) == "bigFont.fnt") {
        if (own) own->removeFromParent();
        n->setString(text);
        if (label) {
            label->limitLabelWidth(maxWidth, NOTICE_LABEL_SCALE, 0.1f);
            n->setString(text);  // laid out again at that size
        }
        return;
    }
    if (!own) {
        if (!label || !label->getParent()) return n->setString(text);
        own = CCLabelBMFont::create("", font);
        own->setID(OWN_LABEL);
        // After the icon, which setIcon adds at the end.
        label->getParent()->addChild(own, 1);
    }
    own->setString(text.c_str());
    own->limitLabelWidth(maxWidth, NOTICE_LABEL_SCALE, 0.1f);
    n->setString("");  // lays the notification out again, around the mod's label
}

void notify(std::string const& text, NotificationIcon icon = NotificationIcon::Info) {
    log::info("Notice: {}", text);
    smoke::noticed(text);
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
    f.hits = g_rec.hits;
    return f;
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
// Switchers keep what they collected until the level is left, deleted SPs' objects too (MegaHack's list
// grew by all the SPs ever imported): a deleted SP's object, if there's one, comes back instead, past
// addObject, and their lists don't grow.
void addObjectFor(PlayLayer* pl, StartPoint& sp) {
    auto& retired = g_session.retired;
    if (!retired.empty()) {
        Ref<StartPosObject> obj = retired.front();
        retired.erase(retired.begin());
        obj->setPosition(objectPosition(sp));
        pl->m_objects->addObject(obj);
        pl->addToSection(obj);
        storage::numberObject(pl, obj);
        sp.object = obj;
        return;
    }
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
    g_session.objectsMade++;
}

// Takes a deleted SP's object out of the level.
void retireObject(PlayLayer* pl, StartPoint& sp) {
    if (!sp.object) return;
    pl->removeObjectFromSection(sp.object);
    g_session.retired.push_back(sp.object);
    pl->m_objects->removeObject(sp.object);
    sp.object = nullptr;  // it may go to another SP
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

// At the checkpoint, as the original run was there: the buttons held since before it, held again but not
// pressed (a press does what the run didn't do there: a spider on the ground teleports right away, a ball or
// swing flips), and what presses left in the players, which loading the checkpoint clears.
void holdAgain(PlayLayer* pl, StartPoint const& sp) {
    // Through the game's handleButton, which knows which players a button moves (both in dual mode).
    g_play.holdOnly = !sp.pressHolds;
    for (int i = 1; i < 7; i++) {
        if (sp.holds[i]) inject(pl, i, true);
    }
    g_play.holdOnly = false;
    if (sp.pressHolds) return;
    std::array<PlayerObject*, 2> players{pl->m_player1, pl->m_player2};
    for (size_t p = 0; p < players.size(); p++) {
        if (!players[p]) continue;
        players[p]->m_jumpBuffered = sp.pressed[p].jumpBuffered;
        players[p]->m_stateRingJump = sp.pressed[p].ringJump;
    }
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
        gs.m_cameraPosition = gs.m_cameraLastPosition = CCPoint(f.camX, f.camY);
    }
    auto now = captureFrame(pl);
    if (pb.controlling && k < sp.handoff()) {
        auto const& f = sp.frames[k];
        trackDiff(now, sp.frames, k, pb.autoX, pb.autoY, pb.autoCam);
        if (k == 0) holdAgain(pl, sp);
        // Back on the recorded path every step, in case the replay drifts. Not to a frame recorded before the game
        // had moved the player on (its x is the frame before's; see trackDiff): that would put the player a step
        // back, and the next frame a step on again, a jolt a wave shows.
        if (k == 0 || sp.frames[k - 1].x != f.x) {
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
        return;
    }
    log::info("SP {}: {} steps after the handoff, largest difference from the original run: x {:.3f}, y {:.3f}, camera {:.3f}",
        sp.id, sp.tail.size(), pb.tailX, pb.tailY, pb.tailCam);
    pb = Playback{};
}

void takeSnapshot(PlayLayer* pl) {
    auto cp = pl->createCheckpoint();
    if (!cp) return;
    auto pressed = [](PlayerObject* p) { return p ? Pressed{p->m_jumpBuffered, p->m_stateRingJump} : Pressed{}; };
    g_rec.snapshots.push_back({g_rec.step, cp, g_rec.holds, g_rec.inputSeq, {pressed(pl->m_player1), pressed(pl->m_player2)}});
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
    if (r.step % CHECKPOINT_INTERVAL == 0) takeSnapshot(pl);
    size_t keep = static_cast<size_t>(std::ceil(autoPartSeconds() * STEPS_PER_SECOND)) + CHECKPOINT_INTERVAL + 2;
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
    sp->pressed = snap->pressed;
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
    return true;
}

void saveUnsaved() {
    for (auto const& sp : g_session.unsaved) storage::save(*sp);
    g_session.unsaved.clear();
}

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
// (or the level start). `sp` by value: the caller's may be an element of the list it's erased from.
void dropSP(PlayLayer* pl, std::shared_ptr<StartPoint> sp) {
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
void removeSP(PlayLayer* pl, std::shared_ptr<StartPoint> sp) {
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
        case Notice::LevelChanged:
            if (!loaded.movedOver) notify(warn, Msg::LevelChanged);
            else if (loaded.notMoved.empty()) notify(NotificationIcon::Success, Msg::LevelUpdated, loaded.moved);
            else {
                notify(fmt::format("{}, {}", text::format(Msg::LevelUpdated, loaded.moved),
                    text::format(Msg::NotMoved, loaded.notMoved.size(), text::percents(loaded.notMoved))), warn);
            }
            break;
        case Notice::CantSave: notify(warn, Msg::CantSave); break;
        case Notice::CantMoveAside: notify(warn, Msg::CantMoveAside); break;
        case Notice::SPsWithLowDetail: notify(warn, Msg::SPsWithLowDetail); break;
        case Notice::SPsWithoutLowDetail: notify(warn, Msg::SPsWithoutLowDetail); break;
    }
}

}

#ifdef SPC_SMOKE
#include "Smoke.inl"
#endif

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

RemoveResult remove(std::vector<SP> const& sps, bool save) {
    RemoveResult out;
    auto pl = PlayLayer::get();
    if (!pl || sps.empty()) return out;
    std::vector<SP> gone;
    if (save) {
        // Setting aside moves the files: they have to be written.
        saveUnsaved();
        auto aside = storage::setAside(sps, storage::AsideFor::Delete);
        gone = std::move(aside.moved);
        out.saved = !gone.empty();
        out.existing = out.saved && aside.existing;
    }
    else {
        for (auto const& sp : sps) {
            if (storage::remove(*sp)) gone.push_back(sp);
        }
    }
    for (auto const& sp : gone) dropSP(pl, sp);
    storage::saveNames(g_session.sps);
    out.removed = static_cast<int>(gone.size());
    out.left = static_cast<int>(sps.size() - gone.size());
    log::info("Deleted {} of {} SPs ({}): {} in the level", out.removed, sps.size(), save ? "set aside" : "deleted",
        g_session.sps.size());
    return out;
}

void rename(SP const& sp, std::string name) {
    sp->name = std::move(name);
    storage::saveNames(g_session.sps);
}

int session() {
    return PlayLayer::get() ? g_sessionCount : 0;
}

void notify(NotificationIcon icon, std::string const& text) {
    ::notify(text, icon);
}

void saveAll() {
    saveUnsaved();
}

ImportResult import(std::vector<Incoming> const& files, Replace replace) {
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
    if (replace == Replace::Delete && out.failed > 0) {
        // One that failed may be the same as one of the level's: deleted, it would be gone.
        log::warn("Import: {} SPs failed, the level's are saved as a version instead of deleted", out.failed);
        replace = Replace::SaveVersion;
    }
    if (replace != Replace::No) {
        auto own = s.sps;
        std::vector<SP> gone;
        if (replace == Replace::SaveVersion) {
            auto aside = storage::setAside(own, storage::AsideFor::Replace);
            gone = std::move(aside.moved);
            out.saved = !gone.empty();
            out.existing = out.saved && aside.existing;
        }
        else {
            for (auto const& sp : own) {
                if (storage::remove(*sp)) gone.push_back(sp);
            }
        }
        out.notMoved = static_cast<int>(own.size() - gone.size());
        for (auto const& sp : gone) dropSP(pl, sp);
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
        replace == Replace::No ? std::string()
            : fmt::format(", replacing the level's ({}, {} not gone)", replace == Replace::SaveVersion ? "saved" : "deleted",
                out.notMoved),
        s.sps.size());
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

    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        if (!isPlayLayer()) return GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        auto pl = PlayLayer::get();
        if (pl->m_player1 && !pl->m_isPlatformer) {
            // The smoke test plays like a player would: before the mod looks at the step.
            smoke::beforeStep(pl);
            if (g_play.sp) playStep(pl);
            recordStep(pl);
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
                bool held = g_play.gameHolds[index];
                endAutoPart(PlayLayer::get(), true, [this](int i, bool down) {
                    bool p1 = i <= 3;
                    this->press(down, p1 ? i : i - 3, p1);
                });
                // The auto part held this button already: the click is still a press (a jump, an orb), which
                // the game takes from a held button too.
                if (held) this->press(true, button, isPlayer1);
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

class $modify(SPCPlayer, PlayerObject) {
    bool pushButton(PlayerButton button) {
        // A button held since before an SP's checkpoint (holdAgain): held, and nothing else a press does.
        if (g_play.holdOnly) {
            m_holdingButtons[static_cast<int>(button)] = true;
            return false;
        }
        return PlayerObject::pushButton(button);
    }
};

class $modify(SPCPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        g_session = Session{};
        g_sessionCount++;
        g_rec = Recording{};
        g_play = Playback{};
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
        smoke::loaded(this, loaded);
        g_session.sps = std::move(loaded.sps);
        g_session.nextId = loaded.nextId;
        // After the level's objects and its anticheat spike: theirs keep their places in m_objects (and so
        // their unique IDs) whatever the SPs.
        for (auto const& sp : g_session.sps) addObjectFor(this, *sp);
        notifyLoaded(loaded, g_session.sps.size());
        smoke::ready(this);
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
        smoke::restarted(this);
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
        smoke::quitting();
        saveUnsaved();
        storage::leave();
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
            if (g_play.sp && g_play.controlling) {
                // No death in the auto part: it replays a run that got through there, so a hit comes from what
                // loading the checkpoint doesn't put back at once (in BPATA a spike a trigger moves stands where
                // the level has it for a step, and moves on by a different amount each attempt).
                if (g_rec.hits <= 3) {
                    log::warn("SP {}: hit object {} at {:.1f},{:.1f} in the auto part, step {} of {}: no death", g_play.sp->id,
                        object ? object->m_objectID : 0, object ? object->getPositionX() : 0.f,
                        object ? object->getPositionY() : 0.f, g_play.step, g_play.sp->handoff());
                }
                return;
            }
            if (smoke::hit(this, player, object)) return;
        }
        PlayLayer::destroyPlayer(player, object);
        if (object != m_anticheatSpike) smoke::died(this);
    }

    void pauseGame(bool unfocused) {
        if (smoke::keepsPlaying()) return;
        PlayLayer::pauseGame(unfocused);
        saveUnsaved();
    }
};
