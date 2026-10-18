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
