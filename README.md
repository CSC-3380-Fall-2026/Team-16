# PFT Navigator : Team 16
# Members
Project Manager: [Name] ([GitHub Name])\
Communications Lead: [Name] ([GitHub Name])\
Git Master: Malek Badawi ([GitHub Name])\
Design Lead: Claire Bonius ([claire-coded])\
Quality Assurance Tester: Laila Holland ([lailaholland])

# About Our Software

Our idea is to make the PFT Digital Twin more student-facing. We already have permission to use the existing digital twin as our foundation, so instead of rebuilding that, we're adding a layer that helps students actually use it. The Digital Twin will have four main steps. First, Explore, students can look around the PFT in 3D. Second, search-they can search for a specific room, professor, or destination. Third, route, the system gives them directions on where they need to go. Finally, there's walk, they can switch to a first person view and follow the route through the building.

The Digital Twin itself is on LSU's Perforce server. This repo has our data files (rooms, faculty, courses, departments), docs, and a script to check the data. Digital Twin files should not be committed here.

## Platforms Tested on
- Not tested yet, will update once we have a build running

# Important Links
Kanban Board: [link]\
Designs: [link]\
Styles Guide(s): [Code Standards](docs/style-guide.md)

# How to Run Dev and Test Environment

## Dependencies
- Python 3.10+ (for the data check script)
- Unreal Engine (version TBD, has to match the Digital Twin project)
- Perforce P4V (to access the Digital Twin)
### Downloading Dependencies
- Python: https://www.python.org/downloads/
- Unreal Engine: through the Epic Games Launcher, https://www.unrealengine.com/download
- Perforce P4V: https://www.perforce.com/downloads

## Commands
Steps for launching the app will be added once we're set up in Unreal. For now, run these from the root of the repo to check the data files:

```sh
python tools/validate_data.py
```

```sh
python -m unittest discover -s tools
```

These also run automatically on every pull request. On Mac you may need to use `python3` instead of `python`.
