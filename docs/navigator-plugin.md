# PFT Navigator plugin

The source in `unreal/PFTNavigator` is our separate UE 5.8 plugin. It depends on
engine modules, not the twin's private game classes or shared plugin source.

## Install and build

From the repo root, run `python tools/install_navigator.py "<path to PFTDigitalTwin.uproject>" --changelist <number>`.
Create that pending Perforce changelist first. The installer validates the repo
data, copies plugin source and JSON, checks out changed files, adds new files,
and enables the plugin. It never submits. Omit `--changelist` only for projects
outside Perforce. Re-run after changing plugin source or repo data.

Build the editor target and restart Unreal to load a newly installed module.
Blueprints can access `PFTDirectory` through **Get Game Instance Subsystem**.
`Rooms`, `Courses`, `Search`, and `Error` expose the loaded data and failures.
`LoadDirectory` validates into temporary arrays, so a failed reload keeps the
last valid data. Empty search text returns no results. Searches match room IDs,
numbers, labels, course IDs, codes, and titles without regard to case.

The plugin reads staged `Content/Data/rooms.json` and `courses.json`. The installer
copies all four directory files after running the team's complete validator.
All current records are placeholders, not verified room or schedule information.
No original twin source or building assets are included in the GitHub plugin.
