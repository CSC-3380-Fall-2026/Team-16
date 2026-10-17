# Git Workflow

## Branches
- `main` - stable version, only updated from `dev`
- `dev` - where all our work gets merged
- `feature/<task-name>` - one branch per task

Don't commit straight to `main` or `dev`, use a pull request.

## Steps for a task
```sh
git checkout dev
git pull
git checkout -b feature/your-task-name

# do your work, then
git add .
git commit -m "Short description of what you changed"
git push -u origin feature/your-task-name
```

Then open a pull request on GitHub into `dev` (not `main`) and have someone review it before merging.

## Git vs Perforce
- Perforce has the Digital Twin Unreal project
- GitHub has our data files, docs, and scripts
- Don't commit Digital Twin files (`.uasset`, `.umap`, etc.) to GitHub
