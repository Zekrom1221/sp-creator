# SP Creator

Place your own startpos (SP) in any classic level while you play. Starting from it replays a short auto part
(0.6 s by default) before you take control, so you enter the hard part at full speed and with the right camera.
The auto part plays back your own run, and you can't die in it. Platformer levels have no SPs.

Attempts from your SPs count in the attempt counter, like attempts from any startpos, but they don't save a new
best or a completion.

## Keys

- <cy>V</c>: place an SP where you are
- <cy>,</c> and <cy>.</c>: previous / next SP (or the level start)
- <cy>Delete</c>: delete the SP you start from
- The <cg>SP</c> button in the pause menu lists your SPs: play, rename, delete one or several. Its clock button
  sets the auto part length.

The game's own keys <cy>Start Pos: Previous</c> and <cy>Start Pos: Next</c> go through your SPs too.

## Settings

- The keys, and <cy>SP switcher keys</c>: the mod's own SP switcher (the <cy>,</c> and <cy>.</c> keys) on or off.
- <cy>Block control in the auto part</c>: on, your clicks in the auto part do nothing; off, a click there gives
  you control right away.
- <cy>Hide the level's startposes</c> and <cy>Notice about the level's startposes</c>: see "With other mods".
- The auto part length, <cy>SP storage</c> and the language.

## Coming from v0.9.0

Your SPs of v0.9.0 (Geometry Dash 2.2081) are brought over the first time you enter each level, and `.spcreator`
files of v0.9.0 import too. The SPs as they were stay in the <cy>levels-2.2081</c> folder, just in case.

## Sharing SPs

In the SP list, <cy>Export</c> saves the SPs you pick to a `.spcreator` file. <cy>Import</c> opens such a file:
<cy>Add</c> adds its SPs to yours, and <cy>Replace all</c> puts them in place of yours (<cy>Save and replace</c>
keeps yours as a version, <cy>Delete and replace</c> deletes them). SPs only fit the level version, game version
and Low Detail Mode they were made with. Import files only from people you trust.

<cy>Versions</c> in the SP list keeps SPs you put aside: replaced by an import, deleted with
<cy>Save and delete</c>, or left from before a level update. Open a version to bring its SPs back, rename it, or
delete the ones you don't need: nothing is deleted on its own. If a version already has the same SPs, they aren't
saved again: that version moves to the top.

When a level is updated, your SPs are moved over to the new version. An SP that needs objects the update took
away stays in <cy>Versions</c> only: the message shows its percent, so you can place it again.

**Your own level**: if you change even one of its objects (in the editor, or with the game's <cy>+</c> button on
the pause menu, which adds a startpos), its SPs aren't moved over. It starts with no SPs, and the old ones stay in
<cy>SP storage</c>, where you can delete them. Saving it without changes keeps its SPs.

<cy>SP storage</c> in the mod's settings lists the SPs and versions of all levels, the biggest first. Pick levels,
or SPs and versions of one level, and delete them to free up space.

## With other mods

- **The level's own startposes** are hidden from other mods and from the game's own startpos list, so they see
  only your SPs. The level still starts where it does. To see them everywhere, turn off
  <cy>Hide the level's startposes</c> in the settings, then enter the level again.
- **The game's own startpos list** on the pause menu shows your SPs by their names (Latin letters only).
- **Startpos switchers** (Mega Hack, Eclipse) list your SPs. An SP placed in the current visit goes to the end of
  their list (and of the game's own startpos list) until you enter the level again. The mod's own <cy>,</c> and
  <cy>.</c> don't need that: they go through all your SPs in order right away, the new ones too. If a level had
  no SPs when you entered it, Eclipse's switcher may not be there until you enter the level again.
- **Mega Hack**: its switcher uses the game's own startpos list and keys. <cy>Smart StartPos</c> can stay on: SPs
  start the way you placed them.
- With <cy>Physics Bypass</c>, SPs play the same at any tick rate.
- **Blitzkrieg** makes stages of your SPs. After adding or deleting SPs, create a new profile with
  <cy>Bind profile</c> on: editing a profile keeps its old startposes.

## Known issues

- Where a level's gameplay is rotated or goes back (up the screen, or to the left), SPs are in the order of where
  they stand, not of their percent: there the SP list and the <cy>,</c> and <cy>.</c> keys can go back and forth.
- SPs placed with Low Detail Mode on in one of your own levels may not import for someone else. Until this is
  fixed, place and import such SPs with Low Detail Mode off on both sides.
- SPs placed on a screen of another shape (16:9 and 16:10, say) start with the camera a little to the side: it
  slides to its place during the auto part.
- While the file window of <cy>Export</c> or <cy>Import</c> is open, don't click the game: in full screen the
  window goes behind it. If that happens, right-click the game on the taskbar and pick <cy>Close window</c>: the
  game stays open, and the file window comes back.

## Bugs and ideas

Report bugs and share ideas on [GitHub](https://github.com/Zekrom1221/sp-creator/issues) or in the
[Telegram channel](https://t.me/spcreator_gd) (in Russian).
