# Git Workflow

## Branches
- `main` - stable version, only updated from `dev`
- `dev` - where all our work gets merged
- `task-<purpose>` or `task-<number>-<purpose>` - one descriptive branch per task

Don't commit straight to `main` or `dev`, use a pull request.

## Steps for a task
```sh
git checkout dev
git pull
git checkout -b task-your-task-name

# do your work, then
git add .
git commit -m "Short description of what you changed"
git push -u origin task-your-task-name
```

Then open a pull request on GitHub into `dev` (not `main`) and have someone review it before merging.

Use a clear title such as `Feature: ...`, `Fix: ...`, `Docs: ...`, `Test: ...`, or
`Cleanup: ...`. Add a matching GitHub label, such as `enhancement`, `bug`, or
`documentation`, and state what changed and how it was checked. Small bug fixes,
test improvements, and documentation updates can each be their own PR.

## Stacked branches
If your task needs another task that isn't merged yet, you can branch off that task's branch instead of `dev` and point your PR at it. Just put that in the PR description so we know the order.

When merging stacked PRs:
1. Merge the first one into `dev` with **Create a merge commit** (not squash, squashing makes the next PR in the stack show the same changes again)
2. Click **Delete branch**. GitHub then moves the next PR over to `dev` on its own
3. The check doesn't start again when the PR gets moved, so close and reopen the PR to run it
4. Once the check is green, merge it and repeat

## Merging into main
Once a set of tasks is merged and working in `dev`, open a PR from `dev` into `main`. Don't delete `dev` after.

## Git vs Perforce
- Perforce has the Digital Twin Unreal project
- GitHub has our data files, docs, and scripts
- Don't commit Digital Twin files (`.uasset`, `.umap`, etc.) to GitHub
