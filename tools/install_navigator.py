import argparse
import json
from pathlib import Path
import shutil
import subprocess

import validate_data


def install(project, changelist=None):
    project = Path(project).resolve()
    root = Path(__file__).resolve().parents[1]
    if not project.is_file() or project.suffix != ".uproject":
        raise ValueError("Choose an existing .uproject file")
    original = project.read_bytes()
    settings = json.loads(original.decode("utf-8-sig"))
    if settings.get("EngineAssociation") != "5.8":
        raise ValueError("This plugin is tested with UE 5.8")
    errors = validate_data.validate(root / "data")
    if errors:
        raise ValueError("\n".join(errors))
    p4 = shutil.which("p4") or r"C:\Program Files\Perforce\p4.exe"

    def write_file(path, content):
        if path.exists() and path.read_bytes() == content:
            return
        exists = path.exists()
        if changelist is not None and exists:
            status = subprocess.run([p4, "-ztag", "-F", "%action%", "fstat", "-T", "depotFile,action", str(path)], cwd=project.parent, capture_output=True, text=True, check=True)
            if status.stdout.strip() not in {"add", "edit"}:
                subprocess.run([p4, "edit", "-c", str(changelist), str(path)], cwd=project.parent, check=True)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)
        if changelist is not None and not exists:
            subprocess.run([p4, "add", "-c", str(changelist), str(path)], cwd=project.parent, check=True)

    source = root / "unreal" / "PFTNavigator"
    destination = project.parent / "Plugins" / "PFTNavigator"
    for path in source.rglob("*"):
        if path.is_file() and not set(path.relative_to(source).parts) & {"Binaries", "Intermediate", "Saved"}:
            write_file(destination / path.relative_to(source), path.read_bytes())
    for path in (root / "data").glob("*.json"):
        write_file(destination / "Content" / "Data" / path.name, path.read_bytes())
    plugins = settings.setdefault("Plugins", [])
    entry = next((plugin for plugin in plugins if plugin["Name"] == "PFTNavigator"), None)
    if entry is None:
        plugins.append({"Name": "PFTNavigator", "Enabled": True})
    else:
        entry["Enabled"] = True
    indent = "\t" if "\n\t" in project.read_text(encoding="utf-8-sig") else 2
    text = json.dumps(settings, indent=indent)
    if original.endswith(b"\n"):
        text += "\n"
    if b"\r\n" in original:
        text = text.replace("\n", "\r\n")
    write_file(project, text.encode())
    print(f"Installed PFTNavigator in {destination}. Build and restart Unreal to load it.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("project", type=Path)
    parser.add_argument("--changelist", type=int)
    args = parser.parse_args()
    install(args.project, args.changelist)
