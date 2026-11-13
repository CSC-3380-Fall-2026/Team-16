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
The finishing pass disables shadows on point, spot, and rect fixtures to reduce
render-thread work while retaining their light and the sun's directional shadow.
It also gives typical fixtures a 60-metre draw limit with a 12-metre fade. Wider
fixtures get a longer limit based on their attenuation radius, and shorter
authored limits are kept. The Windows launcher uses these distances even with
its low shadow quality setting.
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
clearance, and sightline to each marker. Room 1140 is viewed from its hallway
because the storage room is too narrow. The Commons uses the C1008 atrium
volume. [LSU's first-floor plan](https://www.lsu.edu/eng/images/pft_floorplan_guide2_webupdated2021.pdf)
Pixel Streaming reloads the active map when the last viewer leaves.
Copied room volumes keep their source room IDs for the existing room-data service.
Datasmith metadata is retained, while reimport templates that reference the full
twin are removed from the lightweight copy.

## Create or update the lightweight map

The generator, verifier, and selection tests are under the Unreal project's
`Tools` directory. Use `p4 edit` before replacing any versioned scripts there.

Run from the project directory with Unreal Editor closed:

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  "$PWD\PFTDigitalTwin.uproject" /Engine/Maps/Entry `
  "-ExecutePythonScript=$PWD\Tools\create_first_floor_level.py --dry-run" `
  -unattended -nullrhi -RenderOffscreen -nosplash -nop4 -nosound
```

Read `Saved/FirstFloorBuild.json` to review the selected counts and height range.
Report fields use PascalCase, including `SelectedActors` and `SourceRoomIds`.
The tools also accept build reports produced with the earlier snake_case fields.
Remove `--dry-run` to create the geometry. After that editor process exits, run
the same command with `--finish` instead of `--dry-run` to add the destinations.
Build the Navigator plugin with the room preview changes before running this pass.
The two passes use separate editor processes so Unreal can release the temporary
world used for copying. Use `-ExecutePythonScript`; the Python commandlet does
not support the editor's actor duplication operation.

The generator refuses to overwrite an existing map. To rebuild after source
changes, use `--output /Game/Level/PFTFirstFloorUpdated`, inspect that map, and
update `EditorStartupMap`, `GameDefaultMap`, and `MapsToCook` to the new path.
Add the generated `.umap` to the project changelist with `p4 add`.
Use `p4 edit` before changing existing configuration or map files, including
rerunning `--finish` on a versioned map.

Select `/Game/Level/PatrickFTaylorHall` manually to work on the complete building.
To package the complete twin again, change the startup maps and packaging map
list back to that map. Changing only `MapsToCook` leaves the game startup map
pointing at the lightweight version.

Run the selection tests from the project directory:

```powershell
python -m unittest discover -s Tools -p test_first_floor_level.py
```

Check the saved map and its dependency graph without opening the full twin:

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  "$PWD\PFTDigitalTwin.uproject" -run=PythonScript `
  "-script=$PWD\Tools\verify_first_floor_level.py" `
  -unattended -nullrhi -nosplash -nop4 -nosound
```

Results are written to `Saved/FirstFloorVerification.json`. This checks loading,
room preview coverage and positions, references, fixture shadows and draw limits,
known checkerboard slots, and material dependencies. Rendered gameplay and
packaging need separate checks.

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

Open `Play PFT Demo.cmd` in the Windows package. From the C1003 spawn, check the floor in
front of the glass entrance and walk past the Zone 1200 sign around the corner.
Search for `CSC 1350`, select room 1200, and use **View selected room** to check
the camera. Try room 1202 and the Commons to check the added destinations.

The launcher opens the demo at 720p with DirectX 11 and a 30 FPS cap. The package
includes both DirectX renderers. Opening `PFTDigitalTwin.exe` directly uses the
project's default settings.

## Check the Zone 1200 walkthrough

Close the running demo. From the Windows package directory, run the launcher in
walkthrough mode:

```powershell
& '.\Play PFT Demo.cmd' walkthrough
```

The test starts at C1003 and drives the walking pawn past the Zone 1200 sign and
into the hallway. It fails if the pawn falls through the floor or stalls before
the end. The game exits when the test finishes. Check the test result in
`PFTDigitalTwin\Saved\Automation\FirstFloorWalk\index.json` and use `FrameTime`
in the new CSV under `PFTDigitalTwin\Saved\Profiling\CSV` to check frame pacing.
