#pragma once

#include <Geode/Geode.hpp>

#include "Storage.hpp"

#include <string>

// Where the smoke test comes into the gameplay code (main.cpp). Development builds have it: built with
// SPC_SMOKE (build.bat smoke), main.cpp takes in Smoke.inl, which defines these. A release build has no
// test, and they do nothing.
namespace smoke {

#ifdef SPC_SMOKE

bool takesOver();     // a press ends the auto part whatever the setting says
bool keepsPlaying();  // the game doesn't pause when its window goes to the background
bool takesSnapshots();  // the mod's hidden checkpoints (a test turns them off to see what they do to a level)
void noticed(std::string const& text);
void betweenBatches(PlayLayer* pl);                           // the level makes its objects over several frames
void beforeStep(PlayLayer* pl);                               // plays like a player would
void loaded(PlayLayer* pl, storage::Loaded const& loaded);   // right after the level's SPs are read
void ready(PlayLayer* pl);                                    // once they're in the level
void restarted(PlayLayer* pl);
bool hit(PlayLayer* pl, PlayerObject* player, GameObject* object);  // true: noclip, no death
void died(PlayLayer* pl);
void quitting();

#else

inline bool takesOver() { return false; }
inline bool keepsPlaying() { return false; }
inline bool takesSnapshots() { return true; }
inline void noticed(std::string const&) {}
inline void betweenBatches(PlayLayer*) {}
inline void beforeStep(PlayLayer*) {}
inline void loaded(PlayLayer*, storage::Loaded const&) {}
inline void ready(PlayLayer*) {}
inline void restarted(PlayLayer*) {}
inline bool hit(PlayLayer*, PlayerObject*, GameObject*) { return false; }
inline void died(PlayLayer*) {}
inline void quitting() {}

#endif

}
