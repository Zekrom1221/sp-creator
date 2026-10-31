# SP Creator

Place your own startpos (SP) in any level while you play. Starting from it replays a short auto part
(0.6 s by default) before you take control, so you enter the hard part at full speed and with the right camera.
The auto part plays back your own run, and you can't die in it.

Attempts from your startpos count in the attempt counter, like attempts from any startpos, but they don't save
a new best or a completion.

## Keys

- <cy>V</c>: place an SP where you are
- <cy>,</c> and <cy>.</c>: previous / next SP (or the level start)
- <cy>Delete</c>: delete the SP you start from
- The <cg>SP</c> button in the pause menu lists your SPs: play, rename, delete one or several

The keys, the auto part length, whether your clicks in the auto part count, whether other mods see the level's
own startposes and the language are in the mod's settings.

## Sharing SPs

In the SP list, <cy>Export</c> saves the SPs you pick to a `.spcreator` file, and <cy>Import</c> adds the SPs
of such a file to the level, or puts them in place of yours: you choose to save yours as a version or delete
them. SPs only fit the level version, game version and Low Detail Mode they were made with. Import files only
from people you trust.

<cy>Versions</c> in the SP list holds the SPs put aside: saved that way, deleted with <cy>Save and delete</c>,
or set aside when the level was updated. Open one to bring its SPs back, rename it, or delete the ones you don't
need: nothing is deleted on its own.
SPs that a version holds already aren't saved twice: that version moves to the top instead.

When a level is updated, your SPs are moved over to the new version. One that depends on objects the update
took away stays in <cy>Versions</c> only: the message says where it was, so you can place it again.

**Your own level**: if you change its objects in the editor (move, add or delete even one), its SPs aren't moved
over. It starts with no SPs, and the old ones stay in <cy>SP storage</c>, where you can delete them. Saving it
without changes keeps its SPs.

<cy>SP storage</c> in the mod's settings lists the SPs and versions of all levels, the biggest first. Pick levels,
or SPs and versions of one level, and delete them to free up space.

## With other mods

- **The level's own startposes** are hidden from other mods, so their switchers and Blitzkrieg have only your
  SPs. The level still starts where it does. To see them in other mods too, turn off
  <cy>Hide the level's startposes</c> in the settings, then enter the level again.
- **Startpos switchers** (Startpos Switcher, Mega Hack) list your SPs. An SP placed in the current visit goes to
  the end of their list until you enter the level again. After deleting or replacing SPs, their list may keep a
  few extra places until then: those take you to the level start.
- **Mega Hack**: on rated levels its switcher is off unless you turn on <cy>Allow Rated</c> in the StartPos
  Switcher options. <cy>Smart StartPos</c> can stay on: SPs start the way you placed them.
- **Blitzkrieg** makes stages of your SPs. After adding or deleting SPs, create a new profile with
  <cy>Bind profile</c> on: editing a profile keeps its old startposes.

## Known issues

- Where the gameplay of a level is rotated or goes back, SPs are in the order of where they stand, not of their
  percent: there the SP list and the <cy>,</c> and <cy>.</c> keys can go back and forth.

## Beta

This is a beta. Found a bug, or have an idea? Tell about it on [GitHub](https://github.com/Zekrom1221/sp-creator/issues).
