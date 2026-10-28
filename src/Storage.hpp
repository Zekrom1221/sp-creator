#pragma once

#include "StartPoint.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

// Saving SPs between sessions. <mod save dir>/<root>/<level key>/ ("online-<ID>", "-ldm" added for Low
// Detail Mode) holds level.json (the level version
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
        // None here, but some placed with Low Detail Mode on / off, which only fit that mode.
        SPsWithLowDetail,
        SPsWithoutLowDetail,
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

    // Export and import (Transfer.cpp), in the open level.
    struct LevelInfo {
        std::string key;  // the level's SP folder
        std::string name;
        matjson::Value identity;  // as in level.json
    };
    // Empty where SPs aren't saved.
    std::optional<LevelInfo> level();
    // Were SPs of this identity (LevelInfo::identity) made for the open level's version, game, platform?
    bool fits(matjson::Value const& identity);
    // The SP's file as saved (write SPs not saved yet first).
    geode::Result<geode::ByteVector> fileOf(StartPoint const& sp);
    // An SP file, checked as far as it can be without reading its checkpoint: whole, of this format and
    // of the open level and version.
    struct Header {
        float x = 0, percent = 0;
        uint64_t content = 0;  // the same SP has the same, whatever its id
    };
    geode::Result<Header> readHeader(geode::ByteSpan file);
    // Content of the SP's saved file, as in Header; 0 if it can't be read.
    uint64_t contentOf(StartPoint const& sp);
    // SP files (passed through readHeader) loaded and saved under ids from `nextId` on: an SP per file,
    // null where it didn't work (in the log). Reads the checkpoints, which PersistenceAPI doesn't check.
    std::vector<std::shared_ptr<StartPoint>> add(std::vector<geode::ByteSpan> const& files, int& nextId);
    // Moves the files of SPs of the level into a folder of their own (with their names), out of the
    // way of imported ones: those it moved.
    std::vector<std::shared_ptr<StartPoint>> setAside(std::vector<std::shared_ptr<StartPoint>> const& sps);
}
