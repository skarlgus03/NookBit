# Named folder input flows

Folders default to one indivisible bundle. The optional v3 `folderFlows` member
declares named, user-selectable input workflows; it never changes that default.
Create one with the rule canvas context menu → folder processing flow. The editor
creates a named region with three steps: folder input, recursive regular-file
traversal, and a call to the existing default classification tree. Select its
input node to rename it; Delete on any step removes the complete flow. Save All
publishes it to the queue's folder-only selector. The first implementation
supports this complete pipeline only, rather than arbitrary executable graphs.

## Ownership and data

- `RuleSet::folderFlows` owns stable IDs and display names, not canvas coordinates.
- JSON import/export validates the declaration. Layout remains in the separate
  rule-layout document, keyed by flow ID and step type.
- `PendingFilesModel` owns each queued path's flow selection and rule-set ID.
  Only folders offer the selector, and only when the active rules provide flows.
- `JsonPendingFilesRepository` preserves selections in optional `folderSelections`
  alongside its existing v1 `paths`. Existing path-only documents restore as
  whole-folder processing. Invalid selection documents are not overwritten.
- `CreatePreviewUseCase` validates the selected flow against one loaded rule set
  and requests explicit expansion through `IScanner`. RuleEngine stays pure:
  it receives metadata and never enumerates directories or performs file I/O.
- `FilesystemScanner` expands only explicitly selected, link-free folders.
  Traversal failures, unsupported entries and reparse points fail the preview;
  parent/child overlapping move sources are rejected. No source is modified.
- Freeze, approval and execution receive the preview's actual individual source
  paths. No directory is silently expanded again during execution. Files added
  after preview remain untouched; changed captured files use existing stale-plan
  checks. Empty source directories remain, so rollback restores original paths.

Flow identity is scoped by rule-set ID. Rename keeps selection; removal or a
different rule-set ID resets affected entries to the bundle default and announces
the reset. Changing selection invalidates existing preview/approval. An advanced
selection has an explicit queue label, and the preview describes recursive
individual processing. Inspection runs on the existing background task runner.

## Extension boundary

`recursiveFilesToDefault` is the only supported kind. Future predicates,
AND/OR/NOT expressions, content analyzers and additional processing kinds should
have separate versioned contracts. This first step does not implement those
features or misrepresent a comment rectangle as a general-purpose interpreter.

Verification includes JSON and queue round trips, graph boundary/cycle checks,
QML name editing/group deletion, default bundle preservation, nested file
enumeration, and a real temporary-file preview → freeze → approve → move → undo
test. No personal download files are used in automated tests.
