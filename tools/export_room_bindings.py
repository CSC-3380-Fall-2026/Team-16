import json
import math
from pathlib import Path
import re


def build_bindings(rooms, actors, map_path):
    bindings, unresolved = [], []
    for room in rooms:
        matches = [actor for actor in actors if room["RoomNumber"] in re.findall(r"(?<!\d)\d{4}[A-Za-z]?(?!\d)", actor["Label"])]
        if len(matches) != 1:
            unresolved.append(room["Name"])
            continue
        actor = matches[0]
        low, high = actor["Min"], actor["Max"]
        if not all(math.isfinite(value) for value in low + high) or any(a >= b for a, b in zip(low, high)):
            unresolved.append(room["Name"])
            continue
        bindings.append({"RoomId": room["Name"], "Map": map_path, "Location": {"X": (low[0] + high[0]) / 2, "Y": (low[1] + high[1]) / 2, "Z": low[2] + 100}, "SourceLabel": actor["Label"]})
    return bindings, unresolved


def export_bindings():
    import unreal

    root = Path(__file__).resolve().parents[1]
    map_path = "/Game/Level/PatrickFTaylorHall"
    if not unreal.EditorLoadingAndSavingUtils.load_map(map_path):
        raise RuntimeError("Cannot load the PFT map")
    actors = []
    for actor in unreal.WorldPartitionBlueprintLibrary.get_actor_descs():
        if actor.native_class and "AreaTrigger" in actor.native_class.get_name():
            actors.append({"Label": str(actor.label), "Min": [actor.bounds.min.x, actor.bounds.min.y, actor.bounds.min.z], "Max": [actor.bounds.max.x, actor.bounds.max.y, actor.bounds.max.z]})
    rooms = json.loads((root / "data/rooms.json").read_text())
    bindings, unresolved = build_bindings(rooms, actors, map_path)
    (root / "data/room_bindings.json").write_text(json.dumps(bindings, indent=2) + "\n")
    unreal.log(f"Exported {len(bindings)} room markers. Unresolved: {unresolved}")


if __name__ == "__main__":
    export_bindings()
