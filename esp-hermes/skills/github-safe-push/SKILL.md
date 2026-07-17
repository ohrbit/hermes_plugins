---
name: github-safe-push
description: "Push files to a GitHub repo WITHOUT git branch chaos — uses the GitHub Contents API (PUT) directly, bypassing local git state. Use when local git branches are messy, commits landed on wrong branch, or you need idempotent repo updates. Created after esp-hermes branch-hell."
---

# GitHub Safe Push (API-based, no branch chaos)

## When to use
- Local git repo is in branch hell (multiple stale branches, commits on wrong branch)
- You need to push specific files to `main` reliably
- Idempotent re-pushes (update existing file = 200, not conflict)

## The problem it solves
`git push origin <localbranch>:main` with wrong local checkout → commits land
on wrong branch, local `main` goes stale, files missing, double-nested dirs
(`esp-hermes/esp-hermes/`). We hit this on the esp-hermes firmware push.

## Solution
`/root/.hermes/scripts/gh_push_safe.py` — pushes via GitHub Contents API
(`PUT /repos/{owner}/{repo}/contents/{path}`). No local git state involved.
Reads `GITHUB_TOKEN` from `/root/.hermes/.env`.

## Usage
```bash
# single file
python3 /root/.hermes/scripts/gh_push_safe.py <local_file> <repo_path> [msg]

# whole dir (recursive)
python3 /root/.hermes/scripts/gh_push_safe.py --dir <local_dir> <repo_prefix> [msg]
```

Example:
```bash
python3 /root/.hermes/scripts/gh_push_safe.py \
  esp-hermes/firmware/main/io_tools.c \
  esp-hermes/firmware/main/io_tools.c \
  "fix: v6 compat"
```

## Rules to prevent chaos
1. **Prefer API push** over `git push X:main` for single-file updates.
2. Before any `git push`, run `git branch` + `git status` to confirm checkout.
3. Never `mkdir -p <repo>` inside an already-cloned `<repo>/` — causes nesting.
4. Delete stale branches after merge: `git branch -D esp-hermes-*`.
5. Verify after push: `curl -s -o /dev/null -w "%{http_code}" <api_url>` → 200.
