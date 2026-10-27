#pragma once

#include "StartPoint.hpp"

#include <memory>
#include <string>
#include <vector>

// What the SP list in the pause menu (SPList.cpp) uses of the mod's core (main.cpp).
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
    void rename(SP const& sp, std::string name);
}
