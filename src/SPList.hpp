#pragma once

#include "Core.hpp"
#include "Transfer.hpp"

// The SP list in the pause menu (SPList.cpp).
namespace splist {
    void open(PauseLayer* pause);
    // The smoke test looks at the rename popup and the export and import lists too.
    void openRename(core::SP const& sp);
    void openExport();
    void openImport(transfer::Pack pack);
}
