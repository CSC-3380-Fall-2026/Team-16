# Task 2: Open the digital twin in Unreal Engine 5.8

The original board said Unity. This project uses Unreal Engine 5.8; no engine
conversion is needed. Sync the existing twin from Perforce instead of importing
Unreal assets into another engine.

## Windows setup

1. Install UE 5.8, Microsoft C++ Build Tools, the Windows SDK, and .NET Framework
   4.8 SDK. VS Code can remain the code editor.
2. Complete [Perforce setup](perforce-setup.md). Sync the `.uproject`, source,
   content, and shared `DigitalTwinTools` plugin into the same project folder.
3. Open the `.uproject` with UE 5.8. Accept the initial C++ build when prompted.
4. For a command-line editor build, run the engine's `Engine/Build/BatchFiles/Build.bat`
   with `PFTDigitalTwinEditor Win64 Development -Project="<absolute .uproject path>" -WaitMutex`.
   Use `p4 edit` first if an existing generated build receipt is read-only.
5. Open `/Game/Level/PatrickFTaylorHall`, wait for assets/shaders to finish, and
   use Play to test. In World Partition Editor, select the building and use
   **Load Region from Selection** if parts are unloaded.

## Verification, October 17, 2026

The Windows environment check passed and `PFTDigitalTwinEditor Win64 Development`
built successfully. Editor logs confirm the PatrickFTaylorHall world and asset
registry finished loading. Loading took several minutes with low available RAM.
Viewport capture failed in desktop automation, so a visual building walkthrough
has not been signed off. These checks establish setup and loading, not gameplay QA.

## Student-facing code

Keep our new plugin source in this Git repository. Copy/install it into your
local Unreal project for building and testing. Keep the original twin and shared
plugin in Perforce. Never commit their source, `.uasset`, or `.umap` files to GitHub.
