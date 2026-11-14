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
`Rooms`, `Courses`, `Departments`, `Faculty`, `Search`, and `Error` expose the loaded data and failures.
`LoadDirectory` validates into temporary arrays, so a failed reload keeps the
last valid data. Empty search text returns no results. Searches match room IDs,
numbers, labels, course IDs, codes, and titles without regard to case.
Departments match on part of their name ("Computer Science") or their exact
short code ("CSE") and go to the department office. The office result is followed
by every professor in that department, each pointing at their own office.

The plugin reads staged `Content/Data/rooms.json`, `courses.json`, `departments.json`, and `faculty.json`. The installer
copies all four directory files after running the team's complete validator.
Room, faculty, and department records include sourced directory entries; see
[data sources](data-schema.md). Courses are real Fall 2026 CSC sections that meet in PFT.
No original twin source or building assets are included in the GitHub plugin.

## Task 5 destinations

`PFTDestinations` is a world subsystem. Pass a search result's `RoomId` to
`SelectRoom` to show its marker and hide the previous selection. Missing and
ambiguous bindings return false with a readable `Error`. `ClearSelection` hides
the destination. `HasLocation` returns whether a room has a placed marker or a
binding on the current map, without selecting it. A placed `PFTRoomMarker` with the matching `RoomId` can override
a JSON binding; two matching placed markers are an error.

`data/room_bindings.json` contains map-specific positions derived from the twin's
room trigger bounds, in Unreal centimeters. Each entry records its source label.
On October 17, 2026, six repo rooms matched unique triggers: 1100, 1120, 1140,
1200, 1240, and 3325. Rooms 1160, 1180, and 1220 had no match. They turned out to be numbers from our pitch mockup that aren't on the PFT floor plan, so they were removed from `rooms.json`.
Courses in PFT 1200 (like CSC 1350) resolve to the bound PFT 1200 room. A marker indicates a destination, not a walking route.

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

After installing, building, and restarting Unreal, press Play. Finish or skip
the twin's welcome screen to open the Navigator with the cursor available.
The project-side welcome screen code is in Perforce shelf 41648. A host can
defer the panel during its welcome screen by setting the `PFTOnboardingActive`
tag on the local player controller and clearing it when the screen closes.
Search by room number, course code/title, or department, then select a result to show its room
info (name, type, floor, offices, and classes) and its marker. Results marked
**Preview** have a location on the current map. **Details only** results show
their directory entry when selected.
Use **Show more rooms** to load the next set of results in a broad search. The
list starts over when you change the search or floor.
Press Enter in the search box when there is one result to open its preview.
For a room without a map location, Enter shows its directory details.
**View selected room** moves the exploration camera near that marker. Unbound
rooms show an explanation and never move the camera to an invented location.

The room and camera buttons appear above the room details. Click **Free camera**
to fly around the building.

- WASD moves. Q moves down and E moves up.
- Mouse movement looks around.
- Tab switches between flying and using the panel. The panel shows the controls
  for the active camera mode.
- **Return to player** restores the previous pawn, rotation, and cursor.

Opening a room preview hides its marker while the room stays selected. Tab lets
you fly from the preview. Flying or returning to the player shows the marker
again. Clear closes the preview and returns you to the original player.
Selecting a different room also closes the previous preview.

Exploration uses UE's spectator movement and can pass through walls. It is a
standalone/local-host prototype, not multiplayer navigation or a walking guide.
It does not replace the twin's existing pawn classes or edit its shared plugin.
The results list shows at most 50 matches. Current data contains 68 rooms,
34 faculty records, 6 departments, and 7 courses; 6 rooms have
map-derived destinations.

## Room browser

The panel lists rooms as soon as the welcome screen closes. Leave the search blank to browse
all rooms, or choose a floor to narrow the list. Floor choices come from the
loaded room data. Searches for rooms and courses also respect the selected floor;
course results use the floor of their assigned room. Department and faculty
results use their office floor. Blank-query room lists are ordered by floor
and room number. Searches preserve their result order, and up to 50 are shown.

Selecting a result shows the room number, floor, room type, and department offices, faculty, and any courses
assigned in the directory. Unbound rooms still show their details, but
**View selected room** stays disabled. **Clear selected room** hides the marker
and clears the details without moving the camera. Results for the selected room get
a gold **Selected** tag that stays when you search or change floors and goes away
when you clear the room. Scroll the panel to reach
controls when the details take more space.

## Colors

The panel and the room marker use Claire's LSU palette from [the design doc](design.md):
purple for the heading and control buttons, ink for text, and gold for the selected
room's marker label. The colors are all in `Private/PFTPalette.h`, so change them there.

## Verification

Run `python -m unittest discover -s tools` and `python tools/validate_data.py`.
Build the editor, then run the UE automation tests named `PFTNavigator` with the
Session Frontend or a headless editor using `-nullrhi -unattended -nop4
-ExecCmds="Automation RunTests PFTNavigator" -TestExit="Automation Test Queue Empty"`.
Check the exported test report; the process exit code alone does not prove tests
passed. A visual walkthrough of the building is a separate acceptance check.
