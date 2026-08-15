# Contributing

Thanks for wanting to help. This is a small project with a small set of rules, and they are all here.

## Before you start

For anything larger than a bug fix, open an issue first. It is cheaper to disagree about an approach in
a paragraph than in a pull request.

## Building

Visual Studio 2022 with the **Desktop development with C++** workload. Nothing else — the shipped
application has no external dependencies and adding one is a design change, not a detail. The tests
are the single exception, and they are confined to the test binary; see below.

```bash
tools\build.bat release
```

While iterating on a single file, `tools\syntax-check.bat src\ui\Widgets.cpp` compiles just that
translation unit and is much faster than a full build.

## Tests

```bash
tools\test.bat
```

That configures, builds and runs the suite; pass anything else through to `ctest`, so
`tools\test.bat -R eventDetector` runs one unit's cases. CI runs the identical script.

Tests link `PowerPeekCore`, the library holding everything with no window, no device and no message
loop: JSON, settings, the localisation table, the event rules, the battery history and the easing
maths. That split is what makes the suite possible at all — a build agent has no controller, no audio
endpoint and no interactive session, so anything above that line cannot be covered here and is not
pretended to be.

Two consequences worth knowing before you write a test:

- **Nothing may read the clock, the locale or `%LOCALAPPDATA%`.** The units that care about time take
  it as an argument for exactly this reason; anything needing a path gets one from `TempDir`.
- **`tools\test.bat` is the only part of the build that reaches the network**, because doctest is
  fetched at configure time. It lives on its own CMake preset so an ordinary build of the application
  still works with no connection at all.

A change to behaviour in that library wants a test. A change to the drawing code cannot have one, and
saying so in the pull request is better than inventing a test that asserts nothing.

## Branches and pull requests

`main` is always releasable, and nothing lands on it except by merging a pull request — pushing
straight to it is refused by the repository, for everyone.

```bash
git switch main && git pull
git switch -c feat/short-description
```

Name the branch for the change: `feat/`, `fix/`, `docs/`, `refactor/`, `chore/`. One branch per
change, and a new change always starts from a freshly pulled `main` rather than from the last branch.

**Commits have to be signed.** `main` requires it, and an unsigned commit does not fail loudly — the
pull request simply refuses to merge with everything else green, which is a confusing half hour if you
do not know to look. SSH signing needs no new key: add the public half of the key you already push
with to GitHub a second time, choosing **Signing Key** rather than Authentication Key, then

```bash
git config gpg.format ssh
git config user.signingkey ~/.ssh/id_ed25519.pub
git config commit.gpgsign true
```

Those are repository-local on purpose, so nothing changes for your other work. If you have already
made unsigned commits on the branch, `git rebase -f --gpg-sign main` re-signs them in place.

Merge with **squash**, which is why the pull request *title* carries the Conventional Commit prefix:
that title becomes the single commit on `main`, and release-please reads it to decide the next version.
Delete the branch afterwards.

Releases are not cut by hand. release-please keeps a release pull request up to date from the commits
on `main`; merging it writes the changelog, bumps `version.txt`, tags, and the release workflow builds
and attaches the installer and the portable zip. Never edit `version.txt` or `CHANGELOG.md` yourself.

## What a pull request needs

- **A clean build.** `tools\build.bat release` produces no warnings today. Keep it that way.
- **A green suite.** `tools\test.bat` passes, and a change to behaviour under `PowerPeekCore` brings
  the test that would have caught the old behaviour.
- **A Conventional Commit title.** The PR title becomes the commit on `main` and drives the version
  bump, so it has to parse: `feat:`, `fix:`, `docs:`, `refactor:`, `perf:`, `test:`, `build:`, `ci:`,
  `chore:`, with `!` for a breaking change.
- **Both languages.** Every user-visible string lives in `src/core/Strings.h` with an English and a
  Russian column. A missing translation is a compile error, which is deliberate.
- **No implementation detail in the interface.** The settings page tells the user what happens, never
  which API, registry key or file format makes it happen.
- **One change per pull request.** Two good changes in one PR are harder to review than two PRs.

## Style

Read the file you are editing; it is consistent and it is the specification.

- Comments explain **why**. If a comment restates the code, delete it. Where a Win32 API has a trap,
  name the trap — those comments are the valuable ones.
- 4-space indent, 100-column soft limit, `m_` members, `k` constants, PascalCase types, camelCase
  functions.
- `winrt::com_ptr` over raw COM pointers. Never swallow an error: handle it, or log it and degrade in
  a way the user can actually perceive.
- No dead code, no commented-out code, no TODO placeholders.

## Reporting a bug

Battery reporting varies a lot by controller and connection, so the useful details are: which
controller, how it is connected, your Windows version, and what the app showed versus what you
expected. The log at `%LOCALAPPDATA%\PowerPeek\PowerPeek.log` usually explains the
rest — it contains no personal data, only device names and battery readings.

## Licence

Contributions are accepted under [GPL-3.0-or-later](LICENSE), the same licence as the project.
