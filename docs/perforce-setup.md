# Task 1: Perforce access

Install P4V (or the `p4` CLI), then request your own LSU account and depot access
from the project contact. Never share passwords, tickets, or a teammate's workspace.

1. Connect to `ssl:helixcore.cct.lsu.edu:1818` using your own username.
2. Create a Windows or Mac workspace with its own local root.
3. Map the PFT project and its shared `DigitalTwinTools` plugin. Ask the depot
   administrator for the correct view; the plugin belongs to the shared DMAE depot.
4. Sync the project and plugin, then run `p4 login -s` from the workspace.
5. Confirm `p4 where` resolves both the `.uproject` and `DigitalTwinTools.uplugin`.
6. Follow [Unreal setup](unreal-setup.md) to build and open the project.

Run `p4 edit` before changing tracked files and `p4 add` for intended new files.
Review `p4 opened` and `p4 diff`; submit only after team review and authorization.
Keep local credentials, generated output, and IDE settings out of both depots.

GitHub stores our data, docs, tools, and separate student-facing plugin source.
Perforce stores the original Unreal project, building assets, and shared plugin.

## Verification, October 17, 2026

This Windows workspace passes the engine/compiler/SDK checks and has a valid
Perforce login with no open files before task work. The editor target previously
built successfully. Team access is reported as expected by Jayden; each teammate
still needs to verify their own login, mapping, and sync. Their machines were not
inspected from this workstation.
