# Contributing

Thanks for wanting to help. This is a small project with a small set of rules, and they are all here.

## Before you start

For anything larger than a bug fix, open an issue first. It is cheaper to disagree about an approach in
a paragraph than in a pull request.

## Building

Visual Studio 2022 with the **Desktop development with C++** workload. Nothing else — the project has
no external dependencies and adding one is a design change, not a detail.

```bash
tools\build.bat release
```

While iterating on a single file, `tools\syntax-check.bat src\ui\Widgets.cpp` compiles just that
translation unit and is much faster than a full build.

## What a pull request needs

- **A clean build.** `tools\build.bat release` produces no warnings today. Keep it that way.
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
