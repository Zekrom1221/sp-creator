<img src="logo.png" width="128" align="right" alt="SP Creator logo">

# SP Creator

A [Geode](https://geode-sdk.org) mod for Geometry Dash 2.2081 (Windows).

[По-русски](README.ru.md)

Place your own startpos (SP) in any level while you play: online levels, main levels and your own. Starting
from it replays a short auto part (0.6 s by default) before you take control, so you enter the hard part at
full speed and with the right camera, the way you'd get there from the level start. The auto part plays back your
own run, and you can't die in it.

Attempts from your SPs count in the attempt counter, like attempts from any startpos, but they don't save a new
best or a completion.

**This is a beta.** Found a bug, or have an idea? [Open an issue](https://github.com/Zekrom1221/sp-creator/issues).

## Installing

1. Install [Geode](https://geode-sdk.org) for Geometry Dash 2.2081.
2. Download `zekrom.sp-creator.geode` from [Releases](https://github.com/Zekrom1221/sp-creator/releases).
3. Put it into the `geode/mods` folder in the game's folder (in Steam: right-click Geometry Dash → Manage →
   Browse local files), and start the game.

The mod needs two other mods: PersistenceAPI (by sabe) and Node IDs. If Geode says one of them is missing,
install it from Geode's mod list.

## Keys

| Key | What it does |
| --- | --- |
| `V` | Place an SP where you are |
| `,` and `.` | Previous / next SP (or the level start) |
| `Delete` | Delete the SP you start from |

The **SP** button in the pause menu lists your SPs: play from one, rename, delete one or several at once. The
keys, the auto part length, whether your clicks in the auto part count, whether other mods see the level's own
startposes, and the language are in the mod's settings.

Only classic levels: platformer levels have no SPs.

## Your SPs

- SPs are saved between sessions, for each level. They only fit the level version, game version and Low Detail
  Mode they were made with.
- **Sharing.** In the SP list, **Export** saves the SPs you pick to a `.spcreator` file, and **Import** adds
  the SPs of such a file to the level, or puts them in place of yours (yours are saved as a version or
  deleted, as you choose). Import files only from people you trust. A `.spcreator` file is only data: a file
  that asks to be run, or comes as an `.exe`, is not one.
- **Versions** in the SP list holds the SPs put aside: when replaced by an import, deleted with **Save and
  delete**, or when the level was updated. Open one to bring its SPs back, rename it, or delete it. Nothing is
  deleted on its own.
- **Level updates.** When a level is updated, your SPs are moved over to the new version. One that depends
  on objects the update took away stays in **Versions** only: the message says where it was, so you can place
  it again.
- **Your own levels.** If you change the objects of your own level in the editor (move, add or delete even one),
  its SPs aren't moved over: the level starts with no SPs, and the old ones stay in **SP storage**, where you can
  delete them. Saving it without changes keeps its SPs.
- **SP storage** in the mod's settings lists the SPs and versions of all levels, the biggest first, to delete
  what you don't need.

## With other mods

- **The level's own startposes** are hidden from other mods, so their switchers and Blitzkrieg have only your
  SPs. The level still starts where it does. To see them in other mods too, turn off *Hide the level's
  startposes* in the settings, then enter the level again.
- **Startpos switchers** (Startpos Switcher, Mega Hack) list your SPs. An SP placed in the current visit goes
  to the end of their list until you enter the level again. After deleting or replacing SPs, their list may
  keep a few extra places until then: those take you to the level start.
- **Mega Hack**: on rated levels its switcher is off unless you turn on *Allow Rated* in the StartPos Switcher
  options. *Smart StartPos* can stay on: SPs start the way you placed them.
- **Blitzkrieg** makes stages of your SPs. After adding or deleting SPs, create a new profile with *Bind
  profile* on: editing a profile keeps its old startposes.

## Known issues

- Where the gameplay of a level is rotated or goes back, SPs are in the order of where they stand, not of their
  percent: there the SP list and the `,` and `.` keys can go back and forth. Mega Hack's switcher orders them
  the same way.

## Building

With the [Geode SDK](https://docs.geode-sdk.org) 5.11.2 (`GEODE_SDK` set), Visual Studio with its C++ tools,
CMake and Ninja:

```
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSPC_SMOKE=OFF
cmake --build build
```

`build.bat` does the same with the paths of the author's PC, and installs the mod into the game.
`-DSPC_SMOKE=ON` (`build.bat smoke`) builds in a smoke test for development; release builds don't have it.

## License

Copyright (C) 2026 Zekrom. SP Creator is free software under the GNU GPL v3: see [LICENSE](LICENSE).

The mod saves SPs with [PersistenceAPI](https://github.com/0x5abe/PersistenceAPI) (GPLv3)
and compresses them with [zlib](https://zlib.net) (zlib License).
Its font is Ubuntu Bold, under the [Ubuntu Font Licence](resources/fonts/UFL.txt).
