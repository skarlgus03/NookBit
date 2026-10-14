# Windows runtime packaging

Build with `cmake --build --preset package-release`. The package is generated at
`out/package/NookBit-Release`; zip the whole directory for distribution.

`NOOKBIT_COMPACT_PACKAGE` defaults to ON and affects only that package, not the
installed Qt SDK or the development build's runtime. `windeployqt` still discovers
the application's dependencies before the package-specific exclusions run.

Excluded components:

- QML debugging/profiling plugins (`qmltooling`).
- Non-SQLite database drivers: InterBase, Mimer, Oracle, ODBC and PostgreSQL.
- Fusion, Imagine, Material, Universal, FluentWinUI3 and Windows Quick Controls
  styles, their implementation DLLs and their style-specific fallback dialogs.
  The application explicitly selects Basic in `main.cpp`.

Retained components include Basic and its default dialogs, Qt Quick rendering,
image format plugins, SQLite, Windows integration, the offscreen test platform,
graphics compiler/software-rendering fallback DLLs, the VC runtime installer and
the test character ImagePack. PNG/atlas character rendering uses this retained
runtime; adding a character pack does not require the excluded control styles.

The exclusion script only removes named generated runtime components under the
Release package. It checks resolved paths before removal. User settings, source
files and the Qt installation are not cleanup targets. Repackaging removes stale
excluded components left by older full packages.

To restore the full deployment, configure with
`cmake --preset release -DNOOKBIT_COMPACT_PACKAGE=OFF` and package again. Turn it
back ON to restore the compact policy. Revisit the exclusions when changing the
application's control style or database backend.

Validate the packaged executable without the Qt SDK on PATH (`--verify-ui`,
`QT_QPA_PLATFORM=offscreen`, `QT_QUICK_BACKEND=software`, and unset Qt/QML plugin
path overrides). This verifies packaged UI/character resources but does not
replace Windows graphics-driver and native dialog testing on end-user machines.

## Measured first pass (2026-10-14, Qt 6.11.2)

- Before: about 142.2 MiB / 1,458 files.
- Compact package: about 125.1 MiB / 193 files (about 17.1 MiB saved).
- ZIP of that directory: about 66.0 MiB / 193 files.
- Packaged `--verify-ui` passed with the Qt SDK removed from PATH and no external
  Qt/QML plugin path overrides.

These are measurements of this build, not fixed size guarantees. Graphics
compatibility libraries and the VC redistributable account for much of the
remaining size and are deliberately retained. The local archive
`out/package/NookBit-Release-compact.zip` is a snapshot; recreate it after rebuilding
the package before distributing a later version.
