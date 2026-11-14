# Tasks 1 through 6

The six task pull requests below were merged into `dev` on October 24, 2026.
They were originally stacked in task order. Their branch names and links are
kept here as a record of that work. Start new independent tasks from current
`dev`; those original branches have been deleted from the remote.

- Task 1: [PR #4](https://github.com/CSC-3380-Fall-2026/Team-16/pull/4)
- Task 2: [PR #5](https://github.com/CSC-3380-Fall-2026/Team-16/pull/5), based on task 1
- Task 3: [PR #6](https://github.com/CSC-3380-Fall-2026/Team-16/pull/6), based on task 2
- Task 4: [PR #7](https://github.com/CSC-3380-Fall-2026/Team-16/pull/7), based on task 3
- Task 5: [PR #8](https://github.com/CSC-3380-Fall-2026/Team-16/pull/8), based on task 4
- Task 6: [PR #9](https://github.com/CSC-3380-Fall-2026/Team-16/pull/9), based on task 5

| Task | Branch | Purpose |
| --- | --- | --- |
| 1 | `task-1-perforce-setup` | Perforce onboarding and local setup verification |
| 2 | `task-2-unreal-project-setup` | UE 5.8 loading/build instructions and recorded evidence |
| 3 | `task-3-directory-json-validation` | Reuse the JSON schema and validate malformed directory data |
| 4 | `task-4-unreal-json-loader` | Load/search JSON in a separate UE plugin and install it locally |
| 5 | `task-5-room-destination-markers` | Resolve search results to map-derived markers |
| 6 | `task-6-exploration-controls` | Search panel, flying camera, mouse control, and return to player |

## Scope and remaining acceptance checks

Jayden confirmed UE 5.8 replaces the old board's Unity references. Team Perforce
access is expected but each teammate must verify their own machine. The local
engine, compiler, SDK, Perforce login, build, and map loading were checked.

Task 3 reused the team's existing room/course schema and placeholder rows.
Tasks 4 through 6 add only our plugin, tools, docs, and destination data to GitHub.
The twin's original source and assets remain in Perforce. Local plugin integration
is in pending changelist 41648 and has not been submitted.

Six room destinations were matched to unique map triggers. The team later
removed mockup rooms 1160, 1180, and 1220 from the directory. Current `dev`
contains 68 rooms, 34 faculty records, 6 departments, 1 placeholder course,
and 6 room bindings. See [data sources](data-schema.md) for the directory's
provenance. These records do not establish verified course assignments or
walking routes. A visual building walkthrough is still needed.

Tasks 7 through 10, including routing and a first-person guide, are outside this
implementation. The local Perforce integration remains pending; the GitHub
task PRs have been merged into `dev`.

## Final checks on October 17, 2026

The UE 5.8 editor build passed. All 17 Python tests and all 3 Unreal automation
tests passed, with no warnings in the final Unreal test report. The Unreal tests
cover loader errors and reloads, search-to-marker selection, cursor/input locks,
camera focus, and restoring the original pawn. A small DX12 game test rendered
the panel; that confirms its layout, not the building's appearance or performance.
That render showed MegaLights hardware-raytracing warnings on the test machine's
GTX 1080 under the twin's existing renderer settings. Those settings were not edited.

## Room browser update on October 31, 2026

Branch `task-room-browser` adds a room list for blank searches, floor filtering,
the existing room, department, faculty, and course details, and a button to clear the selected
marker. Unbound rooms show details while the room-view button remains disabled.
The panel scrolls so longer details do not hide its controls.

The UE 5.8 editor build, all 22 Python tests, and all 4 Unreal automation tests
passed. The Unreal report contains no test warnings. Tests cover browsing by
floor, filtering course results by their room floor, room details, clearing the
marker, and disabling camera movement for unbound rooms. An offscreen render
confirmed that the room list and floor selector display. Unreal tests ran from
the engine's Entry map; they do not verify building navigation. The integration
remains in pending Perforce changelist 41648. Routing and first-person guidance
remain unfinished.

## First-floor guidance

The Windows demo has first-person walking directions on the first-floor map.
Select a reachable destination and choose **Start walking route** to see a gold
trail, turn instructions, and arrival feedback. Rooms without a connected walk
can still offer a preview. The course directory covers selected Fall 2026 CSC
lectures. See [first-floor build and verification](first-floor-build.md).
