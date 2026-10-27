#pragma once

#include "StartPoint.hpp"

#include <memory>
#include <string>
#include <vector>

// Saving SPs between sessions. <mod save dir>/<root>/<level key>/ holds level.json (the level version
// the SPs belong to), names.json (the names players gave them) and one sp-<id>.spc file per SP: its
// auto part and its checkpoint, the checkpoint written with PersistenceAPI in the field order Practice
// Checkpoint Permanence uses.
namespace storage {
    // What load() has to tell the player.
    enum class Notice {
        None,
        Loaded,         // `sps` loaded
        Failed,         // `failed` SPs couldn't be loaded
        LevelChanged,   // the SPs were made for another version of the level and set aside
        CantSave,       // SPs of this level can't be saved
        CantMoveAside,  // SPs of another version couldn't be set aside: SPs are off in this level
    };

    struct Loaded {
        std::vector<std::shared_ptr<StartPoint>> sps;  // sorted by x
        int nextId = 1;                                // above every SP file of the level, loaded or not
        Notice notice = Notice::None;
        int failed = 0;
    };

    // Folder under the mod save dir (UTF-8, "levels"); the smoke test keeps its SPs apart.
    void setRoot(std::string_view utf8);
    // Right before the level creates its objects (see Storage.cpp).
    void beforeCreatingObjects();
    // Once the level has created its objects.
    Loaded load(PlayLayer* pl);
    // Gives an object the mod adds to the level after its own objects the unique ID of its place in
    // m_objects, like the level's objects have.
    void numberObject(PlayLayer* pl, GameObject* obj);
    // Do nothing in a level whose SPs can't be saved; failures go to the log.
    bool save(StartPoint const& sp);
    void saveNames(std::vector<std::shared_ptr<StartPoint>> const& sps);
    void remove(StartPoint const& sp);
    // Smoke test: deletes the level's SP folder.
    void removeAll();
}
