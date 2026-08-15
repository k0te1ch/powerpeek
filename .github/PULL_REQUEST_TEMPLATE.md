<!--
The PR title becomes the commit on main and drives the release version, so it has to be a valid
Conventional Commit: feat: / fix: / docs: / refactor: / perf: / test: / build: / ci: / chore:
-->

## What this changes

<!-- One paragraph. Why, not just what. -->

## Checklist

- [ ] `tools\build.bat release` builds with no warnings
- [ ] `tools\test.bat` passes, and behaviour changed under `PowerPeekCore` brought a test with it
- [ ] New user-visible strings are in `src/core/Strings.h`, both English and Russian
- [ ] No new external dependency in the application
- [ ] The interface does not name an API, registry key or file format
- [ ] Tried it on a real controller, or explained below why that was not possible
