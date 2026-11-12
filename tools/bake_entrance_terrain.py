"""Bake the source landscape visible from the C1003 entrance."""

from pathlib import Path
import sys

import unreal


sys.path.insert(0, str(Path(__file__).resolve().parent))
from create_first_floor_level import C1003_TERRAIN, baked_terrain_textures, load_selected_actors, load_source_map


def main():
    """Save the source tile as a standalone mesh with baked textures."""
    if unreal.EditorAssetLibrary.does_asset_exist(C1003_TERRAIN):
        raise RuntimeError(f"{C1003_TERRAIN} already exists. Move it before rebaking.")
    load_source_map()
    descriptors = unreal.WorldPartitionBlueprintLibrary.get_actor_descs()
    source = [item for item in descriptors if str(item.label) == "LandscapeStreamingProxy_4_6_0"]
    if len(source) != 1:
        raise RuntimeError(f"Expected one C1003 terrain tile, found {len(source)}")
    tile = load_selected_actors(source)[0]
    mesh = unreal.PFTTerrainLibrary.bake_terrain(tile, C1003_TERRAIN)
    if not mesh:
        raise RuntimeError(f"Could not bake {C1003_TERRAIN}")
    if not baked_terrain_textures(mesh.get_material(0)):
        raise RuntimeError("Terrain bake did not produce textures in the mesh package")
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    if not assets.save_loaded_asset(mesh, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {C1003_TERRAIN}")
    unreal.log(f"C1003_TERRAIN_BAKED: {C1003_TERRAIN}")


if __name__ == "__main__":
    main()
