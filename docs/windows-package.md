# Windows runtime packaging

Build with `cmake --build --preset package-release`. The package is generated at
`out/package/NookBit-Release`.

## Versioned test archive

Run `cmake --build --preset archive-release` to update the package and create:

- `out/package/NookBit-<version>-Windows-x64.zip`
- The matching `.zip.sha256` checksum file.

The ZIP contains the entire `NookBit-Release` directory, including `nookbit.exe`,
the deployed runtime, bundled packs, `README.txt` quick-start instructions and
`version.json`. Extract everything before launching the executable. Personal
preferences, imported packs, pending files and history are stored outside the
application directory and are not added by the packaging target.

The core version is `project(VERSION ...)` in the root CMakeLists.txt;
`NOOKBIT_PRERELEASE` supplies the current pre-release identifier. They form one display version used by
Qt application metadata, Settings > Information, Windows executable properties,
package metadata, instructions and the archive filename. `nookbit.exe --version`
prints the same value without loading personal settings or starting features.
For a stable build, configure `-DNOOKBIT_PRERELEASE=`; for a later test build,
set the next identifier before building. Do not reuse a published version for
different contents. This target creates local artifacts; it does not create a
Git tag, publish a GitHub release or upload any files.

The archive is written to a temporary file before replacing a previous local ZIP.
Its SHA-256 file is generated after success. To inspect it on Windows, use
`Get-FileHash <archive.zip> -Algorithm SHA256` and compare with the `.sha256` text.
Creating the ZIP does not perform a fresh test run; run the relevant CTest checks
before sharing it. A clean-machine/native-desktop test still needs a tester.

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

## Local test archive (2026-10-29)

`0.1.0-alpha.1` contains 250 ZIP entries and is about 69.1 MiB compressed.
The application-version, UI smoke, QML page-boundary, UI metric and saved-all-off
startup checks passed. The ZIP was extracted into a fresh verification directory;
its checksum, manifest, native file version and command-line version matched.
The extracted app passed isolated feature startup with the Qt SDK removed from
PATH and no external Qt/QML plugin-path overrides. No pending-list, watch settings,
database, timing log or PDB files were found in the archive. The temporary extracted
directory was removed after verification. This is local test packaging; no GitHub
release has been published by these build targets.
