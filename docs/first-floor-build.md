# First-floor build

## Apply the Perforce changes

Perforce shelf `42426` contains the original `PFTFirstFloor` implementation.
The revised generator below creates `PFTFirstFloorComplete` from the same source
map. GitHub contains the separate tools, tests, and instructions. The building
assets and original project source stay in Perforce.

In another workspace mapped to the PFTDigitalTwin depot, create a pending
changelist and unshelve the implementation into it:

```sh
p4 unshelve -s 42426 -c CHANGE_NUMBER
```

Replace `CHANGE_NUMBER` with your pending changelist number. Resolve any conflicts
with your local work before building. The shelf includes the startup and packaging
settings, active-map reload change, source room-ID support, generated map, and
project copies of the scripts below. Install the [Navigator plugin](navigator-plugin.md)
if it is not already present, then build `PFTDigitalTwinEditor Win64 Development`
and restart Unreal. Use the revised tools below to generate the updated map,
then set `EditorStartupMap`, `GameDefaultMap`, and `MapsToCook` to its path.

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
The finishing pass disables shadows on point, spot, and rect fixtures to reduce
render-thread work while retaining their light and the sun's directional shadow.
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
World Partition actors. Existing room destinations are copied into the map as
markers. Pixel Streaming reloads the active map when the last viewer leaves.
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
room markers, references, fixture shadows, and the material dependencies that
previously went missing. Rendered gameplay and packaging need separate checks.

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

Close Unreal Editor and run this from the project directory after the map
verification succeeds:

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\RunUAT.bat' `
  BuildCookRun "-project=$PWD\PFTDigitalTwin.uproject" -platform=Win64 `
  -clientconfig=Development -build -cook -stage -pak -iostore -archive `
  "-archivedirectory=$PWD\Saved\FirstFloorWindows" `
  -map=/Game/Level/PFTFirstFloorComplete -unattended -nop4 -utf8output
```

For another package from the same workspace, add
`-AdditionalCookerOptions=-CookIncremental` to reuse unchanged cooked packages.
Keep the files under `Saved` local to the workspace.

Open the archived `PFTDigitalTwin.exe`. From the C1003 spawn, check the floor in
front of the glass entrance and walk past the Zone 1200 sign around the corner.
Search for `CSC 1350`, select room 1200, and check that its destination is visible.

On the GTX 1080 test machine, launching with `-d3d11` avoided the DirectX 12
shader precache errors observed during startup. The package includes both
DirectX renderers.
