# PFT Navigator : Team 16
# Members
Project Manager: Jayden Hong ([jaydenhongfails])\
Communications Lead: Josemaria Ramos ([jramo18])\
Git Master: Malek Badawi ([malekbadawi21])\
Design Lead: Claire Bonius ([claire-coded])\
Quality Assurance Tester: Laila Holland ([lailaholland])

# About Our Software

Our idea is to make the PFT Digital Twin more student-facing. We already have permission to use the existing digital twin as our foundation, so instead of rebuilding that, we're adding a layer that helps students actually use it. The Digital Twin will have four main steps. First, Explore, students can look around the PFT in 3D. Second, search-they can search for a specific room, professor, or destination. Third, route, the system gives them directions on where they need to go. Finally, there's walk, they can switch to a first person view and follow the route through the building.

The Digital Twin itself is on LSU's Perforce server. This repo has our data files (rooms, faculty, courses, departments), docs, and a script to check the data. Digital Twin files should not be committed here.

## Platforms Tested on
- Windows (UE 5.8 editor build, see [task progress](docs/task-progress.md))
- Mac not tested yet

# Important Links
Kanban Board: [link]\
Designs: [Figma](https://www.figma.com/design/nF6y5i40ezSFQVI5zOSS9M/PFT-Navigator-UI---Team-16), [Design doc](docs/design.md)\
Styles Guide(s): [Code Standards](docs/style-guide.md)\
Git Workflow: [Branches and pull requests](docs/git-workflow.md)

# How to Run Dev and Test Environment

## Dependencies
- Python 3.10+ (for the data check script)
- Unreal Engine 5.8, matching the Digital Twin project
- Perforce P4V (to access the Digital Twin)
### Downloading Dependencies
- Python: https://www.python.org/downloads/
- Unreal Engine: through the Epic Games Launcher, https://www.unrealengine.com/download
- Perforce P4V: https://www.perforce.com/downloads

## Commands
See [Perforce setup](docs/perforce-setup.md) and [Unreal setup](docs/unreal-setup.md)
to sync, build, and open the existing digital twin. Run these from this repo's root
to check the directory data:

```sh
python tools/validate_data.py
```

```sh
python -m unittest discover -s tools
```

These also run automatically on every pull request. On Mac you may need to use `python3` instead of `python`.

## Navigator prototype

Our UE 5.8 plugin adds local room/course search, destination markers, and flying
exploration controls. See [plugin installation and controls](docs/navigator-plugin.md)
and [task branches and remaining checks](docs/task-progress.md). The original
digital twin stays in Perforce; GitHub contains our separate plugin source.
