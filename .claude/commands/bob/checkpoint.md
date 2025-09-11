---
name: checkpoint
allowed-tools: Bash(git add:*), Bash(git status:*), Bash(git commit:*)
description: Create a git commit from recent changes
---

## Context

- Current git status: !`git status`
- Current git diff (staged and unstaged changes): !`git diff HEAD`
- Current branch: !`git branch --show-current`
- Recent commits: !`git log --oneline -5`

## Your task

Based on the above changes, create a single git commit. 
- Make sure your commit description up to 70 characters, 50 ideally. 
- you are strictly forbidden to put anything into the commit description. 
- you can prefix the commit message with comma-separated list of affected root folders.
    Examples: 
    - if files changed were inside rive and visemes folder you will prefix like this:
        rive, visemes: {actual commit message}
    - for rive only:
        rive: {actual commit message}
