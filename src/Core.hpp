#pragma once

#include "StartPoint.hpp"

#include <memory>
#include <string>
#include <vector>

// What the SP list in the pause menu (SPList.cpp) and export / import (Transfer.cpp) use of the mod's
// core (main.cpp).
namespace core {
    using SP = std::shared_ptr<StartPoint>;

    // The open level's SPs, by position.
    std::vector<SP> const& sps();
    // The SP restarts go to, null for the level start.
    SP const& active();
    // "SP 2/5 (4.8%)", or with its name; "Level start" for null.
    std::string label(SP const& sp);
    // Restarts go to `sp` (null: the level start) from now on.
    void select(SP const& sp);
    // Before restarting from the pause menu: practice checkpoints would win over the SP.
    void prepareRestart();
    // Deletes the SP without restarting; restarts go to the one before it if they went to it.
    void remove(SP const& sp);
    struct RemoveResult {
        int removed = 0;
        int left = 0;           // couldn't be set aside or deleted: still in the level
        bool saved = false;     // they went to a version
        bool existing = false;  // saved: a version had them already (the newest now)
    };
    // Deletes these SPs the same way, saved as a version first if `save` (storage::setAside).
    RemoveResult remove(std::vector<SP> const& sps, bool save);
    void rename(SP const& sp, std::string name);

    // Changes with every level entered; 0 out of levels. Tells whether a file dialog came back to the
    // level it was opened in.
    int session();
    // The mod's one notification.
    void notify(geode::NotificationIcon icon, std::string const& text);
    // Writes the SPs not written yet.
    void saveAll();

    struct Incoming {
        geode::ByteSpan file;  // checked with storage::readHeader
        std::string name;
    };
    // What importing does with the level's own SPs.
    enum class Replace {
        No,           // they stay, the new ones come in beside them
        SaveVersion,  // they go to a version (storage::setAside), to bring back from the SP list
        Delete,
    };
    struct ImportResult {
        int added = 0, failed = 0;
        int notMoved = 0;  // replacing: SPs of the level that couldn't be set aside or deleted
        bool saved = false;  // replacing: the level's SPs went to a version
        bool existing = false;  // saved: a version had them already (the newest now)
    };
    // Adds SPs from files to the level; replacing, all of the level's own SPs go once some of the new
    // ones are in. If some failed, they go to a version even when deleting them was asked.
    ImportResult import(std::vector<Incoming> const& files, Replace replace);
}
