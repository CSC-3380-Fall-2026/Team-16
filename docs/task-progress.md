# Tasks 1 through 6

These branches are stacked in task order from `dev`. Each branch adds one task
to the preceding task's work. Merge/review them in order, or review task 6 as the
combined implementation. No branch has been merged into `main` or `dev`.

The six pull requests are open. Merge task 1 into `dev` first, then retarget the
next task's PR to `dev` before merging it. Continue in task order.

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

Six room destinations were matched to unique map triggers. Rooms 1160, 1180,
and 1220 remain unbound. Full room descriptions redirect to LSU sign-in, and
course assignments are still placeholders. Confirm this information before
treating the app as a real directory. A visual building walkthrough is still
needed; desktop viewport capture was unavailable during this work.

Tasks 7 through 10, including routing and a first-person guide, are outside this
implementation. No Perforce changelist has been submitted, and no pull request
has been merged.

## Final checks on October 17, 2026

The UE 5.8 editor build passed. All 17 Python tests and all 3 Unreal automation
tests passed, with no warnings in the final Unreal test report. The Unreal tests
cover loader errors and reloads, search-to-marker selection, cursor/input locks,
camera focus, and restoring the original pawn. A small DX12 game test rendered
the panel; that confirms its layout, not the building's appearance or performance.
That render showed MegaLights hardware-raytracing warnings on the test machine's
GTX 1080 under the twin's existing renderer settings. Those settings were not edited.
