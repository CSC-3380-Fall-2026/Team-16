import argparse
import json
from pathlib import Path
import shutil
import subprocess

import validate_data


def navigator_files(root, project):
    """Yield each Git source and its matching destination in the Unreal project."""
    source = root / "unreal" / "PFTNavigator"
    destination = project.parent / "Plugins" / "PFTNavigator"
    for path in sorted(source.rglob("*")):
        relative = path.relative_to(source)
        if path.is_file() and not set(relative.parts) & {"Binaries", "Intermediate", "Saved"}:
            yield path, destination / relative
    for path in sorted((root / "data").glob("*.json")):
        yield path, destination / "Content" / "Data" / path.name


def check_install(project, settings, files):
    """Return missing, changed, or extra build inputs and any enablement error."""
    problems = []
    expected = {destination for _, destination in files}
    for source, destination in files:
        relative = destination.relative_to(project.parent)
        if not destination.is_file():
            problems.append(f"Missing {relative}")
        elif destination.read_bytes() != source.read_bytes():
            problems.append(f"Changed {relative}")
    plugin = project.parent / "Plugins" / "PFTNavigator"
    for folder in (plugin / "Source", plugin / "Content" / "Data"):
        for path in sorted(folder.rglob("*")):
            if path.is_file() and path not in expected:
                problems.append(f"Unexpected {path.relative_to(project.parent)}")
    if not any(plugin.get("Name") == "PFTNavigator" and plugin.get("Enabled") is True
               for plugin in settings.get("Plugins", [])):
        problems.append("PFTNavigator is not enabled in the project file")
    return problems


def install(project, changelist=None, *, check=False):
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
    files = list(navigator_files(root, project))
    if check:
        problems = check_install(project, settings, files)
        if problems:
            raise ValueError("\n".join(problems))
        print(f"PFTNavigator matches this checkout ({len(files)} files).")
        return
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

    for source, destination in files:
        write_file(destination, source.read_bytes())
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
    destination = project.parent / "Plugins" / "PFTNavigator"
    print(f"Installed PFTNavigator in {destination}. Build and restart Unreal to load it.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("project", type=Path)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--changelist", type=int)
    mode.add_argument("--check", action="store_true", help="Compare installed files without changing the project")
    args = parser.parse_args()
    try:
        install(args.project, args.changelist, check=args.check)
    except (OSError, ValueError) as error:
        parser.exit(1, f"{error}\n")
