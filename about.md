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
- The <cg>SP</c> button in the pause menu lists your SPs: play, rename, delete

The keys, the auto part length, whether your clicks in the auto part count and the language are in the mod's
settings.

## Sharing SPs

In the SP list, <cy>Export</c> saves the SPs you pick to a `.spcreator` file, and <cy>Import</c> adds the SPs
of such a file to the level, or puts them in place of yours: you choose to save yours as a version or delete
them. SPs only fit the level version, game version and Low Detail Mode they were made with. Import files only
from people you trust.

<cy>Versions</c> in the SP list holds the SPs saved that way, and those set aside when the level was updated.
Open one to bring its SPs back, rename it, or delete the ones you don't need: nothing is deleted on its own.
SPs that a version holds already aren't saved twice: that version moves to the top instead.

When a level is updated, your SPs are moved over to the new version. One that depends on objects the update
took away stays in <cy>Versions</c> only: the message says where it was, so you can place it again. This is for
online levels: when you edit your own level, its SPs aren't moved over.

<cy>SP storage</c> in the mod's settings lists the SPs and versions of all levels, the biggest first. Pick levels,
or SPs and versions of one level, and delete them to free up space.

## With other mods

- **Startpos switchers** (Startpos Switcher, Mega Hack) list your SPs with the level's startposes. An SP placed in
  the current visit goes to the end of their list until you enter the level again. After deleting or replacing
  SPs, their list may keep a few extra places until then: those take you to the level start.
- **Mega Hack**: on rated levels its switcher is off unless you turn on <cy>Allow Rated</c> in the StartPos
  Switcher options.
- **Blitzkrieg** makes stages of your SPs. After adding or deleting SPs, create a new profile with
  <cy>Bind profile</c> on: editing a profile keeps its old startposes.

## Known issues

- Where the gameplay of a level is rotated or goes back, SPs are in the order of where they stand, not of their
  percent: there the SP list and the <cy>,</c> and <cy>.</c> keys can go back and forth.

## Beta

This is a beta. Found a bug, or have an idea? Tell about it on [GitHub](https://github.com/Zekrom1221/sp-creator/issues).
