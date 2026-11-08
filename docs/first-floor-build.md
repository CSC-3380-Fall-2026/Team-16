# First-floor build

## Apply the Perforce changes

The Unreal project changes and generated map are shelved in Perforce changelist
`42426`; they have not been submitted. GitHub contains only the separate tools,
tests, and these instructions. The building assets and original project source
stay in Perforce.

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
and restart Unreal. The existing authoring workspace already has these changes;
it does not need to unshelve them again.

## Map contents

The default editor map and game map are `/Game/Level/PFTFirstFloor`. Packaging
cooks this map and its dependencies instead of cooking every map in the project.
The complete twin remains at `/Game/Level/PatrickFTaylorHall` for editing.

`CreateFirstFloorLevel.py` copies the first-floor data layer, first-floor room
volumes, shared lighting, and unfiled static meshes within the first-floor room
height range. Floors 2–4, the roof, MEP, campus scenery, and the full-building HLODs
are excluded. Shared meshes and tall structural elements are kept whole.
The output is a standalone map, so opening it does not scan the full twin's
World Partition actors. Existing room destinations are copied into the map as
markers. Pixel Streaming reloads the active map when the last viewer leaves.
Copied room volumes keep their source room IDs for the existing room-data service.
Datasmith metadata is retained, while reimport templates that reference the full
twin are removed from the lightweight copy.

## Create or update the lightweight map

The GitHub copies are `tools/CreateFirstFloorLevel.py`,
`tools/VerifyFirstFloorLevel.py`, and `tools/test_first_floor_level.py`. The shelf
installs matching copies under the Unreal project's `Tools` directory. After
changing these scripts, check out the existing project copies with `p4 edit`
before copying updates into that directory.

Run from the project directory with Unreal Editor closed:

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  "$PWD\PFTDigitalTwin.uproject" /Engine/Maps/Entry `
  "-ExecutePythonScript=$PWD\Tools\CreateFirstFloorLevel.py --dry-run" `
  -unattended -nullrhi -RenderOffscreen -nosplash -nop4 -nosound
```

Read `Saved/FirstFloorBuild.json` to review the selected counts and height range.
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
  "-script=$PWD\Tools\VerifyFirstFloorLevel.py" `
  -unattended -nullrhi -nosplash -nop4 -nosound
```

Results are written to `Saved/FirstFloorVerification.json`. This checks loading,
room markers, and references; it does not replace a rendered gameplay review or
a packaged-build test.

The current workspace is missing the `BasicMasterMaterials` library referenced by
the original model's materials. Restore that dependency before completing visual
or packaged-build verification.
