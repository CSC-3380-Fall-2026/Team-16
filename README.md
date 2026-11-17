# PFT Navigator · Team 16

PFT Navigator helps students find rooms and classes inside Patrick F. Taylor Hall. The Windows MVP runs in the existing Unreal Engine 5.8 digital twin. It includes a local directory, room previews and first-floor walking directions with animated arrows, an overview map, and a list of upcoming turns. Class filters use selected Fall 2026 CSC lecture meetings.

The GitHub repo contains the Navigator plugin, JSON data, build tools, tests, and docs. LSU Perforce holds the Unreal project, building assets, and shared `DigitalTwinTools` plugin. Building from source requires access to both.

## Play the packaged Windows demo

1. Get the Windows demo ZIP from the team. Allow about 3 GB for the ZIP and extracted demo.
2. Extract the entire ZIP to a local folder and open **Play PFT Demo Offline.cmd** in the `Windows` folder. If Windows reports a missing Visual C++ runtime, run the included `Engine\Extras\Redist\en-us\vc_redist.x64.exe`, then open the demo again.
3. Choose **Skip tour**. In PFT Navigator, search **Commons**, select **PFT Commons**, and choose **Preview route** or **Walk there**. Walk with WASD and hold the left mouse button in the scene to look around.
4. To try class search, enable **Find a class by instructor or time** and enter **Duncan** in the instructor field. Choose **Wednesday** and **10:30 AM** to find **CSC 1350** in **PFT 1200**.

## Build the Windows demo from source

Install Unreal Engine **5.8**, Microsoft C++ Build Tools with the Windows SDK and .NET Framework 4.8 SDK, Python **3.10+**, and Perforce P4V with the `p4` CLI. Follow [Windows Unreal setup](docs/unreal-setup.md) and [Perforce setup](docs/perforce-setup.md). Use your own Perforce login and verify your workspace maps both the PFT project and the shared plugin.

Use a clean Perforce workspace and a pending changelist for the build. Sync the project and shared plugin, then apply first-floor shelf **42426** in that workspace. Run the final build commands from the root of this Git checkout.

```powershell
# Use your synced project path and the number of your pending changelist.
$Project = 'C:\path\to\PFTDigitalTwin.uproject'
$Change = Read-Host 'Pending Perforce changelist number'

Push-Location (Split-Path -Parent $Project)
p4 login -s
p4 where PFTDigitalTwin.uproject
p4 where Plugins/DigitalTwinTools/DigitalTwinTools.uplugin
p4 unshelve -s 42426 -c $Change
Pop-Location

python tools/install_navigator.py $Project --changelist $Change
python tools/install_navigator.py $Project --check
& .\tools\package_first_floor_demo.ps1 -Project $Project -FullCook -VerifyOffline -ArchiveDirectory "$env:USERPROFILE\PFTDemo"
```

Resolve any Perforce conflicts and fix any setup or installer error before continuing. The first cook can take a while because Unreal prepares the map and its dependencies. A successful run publishes `PFTDemo\Windows` in your user folder. The package's `PFTDemoBuild.json` records the source revision, map, and test results.

Use `-EngineRoot` with the package command if Unreal 5.8 is installed in a custom location. For later C++ changes on the same machine, see the [cook reuse and troubleshooting instructions](docs/first-floor-build.md). Keep the Unreal project, assets, and generated build output in their local or Perforce locations.

## Run checks

From the Git checkout, run the directory validator and Python tests.

```powershell
python tools/validate_data.py
python -m unittest discover -s tools
```

From the packaged `Windows` folder, run the offline checks with the game closed.

```powershell
& '.\Play PFT Demo.cmd' verifyoffline
```

The offline suite checks startup and onboarding, walking to the Commons, guided arrival, the cross-building route to C1007, and automatic rerouting. See [Navigator controls](docs/navigator-plugin.md) and the [first-floor build guide](docs/first-floor-build.md) for more checks.

## Team and design

| Role | Member |
| --- | --- |
| Project Manager | Jayden Hong (jaydenhongfails) |
| Communications Lead | Josemaria Ramos (jramo18) |
| Git Master | Malek Badawi (malekbadawi21) |
| Design Lead | Claire Bonius (claire-coded) |
| Quality Assurance | Laila Holland (lailaholland) |

[Design in Figma](https://www.figma.com/design/nF6y5i40ezSFQVI5zOSS9M/PFT-Navigator-UI---Team-16) · [Design notes](docs/design.md) · [Code standards](docs/style-guide.md) · [Git workflow](docs/git-workflow.md)
