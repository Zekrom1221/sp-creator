#pragma once

#include "Core.hpp"

// The SP list in the pause menu (SPList.cpp).
namespace splist {
    void open(PauseLayer* pause);
    // The smoke test looks at the rename popup too.
    void openRename(core::SP const& sp);
}
