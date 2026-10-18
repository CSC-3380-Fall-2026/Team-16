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

## Task 5 destinations

`PFTDestinations` is a world subsystem. Pass a search result's `RoomId` to
`SelectRoom` to show its marker and hide the previous selection. Missing and
ambiguous bindings return false with a readable `Error`. `ClearSelection` hides
the destination. A placed `PFTRoomMarker` with the matching `RoomId` can override
a JSON binding; two matching placed markers are an error.

`data/room_bindings.json` contains map-specific positions derived from the twin's
room trigger bounds, in Unreal centimeters. Each entry records its source label.
On October 17, 2026, six repo rooms matched unique triggers: 1100, 1120, 1140,
1200, 1240, and 3325. Rooms 1160, 1180, and 1220 had no match and remain unbound.
The sample CSC 3380 record resolves to the bound PFT 1200 room; its class assignment
is still placeholder data. A marker indicates a destination, not a walking route.

To regenerate bindings, close extra editor sessions and run UE's
`UnrealEditor-Cmd.exe "<project>" -run=pythonscript -script="<repo>/tools/export_room_bindings.py" -nullrhi -unattended -nop4`.
The script reads World Partition actor descriptors, accepts only uniquely matching
room numbers on area triggers, and writes our binding JSON. It never saves the map.
Review the output, run the validator, then rerun the installer and build.

The Perforce asset `Content/Blueprints/Network/BP_NetworkManager.uasset` points to
`https://pft-digitaltwin.cct.lsu.edu/getRoomByNumber` and related room endpoints.
They redirect unauthenticated requests to LSU's Microsoft sign-in. Full room
descriptions and ownership were not retrieved. The binding export uses geometry
metadata already available in the Perforce map; it does not guess missing rooms.

## Task 6 exploration and UI

After installing, building, and restarting Unreal, press Play. The Navigator
panel appears below the twin's top bar for the local player. Search by room
number or course code/title, then select a result to show its room marker.
**View selected room** moves the exploration camera near that marker. Unbound
rooms show an explanation and never move the camera to an invented location.

Click **Explore** or **Fly** to use the separate exploration pawn:

- WASD or arrow keys move; Q/E move down/up.
- Mouse movement looks around.
- Tab releases the mouse and pauses movement so the panel is usable.
- Fly captures the mouse again.
- Return to original player restores the previous pawn, rotation, and cursor.

Exploration uses UE's spectator movement and can pass through walls. It is a
standalone/local-host prototype, not multiplayer navigation or a walking guide.
It does not replace the twin's existing pawn classes or edit its shared plugin.
The results list shows at most 25 matches. Current data contains 9 rooms and
1 placeholder course; 6 rooms have map-derived destinations.

## Verification

Run `python -m unittest discover -s tools` and `python tools/validate_data.py`.
Build the editor, then run the UE automation tests named `PFTNavigator` with the
Session Frontend or a headless editor using `-nullrhi -unattended -nop4
-ExecCmds="Automation RunTests PFTNavigator" -TestExit="Automation Test Queue Empty"`.
Check the exported test report; the process exit code alone does not prove tests
passed. A visual walkthrough of the building is a separate acceptance check.
