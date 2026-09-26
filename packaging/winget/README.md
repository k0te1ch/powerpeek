# winget manifest templates

Templates for the [Windows Package Manager](https://learn.microsoft.com/windows/package-manager/)
manifest set, schema 1.6.0. They are not submitted anywhere by themselves -- the release
workflow (`.github/workflows/release-assets.yml`) renders them for each release and uploads
the result as a release asset (`winget-manifests-<version>.zip`), for whoever does the actual
submission to hand to `winget` tooling directly.

## Why the installer, not the portable zip

Two assets ship with every release: `PowerPeek-Setup-<version>.exe` (Inno Setup) and
`PowerPeek-<version>-portable.zip`. The manifest points at the installer:

- winget's `zip` installer type expects an archive whose *contents* are declared with
  `NestedInstallerType: portable` and a `PortableCommandAlias`, which drops a copy under
  `WindowsApps` and manages its own PATH entry and uninstall registration -- a second,
  winget-owned install layout next to the one this project already documents
  (`%LOCALAPPDATA%\PowerPeek`, manual deletion). It works, but it duplicates machinery this
  project maintains itself in `installer/PowerPeek.iss`.
- The Inno installer already has a proper uninstall entry, a Start Menu shortcut with the
  right AppUserModelID for toasts, and -- since `PrivilegesRequiredOverridesAllowed=dialog`
  -- both a per-user and a machine-wide mode from the same file. winget's `inno` installer
  type maps onto exactly this: `InstallerSwitches.Silent` for unattended installs, and two
  `Scope` entries (`user` / `machine`) so `winget install --scope machine` resolves the
  install-mode dialog the setup would otherwise show.

So the installer is the more robust winget target: it reuses machinery already tested by
this project's own CI, rather than asking winget to reimplement portable-app bookkeeping
against a zip that was built for people who just want a single file to copy around.

## Files

| File | Purpose |
|---|---|
| `k0te1ch.PowerPeek.yaml` | Version manifest. |
| `k0te1ch.PowerPeek.installer.yaml` | Installer manifest -- URL, SHA-256, silent switches, scope. |
| `k0te1ch.PowerPeek.locale.en-US.yaml` | Default locale (`DefaultLocale` in the version manifest). |
| `k0te1ch.PowerPeek.locale.ru-RU.yaml` | Russian locale. |

Each file has three placeholders the CI job substitutes before uploading:

- `__VERSION__` -- the release version, e.g. `1.2.0`.
- `__INSTALLER_URL__` -- the download URL of `PowerPeek-Setup-<version>.exe` on the release.
- `__INSTALLER_SHA256__` -- its SHA-256, upper-cased (the existing `tools\package.ps1`
  already writes a lower-case `.sha256` sidecar for it; the CI job upper-cases it, since
  that is the casing the winget manifest schema and `winget validate` expect).

## What CI does, and does not do

The `winget-manifest` job in `release-assets.yml` runs after the installer and portable zip
are attached to the release. It renders the four files above into `dist/winget/`, zips them,
and uploads that zip as a release asset. That is the whole job -- it does **not** open a pull
request against [microsoft/winget-pkgs](https://github.com/microsoft/winget-pkgs), and it does
not run on a schedule or push to any other repository.

Submitting a new version to winget-pkgs needs a token with permission to push a branch and
open a PR on a fork of microsoft/winget-pkgs, and that token is a repository secret only the
owner can add -- so it is not something this template set can wire up on its own.

### Wiring up automatic submission (optional, owner action)

Once `WINGET_PKGS_TOKEN` (a fine-grained PAT or classic PAT with `public_repo`, from an
account that can fork microsoft/winget-pkgs) is added as a repository secret, a step can be
appended to the `winget-manifest` job to submit automatically, using either tool:

```yaml
      - name: Submit to winget-pkgs
        if: ${{ secrets.WINGET_PKGS_TOKEN != '' }}
        shell: pwsh
        run: |
          winget install --id Microsoft.WingetCreate -e --silent
          wingetcreate update k0te1ch.PowerPeek `
            --version $env:VERSION `
            --urls "$env:INSTALLER_URL" `
            --submit `
            --token $env:WINGET_PKGS_TOKEN
        env:
          VERSION: ${{ ... }}
          INSTALLER_URL: ${{ ... }}
          WINGET_PKGS_TOKEN: ${{ secrets.WINGET_PKGS_TOKEN }}
```

or, with [komac](https://github.com/russellbanks/Komac) instead of `wingetcreate`:

```yaml
      - name: Submit to winget-pkgs
        if: ${{ secrets.WINGET_PKGS_TOKEN != '' }}
        shell: pwsh
        run: |
          komac update k0te1ch.PowerPeek `
            --version $env:VERSION `
            --urls "$env:INSTALLER_URL" `
            --submit `
            --token $env:WINGET_PKGS_TOKEN
        env:
          VERSION: ${{ ... }}
          INSTALLER_URL: ${{ ... }}
          WINGET_PKGS_TOKEN: ${{ secrets.WINGET_PKGS_TOKEN }}
```

Either tool re-derives the manifests from the installer URL rather than reading the templates
in this folder, so keeping the templates and the automated step in sync (tags, descriptions,
locales) is a manual concern if that step is added later.

### First submission is manual regardless

`wingetcreate`/`komac` can only *update* an existing package. The very first submission of
`k0te1ch.PowerPeek` has to be created by hand -- see the package identifier note in the
project's Obsidian plan, or just run:

```
winget install wingetcreate
wingetcreate new https://github.com/k0te1ch/powerpeek/releases/download/v<version>/PowerPeek-Setup-<version>.exe
```

and follow its prompts, or copy the rendered manifests out of the release's
`winget-manifests-<version>.zip` asset and open the PR against microsoft/winget-pkgs by hand.
