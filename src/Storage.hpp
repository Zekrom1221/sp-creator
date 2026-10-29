#pragma once

#include "StartPoint.hpp"

#include <ctime>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Saving SPs between sessions. <mod save dir>/<root>/<level key>/ ("online-<ID>", "-ldm" added for Low
// Detail Mode) holds level.json (the level version
// the SPs belong to), names.json (the names players gave them), objects.bin (prints of the version's
// objects, to move the SPs over when the level is updated) and one sp-<id>.spc file per SP: its
// auto part and its checkpoint, the checkpoint written with PersistenceAPI in the field order Practice
// Checkpoint Permanence uses.
namespace storage {
    // What load() has to tell the player.
    enum class Notice {
        None,
        Loaded,         // `sps` loaded
        Failed,         // `failed` SPs couldn't be loaded
        LevelChanged,   // the SPs were made for another version of the level and set aside (`movedOver`)
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
        // With LevelChanged: the set aside SPs could be moved over to this version (their objects were
        // known), `moved` of them were (loaded: `sps`); `notMoved` holds the others' percents (NaN: unknown).
        bool movedOver = false;
        int moved = 0;
        std::vector<float> notMoved;
    };

#ifdef SPC_SMOKE
    // Folder under the mod save dir (UTF-8, "levels"); the smoke test keeps its SPs apart.
    void setRoot(std::string_view utf8);
#endif
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
    // False if its file couldn't be deleted (in the log).
    bool remove(StartPoint const& sp);
#ifdef SPC_SMOKE
    // Smoke test: deletes the level's SP folder and its versions.
    void removeAll();
#endif
    // The level is left: its folder goes if nothing is kept for it, no SPs and no versions (with versions,
    // their "for an older level version" comes from its level.json). Versions without SPs go too.
    void leave();

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
    struct Aside {
        std::vector<std::shared_ptr<StartPoint>> moved;  // out of the level's folder: in a version now
        bool existing = false;  // a version had them already, and is the newest now
    };
    // Moves the files of SPs of the level into a folder of their own (with their names), a version of
    // them, out of the way of imported ones. If a version that fits holds just these SPs already, byte for
    // byte and with the same names, their files are deleted instead, and that version becomes the newest.
    Aside setAside(std::vector<std::shared_ptr<StartPoint>> const& sps);

    // SPs set aside in a folder next to the level's: by "Replace all" (setAside), or by load() for another
    // version of the level. Kept until the player deletes them.
    struct Version {
        std::filesystem::path dir;
        bool replaced = false;  // by "Replace all"; otherwise for another level version
        std::time_t time = 0;
        int sps = 0;
        uintmax_t bytes = 0;    // all its files
        bool fits = false;      // made for the open level's version: can be brought back
        bool movable = false;   // made for another, but its SPs can be moved over (moveVersion)
        std::string name;       // given by the player; empty: none
    };
    // The open level's versions that hold SPs, newest first.
    std::vector<Version> versions();
    struct VersionSP {
        geode::ByteVector file;
        std::string name;
    };
    // A version's SP files (to pass through readHeader), with their names. Only of a version that fits.
    geode::Result<std::vector<VersionSP>> readVersion(Version const& version);
    struct MovedVersion {
        std::vector<VersionSP> sps;    // for the open level version, as readVersion gives them
        std::vector<float> notMoved;   // percents of those that refer to objects the update took away
    };
    // A version for another level version (`movable`): its SPs read with the objects they refer to found
    // among the open version's, by the prints saved with them.
    geode::Result<MovedVersion> moveVersion(Version const& version);
#ifdef SPC_SMOKE
    // Smoke test: the objects (indices in m_objects) the checkpoint of each SP of the open level refers to.
    struct References {
        int id = 0;
        std::vector<unsigned int> all;
        std::vector<unsigned int> states;  // those with an object state (moved, used, animated) among them
    };
    std::vector<References> referencesOfSPs();
#endif
    bool removeVersion(Version const& version);
    // An empty name takes the version's name away.
    bool renameVersion(Version const& version, std::string const& name);

    // The storage manager (StorageList.cpp): every level's SPs and versions, a level open or not.
    struct StoredLevel {
        std::string key;               // its folder's name: online-123, main-3-ldm, editor-...
        std::string name;              // from level.json; the key without one
        bool lowDetail = false;        // SPs for Low Detail Mode
        bool open = false;             // the open level: its SPs stay
        std::filesystem::path dir;     // its own folder (it may be gone, with versions left)
        int sps = 0;
        uintmax_t spBytes = 0;         // all files of its own folder, 0 when it holds nothing of the player's
        std::vector<Version> versions; // newest first; `fits` against its own folder's level.json
        uintmax_t versionBytes = 0;
        uintmax_t bytes() const { return spBytes + versionBytes; }
    };
    // Levels with SPs or versions, the biggest first. Folders with nothing of the player's in them (only what
    // the mod writes when a level is entered, or a version made empty) are left out: they go when their
    // level is left (leave()). Nothing is deleted here.
    std::vector<StoredLevel> storedLevels();
    // A level's own folder, its SPs with it; never the open level's.
    bool removeLevelSPs(StoredLevel const& level);
    // With the level's last version, its own folder goes too if it holds no SPs, but the open level's.
    bool removeStoredVersion(StoredLevel const& level, Version const& version);
}
