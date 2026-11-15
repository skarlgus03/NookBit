# Operation progress

The background operation panel shows the current phase, filename, file position
and count, durably completed file count, and copy progress for the current file.
Navigation and conflicting actions are disabled during work. The window title
area stays movable.

Preparation and undo use a non-modal panel centered over the extension window's
content area, with a translucent white veil confined to that content area.
The main window and desktop are not dimmed or blocked by this panel. Both the
white color and veil opacity use shared theme/metrics tokens. Manual sorting's
execution phase retains its inline progress page instead of showing two panels.

Read-only recovery-list inspection is an exception: open the recovery page first,
then refresh its list and show a small inline spinner/message. Neither the shared
panel nor white veil is shown for that inspection. Explicit resume/rollback still
uses the shared file-work progress display; approval and inspection are unchanged.

## Copy cancellation

During sorting, **복사 취소** appears while a cross-volume staged copy accepts
cancellation. The button disables after a request. The worker checks the request
during copying and again before it records commit intent. The current source
stays in place. The worker records the cancelled attempt and removes the
uncommitted staging file through its existing protected handle. Previously
completed moves stay in work history, and the batch stops before starting the
next file. The result page offers to undo the files already moved or leave them
where they are. Remaining items stay in the pending queue.

Same-volume moves, undo and recovery do not offer this cancellation action.
Uncertain outcomes or failures to record an outcome still follow the recovery
path.

## Backend contract and lifetime

`include/nookbit/application/operation_progress.hpp` contains a Qt-independent phase enum and value snapshot. `OperationProgressScope` connects a sink for exactly one worker request, restoring any outer scope on destruction. Its active pointer is thread-local request context, not a shared file/DB manager or a global operation result. Without an active observer, backend reports are no-ops. Other threads cannot publish into that request. Future consumers such as a character renderer can receive the same snapshots without being referenced by the file/approval engine.

The background runner owns the scope and queues snapshot copies to its own QObject. Only the UI thread updates the QML properties. Completion and progress are queued from the same worker in order. Each new request resets progress; success/failure also clears the copy percentage. Deleted completion receivers remain protected by the existing QPointer and worker destruction still waits rather than terminating file work.

Progress reports are observational: they are not authorization, validation evidence, cancellation commands, persisted history, or changes to any movement/rule/JSON contract. Source and destination protection, hashes, journaling and recovery choices are unchanged. Sink exceptions do not propagate back into file operations. Sinks should return promptly; the application sink only posts a queued update.

## Meaning of the values

- `fileIndex` and `fileCount` identify the actual execution sequence. Excluded/non-started historical files are not invented as completed. A resumed operation can show only its remaining work, and unknown counts are omitted rather than guessed.
- `completedFiles` increments only after the executor has checked the recorded Applied/RolledBack result. A copied buffer or renamed file alone does not count as a successful file.
- `fileName` is the UTF-8 basename, not the full source path. No progress is written to diagnostic/history files.
- `copiedBytes` counts writes accepted for the current staging file, plus an already-validated resume prefix. It is not a durable-write guarantee. `totalBytes` uses 64-bit-capable values, including files larger than 4 GiB.
- Copy byte notifications are limited to about one per 100 ms; the first and final values and phase/file changes are delivered immediately. There are no per-buffer disk logs or synchronous GUI calls.
- The percentage represents **current-file copying only**, not total job progress. At 100%, flush, content verification, metadata preservation, commit, source-removal checks and history completion may remain.
- Preparation/approval/plan checks, flush, verification, commit, deletion and history writes use an indeterminate indicator. Empty files do not divide by zero or receive invented percentages. Same-volume rename has no copy-byte percentage.
- Detailed phase/byte events are available for the normal staged-copy path and underlying undo/recovery copies. Some recovery inspection or other background requests only supply a broad phase and omit a file count. The panel does not claim a complete recovery-wide denominator.

## Verification

Core tests cover throttling without sleeps, final-byte delivery, large counters, bounded values, empty-file handling, no false completion at copy 100%, thread/nested-scope isolation, observer exceptions and unregistration. Background runner tests hold the worker until the GUI actually receives an intermediate 45% update, verify UI-thread property updates, and ensure failure starts with fresh counts and no stale copy bar. Native background workflows assert recorded completed counts, restored counts, and required copy/verification/commit/removal phase events; same-volume workflows assert the absence of copy percentages.

The commit-transfer and preview-controller tests cover cancellation before
commit and the background runner's cancellation boundary. For a native
two-volume run, use the [cross-volume test procedure](cross-volume-test-run.md).
