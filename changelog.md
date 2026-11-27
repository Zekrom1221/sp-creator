# Changelog

## v1.0.0

For Geometry Dash 2.2091 and Geode 6. For Geometry Dash 2.2081, stay on v0.9.0.

- **Your SPs of v0.9.0 come along**: they're brought over the first time you enter each level, and `.spcreator`
  files of v0.9.0 import too. The SPs as they were stay in the `levels-2.2081` folder.
- No PersistenceAPI needed anymore: SPs are saved with the game's own checkpoints.

### New

- The game's own startpos list on the pause menu (new in Geometry Dash 2.2091) shows your SPs by their names, and
  the game's *Start Pos: Previous / Next* keys go through them.
- The auto part length right from the SP list (the clock button). The setting's arrows go by 0.1 s.
- A notice on entering a level with startposes of its own: whether the mod hides them, and how to change that.
  *Notice about the level's startposes* in the settings turns it off.
- With *Hide the level's startposes* off, the mod's `,` and `.` go through the level's startposes too.
- `,` and `.` on the pause menu: each press restarts behind the menu, so you resume where you switched to.

### Fixed

- Switching or deleting SPs while the level's end animation plays no longer restarts the level. It could end in a
  completion in the new attempt, and in your own level leave the percent stuck at 100%.
- With *Block control in the auto part* off, only a jump ends the auto part: A and D (Mega Hack's switcher on
  them) no longer do.
- Mega Hack's *Smart StartPos* no longer changes an SP's form and camera, and its switcher gets past a deleted SP.
- Blitzkrieg (for when it comes out for 2.2091): the stages of your SPs no longer show 0% after you enter the
  level again.
- Globed: SPs no longer turn off when other players are in the level as it loads.
- Physics Bypass (Mega Hack, Eclipse): SPs play the same at any tick rate.
- Eclipse's Show Trajectory: in the auto part its path stops where you'd die, as anywhere else.
- Notifications stay on screen for 1.5 seconds at any game speed.
- The game's progress bar shows where you are at a restart from an SP, and other mods (Mega Hack's best run) get
  the SP's percent.
- SPs placed without a pause are kept when another mod takes you out of the level.
- A damaged SP file no longer crashes the game: it's set aside as `.bad`, and you're told once.
- Names from imported files follow the same rules as typed names: letters the font has, at most 48.
- Import: an export of your own level from v0.9.0 imports; clearer messages for SPs of another version of the level
  and for a file that can't be read.
- You can't place an SP in a level where SPs are off.
- The SP list and Versions keep their scroll position. The + and - of the auto part length are in the middle of
  their buttons. Clearer texts.

## v0.9.0

First beta.
