#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/ui/OverlayManager.hpp>

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
#include <optional>

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

// The game's own tick rate. SPs count time in its steps, 240ths of a second (the "steps" below), whatever rate
// the game runs at: another (Physics Bypass in Eclipse or Mega Hack) makes each step shorter or longer, and the
// mod records and replays by the time that goes by (stepUnit). At 240 a step is exactly one.
constexpr int STEPS_PER_SECOND = 240;
// Steps of the original run kept after placing an SP, to compare the handoff with it.
constexpr int TAIL_STEPS = 60;
// Steps between hidden checkpoints: the auto part comes out up to this much longer than set.
// Fewer would take more work while playing.
constexpr int CHECKPOINT_INTERVAL = 24;
// Times in steps that are the same (sums of a step's time have rounding in them at other rates).
constexpr double SAME_TIME = 1e-6;

// A hidden checkpoint taken before `step` of the current attempt, at `clock`.
struct Snapshot {
    int step = 0;
    double clock = 0;
    Ref<CheckpointObject> cp;
    Holds holds{};          // buttons held at that moment
    uint64_t inputSeq = 0;  // inputs from this one on came after the checkpoint
    std::array<Pressed, 2> pressed{};  // the players'
};

// The current attempt, as played.
struct Recording {
    int step = 0;              // physics steps played in this attempt
    double clock = 0;          // and the time they took, in steps (at 240 a second the same number)
    std::deque<Frame> frames;  // frames[i]: state before step firstStep + i
    std::deque<double> times;  // times[i]: the clock then
    int firstStep = 0;
    std::deque<Input> inputs;
    uint64_t inputSeq = 0;
    std::deque<Snapshot> snapshots;
    Holds holds{};
    std::shared_ptr<StartPoint> tailFor;  // SP placed in this attempt that still collects its tail
    double tailFrom = 0;                  // its handoff, by the clock
    std::optional<Frame> tailLast;        // the frame recorded before, and its clock
    double tailLastClock = 0;
    int hits = 0;
};

// The auto part of the attempt, if it started from an SP.
struct Playback {
    std::shared_ptr<StartPoint> sp;
    int step = 0;          // physics steps played since the spawn
    double cursor = 0;     // the time they took, in steps: where in the SP's frames the player is
                           // (at 240 a second the same number)
    size_t nextInput = 0;
    bool controlling = false;  // the auto part owns the buttons until the handoff
    bool injecting = false;
    bool holdOnly = false;  // the buttons injected now are held, not pressed (holdAgain)
    Holds gameHolds{};  // given to the game
    Holds userHolds{};  // what the player physically holds meanwhile
    // Largest difference from the original run: in the auto part before guiding, then after the handoff.
    float autoX = 0, autoY = 0, autoCam = 0;
    float tailX = 0, tailY = 0, tailCam = 0;
    bool formKept = false;  // another mod's form switch before the first step left out (othersSwitchForm)
};

// A startpos of the level hidden from other mods while the level creates its objects, and the blank object
// they get instead.
struct HiddenStartPos {
    Ref<StartPosObject> startPos;
    Ref<GameObject> blank;
    bool handing = false;  // on its way to the game past other mods' hooks
    bool handed = false;   // and it got there
};

// A startpos of the level in the mod's switcher.
struct LevelStartPos {
    StartPosObject* object;  // owned by the level
    float x;
    float percent;
};

// The open level's SPs (Storage.cpp keeps them between sessions).
struct Session {
    std::vector<std::shared_ptr<StartPoint>> sps;  // sorted by x
    std::shared_ptr<StartPoint> active;            // null: the level's own start
    // The level's own startposes are in the mod's switcher too, with the setting to hide them off (as it was on
    // entering the level): as other mods' switchers have them, and the level start is then the real one (0%).
    bool levelShown = false;
    std::vector<LevelStartPos> levelStartPoses;    // sorted by x
    StartPosObject* activeLevel = nullptr;         // when `active` is null: the startpos of the level restarts go to
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
    // The x of the startpos the last attempt started from (the level start: -infinity): which way a switcher went.
    float startedAtX = -std::numeric_limits<float>::infinity();
    int objectsMade = 0;                           // through addObject
    bool madeEarly = false;                        // the SPs' objects made as the level finished its own (madeEarly)
    StartPosObject* levelStartPos = nullptr;       // the startpos the level itself picked (owned by the level)
    bool levelTestMode = false;
    int nextId = 1;
    bool creatingObjects = false;                  // the level has started creating its objects
    // The level's own startposes hidden from other mods while it creates them (the setting, as it was on
    // entering the level), until storage::hide().
    bool hiding = false;
    std::vector<HiddenStartPos> hidden;
    int handing = 0;                               // of them on their way to the game
    // Placed SPs not written yet: writing one takes ~10 ms, so it waits for a moment without gameplay
    // (restart, pause, quit) instead of stuttering right where the player pressed the key.
    std::vector<std::shared_ptr<StartPoint>> unsaved;
};

Session g_session;
bool g_gameShowsPercent = false;  // in the game's PlayLayer::updateProgressbar (SPCPlayLayer::getCurrentPercent)
int g_sessionCount = 0;  // levels entered
// The step being played: what the game passed to processCommands, and its time in steps.
float g_stepDt = 0;
bool g_stepHalf = false, g_stepLast = false;
double g_stepUnit = 1;

// A step's time in steps of 240 a second: the game passes 1/240 s, Physics Bypass 1/TPS. A time warp trigger
// doesn't change it (the game makes more steps instead: Dash's 0.2 makes five times as many, each still 1/240),
// nor does a speedhack (it changes how many steps come in a second). The game's own rate gives exactly 1 (dt is
// a float: 1/240 is near it).
double stepUnit(float dt) {
    double unit = dt * STEPS_PER_SECOND;
    if (!std::isfinite(unit) || unit <= 0 || std::abs(unit - 1) < 1e-3) return 1;
    return unit;
}
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
// It goes by the clock, not by game time, which a speedhack slows down or speeds up (Eclipse's scales the
// scheduler's time). Shown with no time of its own, it's hidden here NOTICE_SECONDS after it came on screen;
// and its animations (Geode's fading in and out, actions on it and its parts) run by the clock too.
std::chrono::steady_clock::time_point g_noticeHideAt;
bool g_noticeSeen = false;    // came on screen (Geode shows notifications one at a time: it may wait its turn)
bool g_noticeHidden = false;  // hidden by the clock: fading out
bool g_noticeTimed = false;   // the clock check is queued
constexpr auto NOTICE_TIME = std::chrono::milliseconds(static_cast<int>(NOTICE_SECONDS * 1000));
constexpr auto NOTICE_STALE = std::chrono::seconds(5);  // still waiting its turn by then: not worth showing

// The actions of the mod's notifications, ticked by the clock (noticeFrame) instead of the game's scheduler.
class ClockActions : public CCActionManager {
public:
    void tick(float dt) {
        update(dt);
    }
};
Ref<ClockActions> g_noticeActions;
std::chrono::steady_clock::time_point g_noticeTickedAt;

// Before it runs any: a node's actions go to the manager it has as they're added.
void useClockActions(CCNode* node) {
    if (!g_noticeActions) {
        g_noticeActions = new ClockActions();
        g_noticeActions->release();  // the Ref has it
    }
    node->setActionManager(g_noticeActions);
    for (auto child : CCArrayExt<CCNode*>(node->getChildren())) useClockActions(child);
}

// The clock actions on by the time since they were last ticked. Ticked again in the same frame (the notice about
// the level's startposes has its own frames), they go on by nothing.
void tickNoticeActions(std::chrono::steady_clock::time_point now) {
    if (g_noticeActions) g_noticeActions->tick(std::chrono::duration<float>(now - g_noticeTickedAt).count());
    g_noticeTickedAt = now;
}

bool g_levelNoticeTimed = false;  // levelNoticeFrame is queued

// Once a frame (queueInMainThread runs what's queued meanwhile in the next frame), till the notification is
// off the screen: its animations a frame on by the clock, and hidden on time.
void noticeFrame() {
    auto now = std::chrono::steady_clock::now();
    tickNoticeActions(now);
    if (!g_notice) {
        g_noticeTimed = false;
        return;
    }
    if (g_notice->isShowing()) {
        if (!g_noticeSeen) g_noticeHideAt = now + NOTICE_TIME;
        g_noticeSeen = true;
        if (!g_noticeHidden && now >= g_noticeHideAt) {
            g_noticeHidden = true;
            g_notice->hide();
        }
    }
    else if (g_noticeSeen || now - g_noticeAt > NOTICE_STALE) {
        // Gone (faded out, cancelled), or never came on.
        if (!g_noticeSeen) g_notice->cancel();
        g_noticeTimed = false;
        return;
    }
    Loader::get()->queueInMainThread(noticeFrame);
}

void timeNotice(std::chrono::steady_clock::time_point now) {
    g_noticeAt = now;
    g_noticeHideAt = now + NOTICE_TIME;
    if (g_noticeTimed) return;
    g_noticeTimed = true;
    if (!g_levelNoticeTimed) g_noticeTickedAt = now;
    Loader::get()->queueInMainThread(noticeFrame);
}

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
    // On screen and not fading out yet: change it in place, its time starting over.
    if (g_notice && g_notice->isShowing() && g_noticeTimed && !g_noticeHidden && g_noticeHideAt - now > NOTICE_TIME / 5) {
        setNoticeText(g_notice, text);
        g_notice->setIcon(icon);
        useClockActions(g_notice);  // parts made anew
        timeNotice(now);
        return;
    }
    // Still waiting in the queue: drop it; fading out: let it go (its actions still ticked by noticeFrame, as
    // long as the new one waits its turn).
    if (g_notice && !g_noticeHidden) g_notice->cancel();
    // Time 0: no timer of its own (noticeFrame).
    g_notice = Notification::create(text, icon, 0.f);
    setNoticeText(g_notice, text);
    useClockActions(g_notice);
    g_noticeSeen = false;
    g_noticeHidden = false;
    g_notice->show();
    timeNotice(now);
}

template <class... Args>
void notify(NotificationIcon icon, Msg msg, Args&&... args) {
    notify(text::format(msg, std::forward<Args>(args)...), icon);
}

// The notice on entering a level that has startposes of its own, at the same time as the mod's other notices
// ("SPs loaded") and under them: Geode shows its notifications one at a time, so this one isn't in its queue. It
// looks and moves as they do (Notification.cpp), by the clock too, LEVEL_NOTICE_TIME on screen.
constexpr auto LEVEL_NOTICE_TIME = std::chrono::seconds(4);
constexpr float NOTICE_FADEIN = 0.3f;
constexpr float NOTICE_FADEOUT = 0.5f;
constexpr float NOTICE_GAP = 4.f;

class LevelNotice : public Notification {
public:
    static LevelNotice* create(std::string const& text) {
        auto ret = new LevelNotice();
        if (ret->init("", createIcon(NotificationIcon::Info), 0.f)) {
            ret->autorelease();
            setNoticeText(ret, text);
            return ret;
        }
        delete ret;
        return nullptr;
    }

    float height() {
        return this->getBG()->getContentSize().height * this->getScale();
    }

    float width() {
        return this->getBG()->getContentSize().width * this->getScale();
    }

    // At `y`, faded and scaled in as Geode's.
    void appear(float y) {
        auto win = CCDirector::get()->getWinSize();
        this->setPosition(win.width / 2, y);
        auto scene = CCScene::get();
        this->setZOrder(scene && scene->getChildrenCount() > 0 ? scene->getHighestChildZ() + 2 : 10);
        OverlayManager::get()->addChild(this);
        useClockActions(this);
        for (CCNodeRGBA* part : {static_cast<CCNodeRGBA*>(this->getContent()), static_cast<CCNodeRGBA*>(this->getBG())}) {
            part->setOpacity(0);
            part->setScale(0.6f);
            part->setPositionY(-60.f);
            part->runAction(CCFadeTo::create(NOTICE_FADEIN, part == this->getBG() ? 150 : 255));
            part->runAction(CCEaseExponentialOut::create(CCScaleTo::create(NOTICE_FADEIN, 1.f, 1.f)));
            part->runAction(CCEaseExponentialOut::create(CCMoveBy::create(NOTICE_FADEIN, {0.f, 60.f})));
        }
    }

    // Faded and scaled out as Geode's, then off the screen.
    void disappear() {
        for (CCNodeRGBA* part : {static_cast<CCNodeRGBA*>(this->getContent()), static_cast<CCNodeRGBA*>(this->getBG())}) {
            part->stopAllActions();
            part->runAction(CCEaseExponentialIn::create(CCFadeTo::create(NOTICE_FADEOUT, 0)));
            part->runAction(CCEaseExponentialIn::create(CCMoveBy::create(NOTICE_FADEOUT, {0.f, -25.f})));
            part->runAction(CCSequence::create(CCDelayTime::create(NOTICE_FADEOUT * 0.25f),
                CCEaseExponentialIn::create(CCScaleTo::create(NOTICE_FADEOUT, 0.2f, 0.2f)), nullptr));
        }
        this->runAction(CCSequence::create(CCDelayTime::create(NOTICE_FADEOUT), CallFuncExt::create([this] {
            this->remove();
        }), nullptr));
    }

    void remove() {
        OverlayManager::get()->removeChild(this);
        this->removeFromParent();
    }
};

Ref<LevelNotice> g_levelNotice;
std::chrono::steady_clock::time_point g_levelNoticeAt;
bool g_levelNoticeHiding = false;

void levelNoticeFrame() {
    auto now = std::chrono::steady_clock::now();
    tickNoticeActions(now);
    if (!g_levelNotice || (g_levelNoticeHiding && !g_levelNotice->getParent())) {
        g_levelNotice = nullptr;
        g_levelNoticeTimed = false;
        return;
    }
    if (!g_levelNoticeHiding && now - g_levelNoticeAt >= LEVEL_NOTICE_TIME) {
        g_levelNoticeHiding = true;
        g_levelNotice->disappear();
        log::info("Notice about the level's startposes hidden after {} ms",
            std::chrono::duration_cast<std::chrono::milliseconds>(now - g_levelNoticeAt).count());
    }
    Loader::get()->queueInMainThread(levelNoticeFrame);
}

// On entering a level with `count` startposes of its own (`hidden` by the mod), each time.
void levelStartPosNotice(size_t count, bool hidden) {
    if (count == 0 || !Mod::get()->getSettingValue<bool>("startpos-notice")) return;
    auto text = text::format(hidden ? Msg::LevelStartPosesHidden : Msg::LevelStartPosesShown, count);
    log::info("Notice about the level's startposes: {}", text);
    // One a level: one left from the level before goes at once.
    if (g_levelNotice && g_levelNotice->getParent()) {
        log::info("Notice about the level's startposes: the one from before taken off");
        g_levelNotice->remove();
    }
    g_levelNotice = LevelNotice::create(text);
    auto oneLine = LevelNotice::create(std::string(text::tr(Msg::LevelStart)));
    if (!g_levelNotice || !oneLine) return;
    // Geode's notifications stand at a quarter of the screen's height: this one's top under where theirs ends.
    auto win = CCDirector::get()->getWinSize();
    float y = win.height / 4 - oneLine->height() / 2 - NOTICE_GAP - g_levelNotice->height() / 2;
    g_levelNotice->appear(y);
    log::info("Notice about the level's startposes: {:.0f}x{:.0f} at y {:.0f} on a {:.0f}x{:.0f} screen",
        g_levelNotice->width(), g_levelNotice->height(), y, win.width, win.height);
    auto now = std::chrono::steady_clock::now();
    g_levelNoticeAt = now;
    g_levelNoticeHiding = false;
    if (g_levelNoticeTimed) return;
    g_levelNoticeTimed = true;
    if (!g_noticeTimed) g_noticeTickedAt = now;
    Loader::get()->queueInMainThread(levelNoticeFrame);
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

// Between two frames, at the game's own rate one step apart, at another rate a fraction of one: the player's
// numbers go evenly from one to the other.
float lerp(float a, float b, double f) {
    return static_cast<float>(a + (b - a) * f);
}

PlayerFrame between(PlayerFrame const& a, PlayerFrame const& b, double f) {
    // The short way round: rotations may come as 350 then 10.
    double turn = std::remainder(static_cast<double>(b.rot) - a.rot, 360.0);
    return {lerp(a.y, b.y, f), a.yVel + (b.yVel - a.yVel) * f, static_cast<float>(a.rot + turn * f)};
}

// The state a fraction f of the way from a to b. Hits so far: a's.
Frame between(Frame const& a, Frame const& b, double f) {
    Frame r = a;
    r.x = lerp(a.x, b.x, f);
    r.p1 = between(a.p1, b.p1, f);
    if (a.dual && b.dual) r.p2 = between(a.p2, b.p2, f);
    r.camX = lerp(a.camX, b.camX, f);
    r.camY = lerp(a.camY, b.camY, f);
    return r;
}

// A frame with the position of the frame before. The game moves the player's m_position once a frame, not each
// step (the speed is the step's own): in a frame of several steps the later ones show the first one's place. How
// the auto part has always told them at the game's own rate: by x.
template <class Frames>
bool staleX(Frames const& frames, size_t i) {
    return i > 0 && frames[i].x == frames[i - 1].x;
}

// The same, for going between frames: the whole position the frame before's. Not x alone: in rotated gameplay
// the player goes along y with x standing still.
template <class Frames>
bool stale(Frames const& frames, size_t i) {
    return i > 0 && frames[i].x == frames[i - 1].x && frames[i].p1.y == frames[i - 1].p1.y;
}

// From frame a to frame b the player's numbers don't go evenly when something happens on the way: a jump (a
// teleport, the second player coming or going) or the speed changing at once (a pad, an orb, a portal, a ceiling;
// gravity alone changes it a fraction of a unit a step). Then what's in between is left to the game's steps, which
// do it as the run did: a ball pushed half way to an orb's speed would miss the orb.
template <class Frames>
bool happensBetween(Frames const& frames, size_t a, size_t b) {
    constexpr float JUMP = 40.f;         // a step moves the player a few units
    constexpr double SPEED_JUMP = 1.0;
    auto const& fa = frames[a];
    auto const& fb = frames[b];
    if (fa.dual != fb.dual || std::abs(fb.x - fa.x) > JUMP || std::abs(fb.p1.y - fa.p1.y) > JUMP
        || (fa.dual && std::abs(fb.p2.y - fa.p2.y) > JUMP)) {
        return true;
    }
    for (size_t k = a; k < b; k++) {
        auto const& f = frames[k];
        auto const& g = frames[k + 1];
        if (std::abs(g.p1.yVel - f.p1.yVel) > SPEED_JUMP) return true;
        if (f.dual && g.dual && std::abs(g.p2.yVel - f.p2.yVel) > SPEED_JUMP) return true;
    }
    return false;
}

// The run's state at `at` (in steps) as an SP keeps it, from frames recorded at `times` at another tick rate,
// starting the search at `i` (moved on as it goes: the times asked for only grow). A frame recorded right then:
// that one, as it is (always so at the game's own rate). Otherwise between the last frame before and the first
// after, stale ones left out. Where that can't be (nothing after it, something happening in between, an input
// in between: `inputBy(a, b)`), a copy of `before`, the frame made before it: stale, so the auto part leaves the
// player to the game there.
// An input after frame a, by frame b: frame b has its doing in it already. The game moves the player for some
// presses as they come (a spider's teleport, a ball's flip), before the step's frame is taken; in between the
// player would have, say, a ball's lift without its flip, off the ground the press needs.
template <class Frames, class Times, class InputBy>
Frame frameAt(Frames const& frames, Times const& times, size_t& i, double at, Frame const& before, InputBy inputBy) {
    while (i + 1 < frames.size() && times[i + 1] <= at + SAME_TIME) i++;
    if (std::abs(times[i] - at) < SAME_TIME) return frames[i];
    size_t a = i;
    while (a > 0 && stale(frames, a)) a--;
    size_t b = i + 1;
    while (b < frames.size() && stale(frames, b)) b++;
    if (b >= frames.size() || inputBy(a, b) || happensBetween(frames, a, b)) return before;
    return between(frames[a], frames[b], (at - times[a]) / (times[b] - times[a]));
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
// crashes into the object (BPATA): they're put back in place after each restart instead, and after each
// frame's visibility pass (postUpdate), which does the same to objects added on entering the level: their
// stages in Blitzkrieg were at 0.00% after entering the level again.
void placeObjects() {
    for (auto const& sp : g_session.sps) {
        if (!sp->object) continue;
        auto at = objectPosition(*sp);
        if (sp->object->getPosition() != at) sp->object->setPosition(at);
    }
}

// A startpos object for an SP at `at`, added to the level through addObject: other mods' addObject hooks
// (startpos switchers) collect it like the level's startposes. `keepIds`: the game's unique ID counter left
// as it was (before the game adds its own objects after the level's).
StartPosObject* addStartPos(PlayLayer* pl, CCPoint at, bool keepIds) {
    // Made the way the level's own startposes are (object 31): StartPosObject::create() alone leaves its
    // object ID at 0, which switchers skip.
    auto made = keepIds ? storage::createKeepingIds(31) : GameObject::createWithKey(31);
    auto obj = typeinfo_cast<StartPosObject*>(made);
    if (!obj || !obj->m_startSettings) return nullptr;
    // No enter effects ("Don't Enter"): they put an object at its start position, 0,0 for the SPs' (see
    // placeObjects), every frame for the objects added on entering, till a restart from an SP.
    obj->m_isDontEnter = true;
    obj->m_ignoreEnter = true;
    obj->setPosition(at);
    auto startPos = pl->m_startPosObject;
    bool testMode = pl->m_isTestMode;
    pl->addObject(obj);
    // The game takes an added startpos for the level's start when it's further on: keep the one it has.
    if (pl->m_startPosObject != startPos) pl->setStartPosObject(startPos);
    pl->m_isTestMode = testMode;
    g_session.objectsMade++;
    return obj;
}

// Out of the level, kept for an SP to come (addObjectFor): other mods' switchers may still have it.
void setAside(PlayLayer* pl, Ref<StartPosObject> obj) {
    pl->removeObjectFromSection(obj);
    g_session.retired.push_back(obj);
    pl->m_objects->removeObject(obj);
}

// Adds the SP's startpos object to the level, where the player takes control.
// Switchers keep what they collected until the level is left, deleted SPs' objects too (MegaHack's list
// grew by all the SPs ever imported): a deleted SP's object, if there's one, comes back instead, past
// addObject, and their lists don't grow. The objects made for the SPs as the level finished making its
// objects (madeEarly) are set aside the same way, each at its SP's x: the SP gets that one back.
void addObjectFor(PlayLayer* pl, StartPoint& sp) {
    auto& retired = g_session.retired;
    if (!retired.empty()) {
        auto it = std::find_if(retired.begin(), retired.end(), [&](auto const& o) { return o->getPositionX() == sp.x; });
        if (it == retired.end()) it = retired.begin();
        Ref<StartPosObject> obj = *it;
        retired.erase(it);
        obj->setPosition(objectPosition(sp));
        pl->m_objects->addObject(obj);
        pl->addToSection(obj);
        storage::numberObject(pl, obj);
        sp.object = obj;
        return;
    }
    auto obj = addStartPos(pl, objectPosition(sp), false);
    if (!obj) {
        log::error("SP {}: no startpos object", sp.id);
        return;
    }
    storage::numberObject(pl, obj);
    sp.object = obj;
}

// As the level finishes making its objects, where other mods' switchers take its startposes (Eclipse's sorts them
// there, and with none stays off for the level), objects for the SPs, at their x as the files have it. The SPs load
// later (setupHasCompleted), once the game has added its anticheat spike after the level's objects: in m_objects
// their objects come after it, which their checkpoints count on (it's referred to by its place). So these are set
// aside at once, as deleted SPs' objects are, till the SPs load and take them back.
void madeEarly(PlayLayer* pl) {
    auto& s = g_session;
    if (s.madeEarly) return;
    s.madeEarly = true;
    auto xs = storage::peekPositions(pl);
    std::sort(xs.begin(), xs.end());
    // Not hidden like the level's startposes: they're the SPs'.
    bool hiding = std::exchange(s.hiding, false);
    for (float x : xs) {
        if (auto obj = addStartPos(pl, {x, 105.f}, true)) setAside(pl, obj);
    }
    s.hiding = hiding;
    if (!xs.empty()) log::info("{} SP objects made as the level finished making its objects", xs.size());
}

// Takes a deleted SP's object out of the level.
void retireObject(PlayLayer* pl, StartPoint& sp) {
    if (!sp.object) return;
    setAside(pl, sp.object);
    sp.object = nullptr;  // it may go to another SP
    // The SPs' objects after it moved up a place.
    for (auto const& other : g_session.sps) {
        if (other->object) storage::numberObject(pl, other->object);
    }
}

// Where restarts go: an SP, a startpos of the level (in the switcher, see Session::levelShown), or neither: the
// level start.
struct Entry {
    std::shared_ptr<StartPoint> sp;
    StartPosObject* startPos = nullptr;
    bool operator==(Entry const&) const = default;
};

Entry current() {
    auto const& s = g_session;
    return {s.active, s.active ? nullptr : s.activeLevel};
}

// `obj`, if it's a startpos of the level in the switcher.
StartPosObject* levelStartPosOf(StartPosObject* obj) {
    auto const& l = g_session.levelStartPoses;
    return obj && std::any_of(l.begin(), l.end(), [&](auto const& p) { return p.object == obj; }) ? obj : nullptr;
}

// The level start: with the level's startposes in the switcher, the real one; without, the startpos the level
// picked itself, if it has one (where the level starts without the mod).
StartPosObject* levelStart() {
    return g_session.levelShown ? nullptr : g_session.levelStartPos;
}

bool levelStartTestMode() {
    return g_session.levelShown ? false : g_session.levelTestMode;
}

// Restarts go to `to` from the next one on. `now`: the restart follows right away.
void choose(Entry to, bool now = false) {
    auto& s = g_session;
    s.active = std::move(to.sp);
    s.activeLevel = s.active ? nullptr : to.startPos;
    s.chosen = true;
    s.chosenNow = now;
    auto pl = PlayLayer::get();
    s.startPosWhenChosen = pl ? pl->m_startPosObject : nullptr;
    s.checkpointWhenChosen = pl ? pl->m_currentCheckpoint : nullptr;
}

void choose(std::shared_ptr<StartPoint> sp, bool now = false) {
    choose(Entry{std::move(sp)}, now);
}

Entry pastDeleted(StartPosObject* obj);

// Before resetLevel. Whoever set the startpos last decides where the attempt starts: this mod, or
// another mod's switcher, which may pick an SP's object too. Then the game's startpos retry path gets
// the SP's checkpoint, or the level its own start back.
void applyActiveSP(PlayLayer* pl) {
    auto& s = g_session;
    bool foreign = pl->m_startPosObject != s.startPosWhenChosen || pl->m_currentCheckpoint != s.checkpointWhenChosen;
    bool picked = s.chosen && (!foreign || s.chosenNow);
    if (!picked && isRetired(pl->m_startPosObject)) {
        auto to = pastDeleted(pl->m_startPosObject);
        s.active = to.sp;
        s.activeLevel = to.startPos;
        picked = true;
    }
    else if (!picked) {
        s.active = spOf(pl->m_startPosObject);
        s.activeLevel = s.active ? nullptr : levelStartPosOf(pl->m_startPosObject);
    }
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
    if (picked && s.levelShown) {
        // The mod picked a startpos of the level or the level start: there, as other mods' switchers go. The
        // checkpoint the game made at the startpos it starts from (an SP's, set by the mod) would put the player
        // back there: the game makes the new one's.
        auto to = s.activeLevel;
        if (pl->m_startPosObject != to || (s.lastCheckpoint && pl->m_currentCheckpoint == s.lastCheckpoint)) {
            setCurrentCheckpoint(pl, nullptr);
        }
        pl->setStartPosObject(to);
        pl->m_isTestMode = to != nullptr;
        s.ownTestMode = false;
        s.lastCheckpoint = nullptr;
        return;
    }
    // Off the SPs (also when a switcher picks a deleted SP's object): the level's own start.
    bool retired = isRetired(pl->m_startPosObject);
    if (spOf(pl->m_startPosObject) || retired) pl->setStartPosObject(levelStart());
    // Test mode was on for the SP (or the switcher that picked the deleted SP turned it on).
    if (s.ownTestMode || retired) {
        if (pl->m_startPosObject == levelStart()) pl->m_isTestMode = levelStartTestMode();
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

// Where a startpos at x is (the level's own, in the switcher), as the game counts the percent in a run from the
// start: by position, or in levels whose percent goes by time, by the level's time there against its timestamp (the
// time to the end, in steps of 1/240 s). Not against the time to the level's last object, which can stand far past
// the end (Fiberglass: 68% for 95%).
float startPosPercent(PlayLayer* pl, float x) {
    float percent;
    if (pl->m_level->m_timestamp > 0) {
        percent = pl->timeForPos({x, 0.f}, 0, 0, true, 0) * 240.f / pl->m_level->m_timestamp * 100.f;
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
// Has the player the checkpoint's form?
bool sameForm(PlayerObject* p, PlayerCheckpoint* c) {
    return p->m_isShip == c->m_isShip && p->m_isBall == c->m_isBall && p->m_isBird == c->m_isBird && p->m_isDart == c->m_isDart
        && p->m_isRobot == c->m_isRobot && p->m_isSpider == c->m_isSpider && p->m_isSwing == c->m_isSwing;
}

// Is the player what the checkpoint makes it: form, size, speed, gravity, direction?
bool asInCheckpoint(PlayerObject* p, PlayerCheckpoint* c) {
    return sameForm(p, c)
        && (p->m_vehicleSize < 1.f) == c->m_isMini && std::abs(p->m_playerSpeed - c->m_playerSpeed) < 0.001f
        && p->m_isUpsideDown == c->m_isUpsideDown && p->m_isSideways == c->m_isSideways
        && p->m_isGoingLeft == c->m_isGoingLeft;
}

// The player back to a cube, as a restart makes it before loading the checkpoint: loading one turns its form on,
// not another one off.
void toCube(PlayerObject* p) {
    if (p->m_isShip) p->toggleFlyMode(false, false);
    if (p->m_isBall) p->toggleRollMode(false, false);
    if (p->m_isBird) p->toggleBirdMode(false, false);
    if (p->m_isDart) p->toggleDartMode(false, false);
    if (p->m_isRobot) p->toggleRobotMode(false, false);
    if (p->m_isSpider) p->toggleSpiderMode(false, false);
    if (p->m_isSwing) p->toggleSwingMode(false, false);
}

// What Mega Hack's Smart StartPos writes in the game's state with the form it gives, before the first step (the bot's
// dumps in Storming Summit and RAM): the mode's camera height and the portal the player would have come through.
// Storming Summit's camera stayed at a ship's height for the cube. A restart from an SP changes some of them itself
// in places, and as the checkpoint has them the camera follows the run closer there too (Storming Summit: 9 units
// off at most instead of 20).
std::vector<char const*> portalStateBack(GJGameState& gs, GJGameState const& c) {
    std::vector<char const*> back;
    auto field = [&](auto& now, auto const& was, char const* name) {
        if (std::memcmp(&now, &was, sizeof now) == 0) return;
        now = was;
        back.push_back(name);
    };
    field(gs.m_unkPoint5, c.m_unkPoint5, "unkPoint5");
    field(gs.m_unkPoint6, c.m_unkPoint6, "unkPoint6");
    field(gs.m_unkPoint7, c.m_unkPoint7, "unkPoint7");
    field(gs.m_unkPoint10, c.m_unkPoint10, "unkPoint10");
    field(gs.m_unkPoint11, c.m_unkPoint11, "unkPoint11");
    field(gs.m_unkPoint17, c.m_unkPoint17, "unkPoint17");
    field(gs.m_lastModeHeight, c.m_lastModeHeight, "lastModeHeight");
    field(gs.m_portalY, c.m_portalY, "portalY");
    field(gs.m_unkInt14, c.m_unkInt14, "unkInt14");
    field(gs.m_unkBool7, c.m_unkBool7, "unkBool7");
    field(gs.m_lastActivatedPortal1, c.m_lastActivatedPortal1, "lastActivatedPortal1");
    field(gs.m_lastActivatedPortal2, c.m_lastActivatedPortal2, "lastActivatedPortal2");
    field(gs.m_dualAnimationProgress, c.m_dualAnimationProgress, "dualAnimationProgress");
    field(gs.m_dualRelated, c.m_dualRelated, "dualRelated");
    return back;
}

// Another mod may change the player after the restart, past the checkpoint: Mega Hack's Smart StartPos gives it
// the form, speed and size it works out for the startpos (Dash: a cube at 0.9 where the run was a ball at 1.3;
// Storming Summit: a ship where it was a cube), before the game's first step. The SP's checkpoint wins, before
// that step too.
void undoOthersAtSpawn(PlayLayer* pl, StartPoint const& sp) {
    auto cp = sp.cp.data();
    if (!cp) return;
    auto& gs = pl->m_gameState;
    std::vector<std::string> undone;
    auto player = [&](PlayerObject* p, PlayerCheckpoint* c, char const* which) {
        if (!p || !c || asInCheckpoint(p, c)) return;
        if (!sameForm(p, c)) toCube(p);
        p->loadFromCheckpoint(c);
        undone.push_back(which);
        if (!asInCheckpoint(p, c)) log::warn("SP {}: {} still not as in its checkpoint after putting it back", sp.id, which);
    };
    player(pl->m_player1, cp->m_player1Checkpoint, "player 1");
    if (gs.m_isDualMode) player(pl->m_player2, cp->m_player2Checkpoint, "player 2");
    if (gs.m_isFlipped != cp->m_gameState.m_isFlipped) {
        pl->toggleFlipped(cp->m_gameState.m_isFlipped, true);
        undone.push_back("mirror");
    }
    bool dual = gs.m_isDualMode != cp->m_gameState.m_isDualMode;
    if (auto back = portalStateBack(gs, cp->m_gameState); !back.empty()) {
        undone.push_back(fmt::format("game state ({})", fmt::join(back, ", ")));
    }
    if (!undone.empty()) {
        log::info("SP {}: changed after the restart, put back as in its checkpoint: {}", sp.id, fmt::join(undone, ", "));
    }
    if (dual) log::warn("SP {}: dual changed after the restart, not put back", sp.id);
}

// The recorded state at c (in steps: another tick rate) to put the player in, as frameAt has it; `nextInput`: the
// step of the next input the replay hasn't pressed yet. None where the game's steps take the player on: from the
// last frame to the handoff, and where something happens between frames.
std::optional<Frame> recordedAt(std::vector<Frame> const& frames, double c, int nextInput) {
    size_t a = static_cast<size_t>(std::floor(c + SAME_TIME));
    if (a >= frames.size()) return std::nullopt;
    if (std::abs(c - static_cast<double>(a)) < SAME_TIME && !stale(frames, a)) return frames[a];
    while (a > 0 && stale(frames, a)) a--;
    size_t b = static_cast<size_t>(std::floor(c + SAME_TIME)) + 1;
    while (b < frames.size() && stale(frames, b)) b++;
    if (b >= frames.size() || nextInput <= static_cast<int>(b) || happensBetween(frames, a, b)) return std::nullopt;
    return between(frames[a], frames[b], (c - static_cast<double>(a)) / static_cast<double>(b - a));
}

// `unit`: the step's time in steps (stepUnit). The auto part goes by time: at the game's own rate step k plays
// frame k, at another the frames go by as fast as the time does.
void playStep(PlayLayer* pl, double unit) {
    auto& pb = g_play;
    auto const& sp = *pb.sp;
    int k = pb.step++;
    double c = pb.cursor;
    pb.cursor += unit;
    if (k == 0) {
        undoOthersAtSpawn(pl, sp);
        // Loading the checkpoint puts the camera on its target instead of where it was (up to 15 units
        // off in BPATA), and the next frames would ease back from there: start where the run had it.
        auto const& f = sp.frames.front();
        auto& gs = pl->m_gameState;
        gs.m_cameraPosition = gs.m_cameraLastPosition = CCPoint(f.camX, f.camY);
    }
    auto now = captureFrame(pl);
    int n = sp.handoff();
    if (pb.controlling && c < n - SAME_TIME) {
        int i = static_cast<int>(std::lround(c));
        bool whole = std::abs(c - i) < SAME_TIME;
        trackDiff(now, sp.frames, std::min(i, n - 1), pb.autoX, pb.autoY, pb.autoCam);
        if (k == 0) holdAgain(pl, sp);
        // Back on the recorded path every step, in case the replay drifts. Not to a frame recorded before the game
        // had moved the player on (its x is the frame before's; see trackDiff): that would put the player a step
        // back, and the next frame a step on again, a jolt a wave shows. At another rate between frames
        // (recordedAt), the inputs due at this step as pressed already: they go in right after.
        std::optional<Frame> f;
        if (unit == 1 && whole) {
            if (!staleX(sp.frames, i)) f = sp.frames[i];
        }
        else {
            size_t next = pb.nextInput;
            while (next < sp.inputs.size() && sp.inputs[next].step <= c + SAME_TIME) next++;
            f = recordedAt(sp.frames, c, next < sp.inputs.size() ? sp.inputs[next].step : INT_MAX);
        }
        if (f) {
            forcePlayer(pl->m_player1, f->p1);
            if (f->dual && pl->m_player2) forcePlayer(pl->m_player2, f->p2);
        }
        // An input goes in before the first step at or past its time.
        while (pb.nextInput < sp.inputs.size() && sp.inputs[pb.nextInput].step <= c + SAME_TIME) {
            auto const& in = sp.inputs[pb.nextInput++];
            inject(pl, holdIndex(in.button, in.p1), in.down);
        }
        return;
    }
    if (pb.controlling) endAutoPart(pl, false);
    long t = std::lround(c - n);
    if (t >= 0 && static_cast<size_t>(t) < sp.tail.size()) {
        trackDiff(now, sp.tail, static_cast<size_t>(t), pb.tailX, pb.tailY, pb.tailCam);
        return;
    }
    log::info("SP {}: {} steps after the handoff, largest difference from the original run: x {:.3f}, y {:.3f}, camera {:.3f}",
        sp.id, sp.tail.size(), pb.tailX, pb.tailY, pb.tailCam);
    pb = Playback{};
}

void takeSnapshot(PlayLayer* pl) {
    if (!smoke::takesSnapshots()) return;
    auto cp = pl->createCheckpoint();
    if (!cp) return;
    auto pressed = [](PlayerObject* p) { return p ? Pressed{p->m_jumpBuffered, p->m_stateRingJump} : Pressed{}; };
    g_rec.snapshots.push_back({g_rec.step, g_rec.clock, cp, g_rec.holds, g_rec.inputSeq,
        {pressed(pl->m_player1), pressed(pl->m_player2)}});
}

// The run after an SP's handoff, a frame a step from it by the clock (each recorded frame at the game's own rate).
void collectTail(Frame const& f) {
    auto& r = g_rec;
    auto& tail = r.tailFor->tail;
    while (tail.size() < TAIL_STEPS) {
        double at = r.tailFrom + static_cast<double>(tail.size());
        if (at > r.clock + SAME_TIME) break;
        if (std::abs(at - r.clock) < SAME_TIME || !r.tailLast) tail.push_back(f);
        else tail.push_back(between(*r.tailLast, f, (at - r.tailLastClock) / (r.clock - r.tailLastClock)));
    }
    r.tailLast = f;
    r.tailLastClock = r.clock;
    if (tail.size() >= TAIL_STEPS) r.tailFor = nullptr;
}

// Before a step: record the state the step starts from, and keep only what an SP placed now could need.
void recordStep(PlayLayer* pl) {
    auto& r = g_rec;
    auto f = captureFrame(pl);
    if (r.tailFor) collectTail(f);
    if (r.frames.empty()) r.firstStep = r.step;
    r.frames.push_back(f);
    r.times.push_back(r.clock);
    // Every CHECKPOINT_INTERVAL steps of time: at the first step at or past each multiple of it (at the game's own
    // rate the step numbers themselves).
    if (std::fmod(r.clock + SAME_TIME, CHECKPOINT_INTERVAL) < g_stepUnit) takeSnapshot(pl);
    // Frames spanning `keep` steps of time.
    double keep = std::ceil(autoPartSeconds() * STEPS_PER_SECOND) + CHECKPOINT_INTERVAL + 2;
    while (r.clock - r.times.front() > keep - 1 + SAME_TIME) {
        r.frames.pop_front();
        r.times.pop_front();
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

// "SP 2/5 (4.8%)": SP ids skip numbers once SPs get deleted, so players see positions.
std::string labelOf(std::shared_ptr<StartPoint> const& sp) {
    int i = indexOf(sp);
    if (i < 0) return std::string(text::tr(Msg::LevelStart));
    int n = static_cast<int>(g_session.sps.size());
    if (sp->name.empty()) return text::format(Msg::SPLabel, i + 1, n, sp->percent);
    return text::format(Msg::SPLabelNamed, sp->name, i + 1, n, sp->percent);
}

// "Level startpos (45.0%)": no number, so an SP's stays the one the SP list has.
std::string labelOf(Entry const& e) {
    if (e.sp || !e.startPos) return labelOf(e.sp);
    for (auto const& p : g_session.levelStartPoses) {
        if (p.object == e.startPos) return text::format(Msg::LevelStartPosLabel, p.percent);
    }
    return labelOf(nullptr);
}

std::string activeLabel() {
    return labelOf(current());
}

// What the switcher goes through: the level start, then the SPs and the level's startposes in the switcher by x, as
// other mods' switchers have them (Eclipse's, MegaHack's: the startposes by x, the level's first at the same x).
std::vector<Entry> entries() {
    auto const& s = g_session;
    std::vector<Entry> out{Entry{}};
    auto level = s.levelStartPoses.begin();
    for (auto const& sp : s.sps) {
        for (; level != s.levelStartPoses.end() && level->x <= sp->x; ++level) out.push_back({nullptr, level->object});
        out.push_back({sp});
    }
    for (; level != s.levelStartPoses.end(); ++level) out.push_back({nullptr, level->object});
    return out;
}

float xOf(Entry const& e) {
    if (e.sp) return e.sp->x;
    if (e.startPos) return e.startPos->getPositionX();
    return -std::numeric_limits<float>::infinity();
}

// A switcher that keeps the startposes it collected and goes on from the one the attempt started at (Mega Hack's)
// may pick a deleted SP's object. Sent to the level start from there, it would pick the same object again, and the
// player could never get past it. So restarts go on past it the way the switcher went from where the last attempt
// started: to the next SP or startpos of the level in the switcher that way, or the level start when there's none
// (as switchers go round). Entries between the two mean it went round the end of its list instead (Mega Hack's
// previous one from the level start is its last): then the other way.
Entry pastDeleted(StartPosObject* obj) {
    float x = obj->getPositionX();
    float from = g_session.startedAtX;
    std::vector<Entry> live;
    for (auto const& e : entries()) {
        if (!e.sp || e.sp->object) live.push_back(e);
    }
    bool ahead = x > from;
    bool between = std::any_of(live.begin(), live.end(), [&](Entry const& e) {
        float ex = xOf(e);
        return ex > std::min(x, from) && ex < std::max(x, from);
    });
    if (between) ahead = !ahead;
    Entry to;
    if (ahead) {
        auto it = std::find_if(live.begin(), live.end(), [&](Entry const& e) { return xOf(e) > x; });
        if (it != live.end()) to = *it;
    }
    else {
        auto it = std::find_if(live.rbegin(), live.rend(), [&](Entry const& e) { return xOf(e) < x; });
        if (it != live.rend()) to = *it;
    }
    log::info("A switcher picked a deleted SP's object at x {:.1f} (the last attempt from x {:.1f}): on {} to {}", x,
        from, ahead ? "ahead" : "back", labelOf(to));
    return to;
}

bool canUseKeys(PlayLayer* pl) {
    return pl && pl->m_player1 && !pl->m_isPaused && !pl->m_hasCompletedLevel && !pl->m_isPlatformer;
}

// On the pause menu itself, nothing open over it (the SP list, a rename, the settings: popups).
bool canSwitchPaused(PlayLayer* pl) {
    if (!pl || !pl->m_player1 || !pl->m_isPaused || pl->m_hasCompletedLevel || pl->m_isPlatformer) return false;
    auto scene = CCScene::get();
    if (!scene || !scene->getChildByType<PauseLayer>(0)) return false;
    for (auto child : CCArrayExt<CCNode*>(scene->getChildren())) {
        if (typeinfo_cast<FLAlertLayer*>(child)) return false;
    }
    return true;
}

bool placeSP() {
    auto pl = PlayLayer::get();
    if (!canUseKeys(pl) || pl->m_player1->m_isDead) return false;
    // It would be gone once the level is left, though it said it was placed.
    if (!storage::level()) {
        notify(NotificationIcon::Warning, storage::offMsg());
        return true;
    }
    if (g_play.sp && g_play.controlling) {
        notify(NotificationIcon::Warning, Msg::WaitAutoPart);
        return true;
    }
    auto& r = g_rec;
    // The player is in control from this step on, and from this time.
    int now = r.step;
    double nowClock = r.clock;
    int autoSteps = static_cast<int>(std::lround(autoPartSeconds() * STEPS_PER_SECOND));
    Snapshot const* snap = nullptr;
    for (auto it = r.snapshots.rbegin(); it != r.snapshots.rend(); ++it) {
        if (it->clock <= nowClock - autoSteps + SAME_TIME && it->step >= r.firstStep) {
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
    // An SP keeps its time in steps of 240 a second, whatever rate it was placed at: a frame a step from the spawn
    // till the handoff (at the game's own rate the recorded frames themselves), inputs at the steps they go in
    // before. The file is the same at any rate.
    sp->spawnStep = static_cast<int>(std::lround(snap->clock));
    int count = static_cast<int>(std::ceil(nowClock - snap->clock - SAME_TIME));
    std::vector<size_t> inputFrames;  // the frames the inputs went in before
    for (auto const& in : r.inputs) {
        if (in.seq < snap->inputSeq || in.step >= now) continue;
        auto rel = in;
        size_t frame = static_cast<size_t>(in.step - r.firstStep);
        inputFrames.push_back(frame);
        rel.step = std::min(count - 1, static_cast<int>(std::ceil(r.times[frame] - snap->clock - SAME_TIME)));
        sp->inputs.push_back(rel);
    }
    std::sort(inputFrames.begin(), inputFrames.end());
    auto inputBy = [&](size_t a, size_t b) {
        auto it = std::upper_bound(inputFrames.begin(), inputFrames.end(), a);
        return it != inputFrames.end() && *it <= b;
    };
    size_t from = static_cast<size_t>(snap->step - r.firstStep);
    for (int k = 0; k < count; k++) {
        Frame const& before = k > 0 ? sp->frames.back() : r.frames[from];
        sp->frames.push_back(frameAt(r.frames, r.times, from, snap->clock + k, before, inputBy));
    }
    sp->x = pl->m_player1->m_position.x;
    sp->percent = pl->getCurrentPercent();

    auto& sps = g_session.sps;
    auto at = std::upper_bound(sps.begin(), sps.end(), sp->x, [](float x, auto const& o) { return x < o->x; });
    sps.insert(at, sp);
    addObjectFor(pl, *sp);
    choose(sp);
    r.tailFor = sp;
    r.tailFrom = nowClock;
    r.tailLast.reset();
    g_session.unsaved.push_back(sp);
    notify(NotificationIcon::Success, Msg::Placed, activeLabel());
    log::info("SP {} placed at x {:.1f} ({:.2f}%): spawn from step {}, auto part {} steps, {} inputs, practice {}",
        sp->id, sp->x, sp->percent, snap->step, sp->handoff(), sp->inputs.size(), pl->m_isPracticeMode);
    if (std::abs((nowClock - snap->clock) - (now - snap->step)) > SAME_TIME) {
        log::info("SP {}: placed at {:.1f} steps a second ({} steps of the run since the spawn), kept at 240", sp->id,
            (now - snap->step) / (nowClock - snap->clock) * STEPS_PER_SECOND, now - snap->step);
    }
    return true;
}

void saveUnsaved() {
    for (auto const& sp : g_session.unsaved) storage::save(*sp);
    g_session.unsaved.clear();
}

// Restart from `to`, as startpos switchers do.
void restartFrom(PlayLayer* pl, Entry to) {
    // Practice checkpoints would win over the startpos in resetLevel.
    if (pl->m_isPracticeMode) pl->removeAllCheckpoints();
    choose(std::move(to), true);
    pl->resetLevel();
}

// On the pause menu: the restart at once, behind the menu, the game still paused. The player stands at the
// spawn, and the game goes on from there however it goes on.
void restartPaused(PlayLayer* pl, Entry to) {
    restartFrom(pl, std::move(to));
    // The restart took the blanks out (SPCBlanksOut): back in for as long as the menu is open.
    storage::showBlanks(true);
}

// dir -1 / +1 through entries(): the level start, the SPs (and the level's startposes) by position.
// False when the key isn't for the mod right now (a popup over the pause menu, say), so it goes on to the game.
bool switchSP(int dir) {
    auto pl = PlayLayer::get();
    bool paused = canSwitchPaused(pl);
    if (!paused && !canUseKeys(pl)) return false;
    auto list = entries();
    if (list.size() < 2) {
        notify(NotificationIcon::Warning, Msg::NoSPs);
        return true;
    }
    int n = static_cast<int>(list.size());
    auto at = std::find(list.begin(), list.end(), current());
    int i = at == list.end() ? 0 : static_cast<int>(at - list.begin());
    auto to = list[(i + dir + n) % n];
    notify(labelOf(to));
    if (paused) restartPaused(pl, to);
    else restartFrom(pl, to);
    return true;
}

// Takes an SP out of the level, its file left as it is; restarts that went to it go to what's before it in the
// switcher (the SP before it, a startpos of the level, the level start). `sp` by value: the caller's may be an
// element of the list it's erased from.
void dropSP(PlayLayer* pl, std::shared_ptr<StartPoint> sp) {
    auto& s = g_session;
    int i = indexOf(sp);
    if (i < 0) return;
    if (s.active == sp) {
        auto list = entries();
        auto at = std::find(list.begin(), list.end(), Entry{sp});
        choose(at != list.end() && at != list.begin() ? *(at - 1) : Entry{});
    }
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
        notify(NotificationIcon::Warning, g_session.activeLevel ? Msg::NotAnSP : Msg::NoSPSelected);
        return true;
    }
    removeSP(pl, sp);
    notify(NotificationIcon::Success, Msg::Deleted, sp->percent);
    restartFrom(pl, current());
    return true;
}

bool switcherOn() {
    return Mod::get()->getSettingValue<bool>("switcher");
}

bool hideLevelStartPoses() {
    return Mod::get()->getSettingValue<bool>("hide-level-startposes");
}

// The level's startposes for the switcher (Session::levelShown): what other mods' switchers collect, every object 31
// (the disabled ones too), as the level made them, before the SPs' objects come in.
void collectLevelStartPoses(PlayLayer* pl) {
    auto& s = g_session;
    for (auto obj : CCArrayExt<GameObject*>(pl->m_objects)) {
        if (obj->m_objectID != 31) continue;
        auto startPos = typeinfo_cast<StartPosObject*>(obj);
        if (!startPos || spOf(startPos) || isRetired(startPos)) continue;
        float x = startPos->getPositionX();
        s.levelStartPoses.push_back({startPos, x, startPosPercent(pl, x)});
    }
    std::stable_sort(s.levelStartPoses.begin(), s.levelStartPoses.end(), [](auto const& a, auto const& b) { return a.x < b.x; });
    // Where the level starts: the switcher goes on from there.
    s.activeLevel = levelStartPosOf(pl->m_startPosObject);
    if (!s.levelStartPoses.empty()) {
        log::info("{} startposes of the level in the switcher, the level starts at {}", s.levelStartPoses.size(),
            s.activeLevel ? fmt::format("x {:.1f}", s.activeLevel->getPositionX()) : std::string("0%"));
    }
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
        case Notice::OutOfOrder: notify(warn, Msg::OutOfOrder); break;
        case Notice::CantMoveAside: notify(warn, Msg::CantMoveAside); break;
        case Notice::CantMoveLowDetail: notify(warn, Msg::CantMoveLowDetail); break;
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

bool onLevelStartPos() {
    return !g_session.active && g_session.activeLevel;
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

double autoPartSeconds() {
    return ::autoPartSeconds();
}

std::pair<double, double> autoPartRange() {
    double lo = 0.2, hi = 2.0;
    if (auto setting = geode::cast::typeinfo_pointer_cast<FloatSettingV3>(Mod::get()->getSetting("auto-part-length"))) {
        lo = setting->getMinValue().value_or(lo);
        hi = setting->getMaxValue().value_or(hi);
    }
    return {lo, hi};
}

void setAutoPartSeconds(double seconds) {
    auto [lo, hi] = autoPartRange();
    seconds = std::clamp(seconds, lo, hi);
    if (std::abs(seconds - Mod::get()->getSettingValue<double>("auto-part-length")) < 1e-6) return;
    Mod::get()->setSettingValue<double>("auto-part-length", seconds);
    log::info("Auto part length: {:.2f} s, from the SP list", seconds);
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
        // Played on from the pause menu some way not caught (SPCBlanksOut).
        if (storage::blanksShown()) {
            log::warn("Playing with the blanks of the level's startposes in m_objects: the startposes are back");
            storage::showBlanks(false);
        }
        g_stepDt = dt;
        g_stepHalf = isHalfTick;
        g_stepLast = isLastTick;
        g_stepUnit = stepUnit(dt);
        if (pl->m_player1 && !pl->m_isPlatformer) {
            // The smoke test plays like a player would: before the mod looks at the step.
            smoke::beforeStep(pl);
            if (g_play.sp) playStep(pl, g_stepUnit);
            smoke::afterAutoPartStep(pl);
            recordStep(pl);
        }
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        g_rec.step++;
        g_rec.clock += g_stepUnit;
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        if (!isPlayLayer()) return GJBaseGameLayer::handleButton(down, button, isPlayer1);
        int index = holdIndex(button, isPlayer1);
        if (g_play.sp && g_play.controlling && !g_play.injecting) {
            // The auto part owns the buttons: remember what the player holds for the handoff.
            g_play.userHolds[index] = down;
            // Unless control isn't blocked: then the first jump press ends the auto part and goes in. From inside
            // this hook a handleButton call goes to the hooks after this one, so the buttons are recorded here.
            // Left and right don't end it: SPs are for classic levels, where they do nothing, yet they come in
            // there too (Mega Hack's switcher on A and D, Click Between Frames sends them in any level); they are
            // handed to the game with the rest at the handoff, as with control blocked.
            if (down && !blockInput() && button == static_cast<int>(PlayerButton::Jump)) {
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

// Between a restart from an SP and its first step the game switches no player's form: another mod does (Mega Hack's
// Smart StartPos, with the form it works out for the startpos: Storming Summit's ship where the run was a cube, the
// ship's camera height left in the game's state). The SP's checkpoint has the form already: it stays.
bool othersSwitchForm(PlayerObject* p, char const* what) {
    auto pl = PlayLayer::get();
    if (!pl || !g_play.sp || !g_play.controlling || g_play.step != 0 || (p != pl->m_player1 && p != pl->m_player2)) {
        return false;
    }
    if (!g_play.formKept) {
        g_play.formKept = true;
        log::info("SP {}: another mod switches the player's form before the first step ({}): it stays as in the SP's checkpoint",
            g_play.sp->id, what);
    }
    return true;
}

#define SPC_FORM_KEPT(name)                                                                                       \
    void name(bool enable, bool noEffects) {                                                                      \
        if (othersSwitchForm(this, #name)) return;                                                                \
        PlayerObject::name(enable, noEffects);                                                                    \
    }

class $modify(SPCPlayer, PlayerObject) {
    SPC_FORM_KEPT(toggleFlyMode)
    SPC_FORM_KEPT(toggleRollMode)
    SPC_FORM_KEPT(toggleBirdMode)
    SPC_FORM_KEPT(toggleDartMode)
    SPC_FORM_KEPT(toggleRobotMode)
    SPC_FORM_KEPT(toggleSpiderMode)
    SPC_FORM_KEPT(toggleSwingMode)
    SPC_FORM_KEPT(togglePlayerScale)

    void switchedToMode(GameObjectType type) {
        if (othersSwitchForm(this, "switchedToMode")) return;
        PlayerObject::switchedToMode(type);
    }

    bool pushButton(PlayerButton button) {
        // A button held since before an SP's checkpoint (holdAgain): held, and nothing else a press does.
        if (g_play.holdOnly) {
            m_holdingButtons[static_cast<int>(button)] = true;
            return false;
        }
        return PlayerObject::pushButton(button);
    }
};

#undef SPC_FORM_KEPT

// Other mods find the level's startposes in PlayLayer::addObject as the level creates them (switchers collect
// them there), or later in m_objects (Blitzkrieg, from the pause menu). Hidden from them (the setting): the
// other mods' hooks get a blank object instead, and the game, past them all, the startpos, so the level starts
// where it did. In the pause menu the blanks take the startposes' places in m_objects too (storage::hide).
class $modify(SPCHideStartPoses, PlayLayer) {
    static void onModify(auto& self) {
        // Before every other mod's hook: what Stub is for, changing what the hooks after it get.
        if (auto res = self.setHookPriority("PlayLayer::addObject", Priority::Stub); res.isErr()) {
            log::error("Can't set the priority of the PlayLayer::addObject hook: {}", res.unwrapErr());
        }
    }

    void addObject(GameObject* obj) {
        auto& s = g_session;
        auto startPos = s.hiding && obj && obj->m_objectID == 31 ? typeinfo_cast<StartPosObject*>(obj) : nullptr;
        auto blank = startPos ? storage::blankFor(startPos) : nullptr;
        if (!blank) return PlayLayer::addObject(obj);
        // By index: another mod's hook may add a startpos itself on the way, one more.
        size_t i = s.hidden.size();
        s.hidden.push_back({startPos, blank, true, false});
        s.handing++;
        PlayLayer::addObject(blank);
        s.handing--;
        s.hidden[i].handing = false;
        // Another mod's hook kept it from the game (or went past the hook below).
        if (!s.hidden[i].handed) {
            log::warn("A startpos of the level at x {:.1f} didn't get to the game past other mods", startPos->getPositionX());
        }
    }
};

// Past every other mod's hook, right before the game: the startpos the blank stands for.
class $modify(SPCHandStartPoses, PlayLayer) {
    static void onModify(auto& self) {
        if (auto res = self.setHookPriority("PlayLayer::addObject", Priority::Replace); res.isErr()) {
            log::error("Can't set the priority of the PlayLayer::addObject hook: {}", res.unwrapErr());
        }
    }

    void addObject(GameObject* obj) {
        auto& s = g_session;
        for (size_t i = s.hidden.size(); s.handing > 0 && i-- > 0;) {
            auto& h = s.hidden[i];
            if (h.handing && h.blank.data() == obj) {
                obj = h.startPos.data();
                h.handed = true;
                break;
            }
        }
        PlayLayer::addObject(obj);
    }
};

// The SPs' objects for other mods' switchers as the level finishes making its objects (madeEarly). Past every
// other mod's hook, right after the game: when theirs go on after the game's, these are there.
class $modify(SPCEarlyObjects, PlayLayer) {
    static void onModify(auto& self) {
        if (auto res = self.setHookPriority("PlayLayer::createObjectsFromSetupFinished", Priority::Replace); res.isErr()) {
            log::error("Can't set the priority of the PlayLayer::createObjectsFromSetupFinished hook: {}", res.unwrapErr());
        }
    }

    void createObjectsFromSetupFinished() {
        PlayLayer::createObjectsFromSetupFinished();
        madeEarly(this);
    }
};

// Blitzkrieg looks for startposes in m_objects from the pause menu: the blanks are there for as long as it's
// open. Not while playing: the game goes through m_objects itself, to restart (moving objects between their
// sections, which the blanks aren't in), to make checkpoints, to quit. Out again before every other mod's hook
// on the ways out of the menu, and when the level goes (the game goes through m_objects as it does).
class $modify(SPCBlanksPause, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        if (PlayLayer::get()) storage::showBlanks(true);
    }

    void goEdit() {
        storage::showBlanks(false);
        PauseLayer::goEdit();
    }
};

class $modify(SPCBlanksOut, PlayLayer) {
    static void onModify(auto& self) {
        for (auto name : {"PlayLayer::resetLevel", "PlayLayer::resume", "PlayLayer::resumeAndRestart",
                 "PlayLayer::togglePracticeMode", "PlayLayer::onQuit", "PlayLayer::PlayLayer"}) {
            if (auto res = self.setHookPriority(name, Priority::Stub); res.isErr()) {
                log::error("Can't set the priority of the {} hook: {}", name, res.unwrapErr());
            }
        }
    }

    void resetLevel() {
        storage::showBlanks(false);
        PlayLayer::resetLevel();
    }

    void resume() {
        storage::showBlanks(false);
        PlayLayer::resume();
    }

    void resumeAndRestart(bool fromStart) {
        storage::showBlanks(false);
        PlayLayer::resumeAndRestart(fromStart);
    }

    void togglePracticeMode(bool practiceMode) {
        storage::showBlanks(false);
        PlayLayer::togglePracticeMode(practiceMode);
    }

    void onQuit() {
        storage::showBlanks(false);
        PlayLayer::onQuit();
    }

    // Left some other way too (a new scene in its place). Not onExit: the game calls that to pause.
    void destructor() {
        storage::unhide(this);
        storage::objectsMade(this);
        PlayLayer::~PlayLayer();
    }
};

class $modify(SPCPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        g_session = Session{};
        g_session.hiding = hideLevelStartPoses();
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
            storage::beforeCreatingObjects(this);
        }
        else {
            // Between the batches, as other mods do (Globed).
            smoke::betweenBatches(this);
            storage::beforeBatch(this);
        }
        PlayLayer::processCreateObjectsFromSetup();
        storage::afterBatch(this);
    }

    // Right in the batch that made the last objects, or in a frame of its own: the level's counter is back for
    // what the game (and other mods) add after the level's objects, the anticheat spike first.
    void createObjectsFromSetupFinished() {
        storage::objectsMade(this);
        PlayLayer::createObjectsFromSetupFinished();
    }

    void setupHasCompleted() {
        storage::objectsMade(this);
        // The level has created its objects and picked its own startpos, if it has one. The SPs' objects,
        // added from here on, aren't hidden.
        g_session.levelShown = !g_session.hiding;
        g_session.hiding = false;
        g_session.levelStartPos = m_startPosObject;
        g_session.levelTestMode = m_isTestMode;
        if (m_startPosObject) g_session.startedAtX = m_startPosObject->getPositionX();
        PlayLayer::setupHasCompleted();
        if (g_session.levelShown) collectLevelStartPoses(this);
        auto loaded = storage::load(this);
        // After load(), which takes the objects' prints.
        std::vector<std::pair<GameObject*, GameObject*>> blanks;
        for (auto const& h : g_session.hidden) blanks.push_back({h.startPos.data(), h.blank.data()});
        storage::hide(this, blanks);
        size_t hiddenCount = g_session.hidden.size();
        g_session.hidden.clear();
        smoke::loaded(this, loaded);
        g_session.sps = std::move(loaded.sps);
        g_session.nextId = loaded.nextId;
        // After the level's objects and its anticheat spike: theirs keep their places in m_objects (and so
        // their unique IDs) whatever the SPs. Each SP takes the object made for it early (madeEarly).
        for (auto const& sp : g_session.sps) addObjectFor(this, *sp);
        notifyLoaded(loaded, g_session.sps.size());
        if (g_session.levelShown) levelStartPosNotice(g_session.levelStartPoses.size(), false);
        else levelStartPosNotice(hiddenCount, true);
        smoke::ready(this);
    }

    // The game's visibility pass (updateVisibility, in here) may put the SPs' objects back at their start
    // position, 0,0 (see placeObjects): after it, they're put where they stand again.
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        placeObjects();
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
        s.startedAtX = m_startPosObject ? m_startPosObject->getPositionX() : -std::numeric_limits<float>::infinity();
        if (s.lastCheckpoint) s.lastCheckpointRetains = s.lastCheckpoint->retainCount();
        smoke::restarted(this);
    }

    float getCurrentPercent() {
        // An attempt from an SP starts at its spawn, an auto part before the SP. Until its first step, mods
        // that note where an attempt starts (Blitzkrieg, Mega Hack's best run, in resetLevel) get the SP's percent:
        // the game's where the SP was placed, which the game gives again there in the attempt (it goes on from the
        // SP's checkpoint). Not one counted from x: in levels that go back and forth that's somewhere else.
        // Not the game's own progress bar and percent: they show where the player is, the spawn.
        if (g_gameShowsPercent) return PlayLayer::getCurrentPercent();
        auto sp = g_session.spawning;
        if (!sp && g_play.sp && g_play.controlling && g_play.step == 0) sp = g_play.sp;
        if (sp && sp->object && m_checkpointArray->count() == 0) return sp->percent;
        return PlayLayer::getCurrentPercent();
    }

    // The game's bar and label at a restart from an SP: where the player is, a bit before the SP (Fiberglass: 94%
    // at the spawn of an SP at 95.5%), not the SP's percent that other mods get (getCurrentPercent).
    void updateProgressbar() {
        g_gameShowsPercent = true;
        PlayLayer::updateProgressbar();
        g_gameShowsPercent = false;
    }

    void onQuit() {
        smoke::quitting();
        // Left while the level was making its objects.
        storage::objectsMade(this);
        saveUnsaved();
        storage::leave();
        // Checkpoints point into the level's objects: let go of them while the level still exists.
        g_play = Playback{};
        g_rec = Recording{};
        g_session = Session{};
        PlayLayer::onQuit();
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        // Only the level's players: another mod's own (Eclipse's Show Trajectory runs a copy ahead of the player)
        // die the way the game has them die, the auto part or not.
        if (player != m_player1 && player != m_player2) return PlayLayer::destroyPlayer(player, object);
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
