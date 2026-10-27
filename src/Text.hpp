#pragma once

#include <Geode/Geode.hpp>

#include <string>
#include <string_view>

// What the mod says to the player, in English or Russian. The game's fonts have no Cyrillic letters,
// so Russian text uses the mod's own font (Ubuntu Bold, built by the Geode CLI from mod.json).
namespace text {
    enum class Msg {
        WaitAutoPart,
        TooEarly,         // {seconds}
        Placed,           // {SP label}
        NoSPs,
        NoSPSelected,
        Deleted,          // {percent}
        LevelStart,
        SPLabel,          // {position} {count} {percent}
        SPLabelNamed,     // {name} {position} {count} {percent}
        Loaded,           // {count}
        Failed,           // {count}
        LevelChanged,
        CantSave,
        CantMoveAside,
        ListTitle,
        ListEmpty,        // {key}
        ListLevelStart,
        ConfirmDelete,
        RenameTitle,
        NamePlaceholder,
        Save,
        Count,
    };

    bool russian();
    std::string_view tr(Msg msg);

    template <class... Args>
    std::string format(Msg msg, Args&&... args) {
        return fmt::format(fmt::runtime(tr(msg)), std::forward<Args>(args)...);
    }

    // Fonts for text that may be in Russian (or in a name the player typed).
    char const* font();       // instead of bigFont.fnt
    char const* titleFont();  // instead of goldFont.fnt
    char const* fontFor(std::string const& str, bool title = false);
    cocos2d::CCLabelBMFont* label(std::string const& str, bool title = false);
}
