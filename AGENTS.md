# Project rules (Denarmo/emuden)

These rules cover the EmuDEN app. Other repositories open in the same workspace
(`emuden-plugins`, `emuden-switch2`, engine forks) follow their own AGENTS.md.

## Committing and pushing

- When a task is finished and the Windows build and tests pass
  (`powershell -ExecutionPolicy Bypass -File tools\build.ps1 -Preset win -Test`),
  commit it to `main` and push. Don't wait to be asked.
- Other agents may be working in this same checkout. Stage only the files your task
  changed (`git add <paths>`, never `git add -A` or `git commit -a`), and leave
  everyone else's uncommitted changes as they are.
- If uncommitted changes from another task stop the build, check your files in a
  clean worktree from `origin/main` (`git worktree add`), commit there and push
  that commit. Don't commit, revert or stash the other task's files.
- For long or risky work, use a branch or worktree of your own and merge it into
  `main` once its checks pass.
- Pull with `--ff-only` before pushing. Never force-push or rewrite pushed history.
- Don't trigger, re-run or add GitHub Actions workflows; builds and tests run on
  this PC.
- Commit messages: imperative subject, then a body saying what changed and what
  was tested (and what wasn't, such as on-screen checks or devices).
