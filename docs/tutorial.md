# Read-only tutorial

The tutorial explains how to use NookBit. It does not configure features, import
rules, scan folders, enqueue paths, or move files. Configuration remains in each
feature's normal UI. Reading, navigating, skipping and completing the tutorial
cannot invoke feature commands.

## User flow

- A fresh installation opens the tutorial once. Previous/Next navigate pages;
  the table-of-contents dropdown jumps to any page. The final Next becomes Done.
- The host supplies Welcome and Finish. Active built-in modules supply their own
  pages: File Organizer contributes sorting, rules and history; Companion, Folder
  Watch and Notifications contribute their respective introductions. A module
  may supply multiple pages or none. Disabled modules contribute no pages.
- The page explicitly states that it describes currently enabled base modules.
  Extensions may register their own instructions through the same contribution
  API; the host does not inspect feature identities to render them.
- Skip records completion without changing feature settings. Closing/minimizing
  the application during first-run reading does not record completion.
- Settings → Information → **튜토리얼 다시 보기** starts from the first page.
  Close/Done returns to Settings and leaves completion records unchanged.
- `tutorial/completed` records explicit first-run completion or skipping. When
  absent, the previous `onboarding/completed` marker is respected; existing users
  are not forced through the tutorial again. No feature settings are migrated.

## Contribution contract

`UiPageRegistry::addTutorialSteps(QList<UiTutorialStep>, QObject& lifetime)` accepts
read-only value data: stable `id`, `ownerId`, title, summary, nonempty instructions,
optional note and ordering number. Empty fields and duplicate IDs are rejected
atomically. Registration must occur on the registry's UI thread, with a lifetime
object on that same thread. Destroying that object removes only its contributions.
Ordering is deterministic by number and then stable ID. Queries return copies.

Each module registers its text in its own `registerUi()` composition code. The
host assembles active modules as before; it owns no module-specific tutorial text.
No QML URLs, callbacks, controller pointers or arbitrary settings are accepted in
tutorial data. `TutorialPage.qml` renders this data generically; `TutorialController`
receives only the completion-preference store and a persistence flag. Its only
commands reopen, finish and skip. It has no access to feature runtimes.

This first version provides reading guidance rather than an interactive overlay
or simulated file workflow. If richer demonstrations are added later, they should
preserve the separation from real feature commands and user files.

## Verification

Registry tests cover multi-page contribution, stable ordering, atomic rejection,
thread ownership, value snapshots and lifetime cleanup. Controller tests verify
completion, skip, read-only review, legacy compatibility and save errors.
`--verify-tutorial` and `--verify-tutorial-skip` exercise the actual QML navigation,
contents selector, completion and Settings replay in isolated app data. Module
startup tests verify that inactive modules add no tutorial pages, including the
all-disabled case with only host guidance. Normal UI verification never writes
the user's tutorial preferences.
