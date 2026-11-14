# First-floor build

## Apply the Perforce changes

Perforce shelf `42426` contains `PFTFirstFloorComplete`, its baked entrance terrain,
and the project settings. GitHub contains the separate tools, plugin source, tests,
and instructions. The building assets and original project source stay in Perforce.

In another workspace mapped to the PFTDigitalTwin depot, create a pending
changelist and unshelve the implementation into it:

```sh
p4 unshelve -s 42426 -c CHANGE_NUMBER
```

Replace `CHANGE_NUMBER` with your pending changelist number. Resolve any conflicts
with your local work before building. The shelf includes the startup and packaging
settings, active-map reload change, source room-ID support, generated map, and
project copies of the scripts below. Install the [Navigator plugin](navigator-plugin.md),
then build `PFTDigitalTwinEditor Win64 Development` and restart Unreal. Use the
Navigator room preview update to view the camera positions saved in the current shelf.

## Map contents

The default editor map and game map are `/Game/Level/PFTFirstFloorComplete`. Packaging
cooks this map and its dependencies instead of cooking every map in the project.
The complete twin remains at `/Game/Level/PatrickFTaylorHall` for editing.

`create_first_floor_level.py` keeps the first-floor actors, room volumes, roof,
and upper-floor architecture and lighting. Unfiled structure and nearby site
meshes are included so the building stays visible through entrances and atriums.
The source `Floor_MainHallway` group keeps its original floor meshes and walking
collision through C1003.
The player starts in C1003 facing the Zone 1200 signs.
The finishing pass replaces checkerboard slots on the lounge seating and tables,
the C1003 wall, floor pieces in rooms 1221 and 1363, the Panera soda fountain,
foundation slabs, and nearby paving. It uses existing building materials on the
demo's mesh components and preserves material assignments that already work.
Retained upper-floor meshes also get matching finishes on the floor pieces,
chairs, cushions, couches, and lab benches where slots are blank or checkered.
Grouped window and elevator meshes get matching metal finishes for unassigned
slots. The verifier scans all static mesh components, including grouped actors.
The finishing pass disables shadows on point, spot, and rect fixtures to reduce
render-thread work while retaining their light and the sun's directional shadow.
It also gives typical fixtures a 60-metre draw limit with a 12-metre fade. Wider
fixtures get a longer limit based on their attenuation radius, and shorter
authored limits are kept. The Windows launcher uses half of those draw
distances to reduce the number of distant lights rendered during the walkthrough.
The selection retains ambiguous upper-floor meshes and drops known furnishing
and equipment categories outside the first-floor height band. It also omits
the source map's unloaded MEP and cinematic layers, distant campus meshes,
World Partition landscape tiles, and HLODs. Review the result in Unreal from
the entrances, atriums, and other public first-floor spaces.
The C1003 entrance uses a baked static mesh of the original outdoor terrain.
Its textures are stored with the mesh, and its `NoCollision` profile leaves
walking collision to the original building meshes. The generator excludes the
elevated `SM_road_full_block` overlay from the standalone map.
The output is a standalone map, so opening it does not scan the full twin's
World Partition actors. The finishing pass places first-floor destination
markers and preview cameras using the room volumes. It checks the floor, camera
clearance, and sightline to each marker. Preferred directions show the lectern
in room 1245, the conference table in room 1278, the Incubator seating in room
1280, and the stairs and seating in the Commons. The generator uses the default
clear view when the preferred direction is obstructed. Room 1140 is viewed from
its hallway because the storage room is too narrow. The Commons uses the C1008
atrium volume. [LSU's first-floor plan](https://www.lsu.edu/eng/images/pft_floorplan_guide2_webupdated2021.pdf)
Pixel Streaming reloads the active map when the last viewer leaves.
Copied room volumes keep their source room IDs for the existing room-data service.
The first-floor HUD reads room and hallway numbers directly from the saved map.
Its player controller integration is on Perforce shelf 42426.
Datasmith metadata is retained, while reimport templates that reference the full
twin are removed from the lightweight copy.

## Create or update the lightweight map

Build `PFTDigitalTwinEditor Win64 Development` with the Navigator plugin
installed and sync the imported material plugins. Close the project in Unreal
Editor, then run this from the project directory with a fresh map name.

```powershell
& .\Tools\rebuild_first_floor.ps1 -Project .\PFTDigitalTwin.uproject `
  -MapName PFTFirstFloorUpdated
```

The script copies the geometry, adds the destinations and material repairs,
then verifies the saved map. It runs each step in a fresh Unreal process and
prints the map and report paths after verification. Build and verification
reports and step logs are under `Saved/FirstFloorBuilds/Level/PFTFirstFloorUpdated`.
Pass `-EngineRoot` when Unreal is installed elsewhere.

Add `-PackageDemo` to build the Windows demo after map verification. The command
prints the launcher path after the Commons walkthrough and Navigator test
pass. A fresh map cooks its assets. On resume, packaging can reuse a matching cook.

```powershell
& .\Tools\rebuild_first_floor.ps1 -Project .\PFTDigitalTwin.uproject `
  -MapName PFTFirstFloorUpdated -PackageDemo
```

If a later step fails, use the same command with `-Resume` once the error is
fixed. Resume uses the saved geometry and its build report. Open the new map
in Unreal to review it before updating `EditorStartupMap`, `GameDefaultMap`,
and `MapsToCook`. Use `p4 add` for the selected new map and `p4 edit` before
changing any versioned map, script, or configuration.

Select `/Game/Level/PatrickFTaylorHall` manually to work on the complete building.
To package the complete twin again, change the startup maps and packaging map
list back to that map. Changing only `MapsToCook` leaves the game startup map
pointing at the lightweight version.

Run the selection tests from the project directory:

```powershell
python -m unittest discover -s Tools -p test_first_floor_level.py
```

To verify the current demo map by itself, run the verifier from the project
directory.

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  "$PWD\PFTDigitalTwin.uproject" -run=PythonScript `
  "-script=$PWD\Tools\verify_first_floor_level.py" `
  -unattended -nullrhi -nosplash -nop4 -nosound
```

Results are written to `Saved/FirstFloorVerification.json`. This checks loading,
room ID uniqueness, room preview coverage, camera clearance and sightlines,
references, fixture shadows and draw limits, checkerboard and unassigned static-mesh slots, and
material dependencies. Its `FallbackMaterialSlots` field identifies the actor,
component, mesh, and slot for each missing surface.
Rendered gameplay and packaging need separate checks.

For a selection preview, run `create_first_floor_level.py --dry-run` with
`-ExecutePythonScript` in the editor. Read `FirstFloorSelection.json` in the
same report directory as the build. Pass `--output /Game/Level/PFTFirstFloorUpdated`
to either Python script to select a candidate map.

Sync `BasicMasterMaterials` and `DTGenericCatalog` through the project's Perforce
stream imports before visual or packaged-build verification. A custom client
view must include both plugin mappings from the project stream. The generator
reports missing material asset paths before it copies or finishes the map.

If the landscape outside C1003 changes, build the editor target with the
`PFTTerrainTools` plugin installed, then run the terrain bake. The bake needs a
graphics device to capture the material. Keep a copy of the existing
`SM_C1003Terrain` asset before replacing it at the same package path.

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  "$PWD\PFTDigitalTwin.uproject" /Engine/Maps/Entry `
  "-ExecutePythonScript=$PWD\Tools\bake_entrance_terrain.py" `
  -unattended -d3d11 -RenderOffscreen -nosplash -nop4 -nosound
```

The `--finish` pass places the baked mesh. The verifier checks its textures,
its collision settings, and the standalone map's dependencies.

## Package the Windows demo

Close the project in Unreal Editor and run this from the project directory
after the map verification succeeds:

```powershell
& .\Tools\package_first_floor_demo.ps1 -Project .\PFTDigitalTwin.uproject
```

The script reuses unchanged cooked packages. Pass `-FullCook` to cook everything
again or `-EngineRoot` if UE 5.8 is installed in a different directory. The script
copies the launcher into `Saved\FirstFloorWindows\Windows` after packaging succeeds.
Keep the files under `Saved` local to the workspace.
The project settings on Perforce shelf `42426` use the Unreal 5.8 include order
and keep local `UserEngine.ini` overrides out of the Windows package.

Pass `-Verify` to run the Commons walkthrough and the Navigator click test
after packaging. The command exits with an error if either test fails.

For C++ implementation edits, pass `-ReuseCook` to rebuild the executable and
package the saved Windows cook. The script checks the selected map, project and
plugin files, headers, and cook metadata. A missing or stale cook falls back to
incremental cooking. Use a regular cook after changing asset defaults or
serialization in C++.

```powershell
& .\Tools\package_first_floor_demo.ps1 -Project .\PFTDigitalTwin.uproject `
  -ReuseCook -Verify
```

To keep a playable build while packaging a review copy, set a separate archive
directory. Relative archive paths are resolved from the current directory.

```powershell
& .\Tools\package_first_floor_demo.ps1 -Project .\PFTDigitalTwin.uproject `
  -ArchiveDirectory '.\Saved\FirstFloor Review' -Verify
```

To package a rebuilt candidate, pass the same map name you gave the rebuild
script.

```powershell
& .\Tools\package_first_floor_demo.ps1 -Project .\PFTDigitalTwin.uproject `
  -MapName PFTFirstFloorUpdated
```

The candidate archive goes in
`Saved/FirstFloorBuilds/Level/PFTFirstFloorUpdated/WindowsDemo` by default.
The candidate also gets its own cook cache in that map's build folder. Use its
launcher to play and test the selected map before choosing it as the project
startup map.

Open `Play PFT Demo.cmd` in the Windows package. Choose **Skip tour** to open the
Navigator. From the C1003 spawn, check the floor in
front of the glass entrance and walk past the Zone 1200 sign around the corner.
Search for `CSC 1350`, select room 1200, and use **View selected room** to check
the camera. Try room 1202 and the Commons to check the added destinations.

The launcher opens the demo at native 720p with DirectX 11 and a 30 FPS cap.
The package includes both DirectX renderers.

## Verify a Windows package

Close the running demo. From the Windows package directory, run both checks:

```powershell
& '.\Play PFT Demo.cmd' verify
```

The walkthrough clicks **Skip tour**, starts at C1003, and walks past the Zone
1200 sign into the Commons. It fails if the welcome screen blocks the walk or if
the pawn falls through the floor or stalls. The Navigator test checks that the
welcome screen locks walking and looking until it closes. It then searches for
a course, opens the room preview, and returns to the original player. The
welcome screen input change is on Perforce shelf `41648`. Use `walkthrough` to
run the shorter Zone 1200 route on its own. The game exits after each test.

Logs are in `PFTDigitalTwin\Saved\Logs`. The `CommonsWalk` and `Navigator`
folders under `PFTDigitalTwin\Saved\Automation` each contain an `index.json`
with the test result. The shorter walkthrough writes to `FirstFloorWalk`.
Use `FrameTime` in the new CSV under `PFTDigitalTwin\Saved\Profiling\CSV`
to check frame pacing.
