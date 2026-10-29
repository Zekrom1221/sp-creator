#pragma once

#include <Geode/Geode.hpp>

#include <ctime>
#include <string>
#include <string_view>
#include <vector>

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
        SPsWithLowDetail,
        SPsWithoutLowDetail,
        // Export and import.
        Export,
        Import,
        ExportTitle,
        ImportTitle,
        ImportWarning,
        Add,
        Replace,
        AlreadyHere,
        Repeated,
        NearYours,        // {position} {percent}
        NothingSelected,
        PickedAlreadyHere,
        Exported,         // {count}
        ExportFailed,
        Imported,         // {count}
        ImportedSaved,    // {count}
        ImportedInVersion, // {count}
        ImportFailed,     // {count}
        ImportFailedSaved, // {count}
        NotSetAside,      // {imported} {not set aside}
        NotDeleted,       // {imported} {not deleted}
        AllAlreadyHere,
        NotAPack,
        NewerFormat,
        PackDamaged,
        PackTooBig,
        OtherLevel,       // {level name}
        OtherVersion,
        OtherGame,        // {game version}
        ForLowDetail,
        ForFullDetail,
        FileType,
        // "Replace all" and versions of the level's SPs.
        ReplaceQuestion,  // {count}
        ReplaceHint,
        SaveAndReplace,
        DeleteAndReplace,
        // Deleting picked SPs.
        Delete,
        DeleteTitle,
        DeleteQuestion,   // {count}
        SaveAndDelete,
        SPsDeleted,       // {count}
        SPsDeletedSaved,  // {count}
        SPsDeletedInVersion, // {count}
        SPsNotSetAside,   // {deleted} {not set aside}
        SPsNotDeleted,    // {deleted} {not deleted}
        Versions,
        VersionsTitle,
        VersionsTotal,    // {count} {size}
        VersionsEmpty,
        VersionReplaced,
        VersionOld,
        VersionBeforeDelete,
        VersionInfo,      // {count} {size}
        VersionDeleteOnly,
        Open,
        VersionTitle,     // {date}
        VersionHere,
        VersionDeleted,
        VersionNotDeleted,
        VersionUnreadable,
        RenameVersion,
        VersionNotRenamed,
        // Moving SPs over to a new level version.
        LevelUpdated,     // {moved}
        NotMoved,         // {count} {percents}
        MoveOver,
        VersionMovable,
        VersionMovedOver, // {count}
        NoneMoved,        // {percents}
        // The storage manager.
        StorageTitle,
        StorageTotal,     // {levels} {size}
        StorageEmpty,
        StorageLevelInfo, // {size} {SPs} {versions}
        StorageOpenLevel,
        StorageLevelTotal, // {size}
        StorageCurrent,   // {count} {size}
        StorageOtherVersion,
        SelectAll,
        DeleteVersions,
        DeleteEntirely,
        DeletePicked,
        StorageConfirm,   // {size}
        StorageDeleted,   // {size}
        StorageNotAll,    // {size} {not deleted}
        StorageNothing,
        Kilobytes,        // {size}
        Megabytes,        // {size}
        Count,
    };

    bool russian();
    std::string_view tr(Msg msg);

    template <class... Args>
    std::string format(Msg msg, Args&&... args) {
        return fmt::format(fmt::runtime(tr(msg)), std::forward<Args>(args)...);
    }

    // "1.7 MB", "120 KB"; "2026-10-28 14:05" (in Russian "28.10.2026 14:05"), local time.
    std::string size(uintmax_t bytes);
    std::string date(std::time_t time);
    // Where SPs are: "12%, 40%, 81%", the first few and "..." for the rest; NaN: "?".
    std::string percents(std::vector<float> const& percents);

    // Fonts for text that may be in Russian (or in a name the player typed).
    char const* font();       // instead of bigFont.fnt
    char const* titleFont();  // instead of goldFont.fnt
    char const* fontFor(std::string const& str, bool title = false);
    cocos2d::CCLabelBMFont* label(std::string const& str, bool title = false);
}
