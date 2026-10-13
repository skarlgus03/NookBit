# Operation progress

The background operation panel shows the current processing phase, current filename, file position/count, durably completed file count, and current-file copy bytes/percentage. Navigation and conflicting operations remain input-disabled while work is active, but the window title area stays movable. No cancel button or estimated time remaining was added.

Preparation and undo use a non-modal panel centered over the extension window's
content area, with a translucent white veil confined to that content area.
The main window and desktop are not dimmed or blocked by this panel. Both the
white color and veil opacity use shared theme/metrics tokens. Manual sorting's
execution phase retains its inline progress page instead of showing two panels.

Read-only recovery-list inspection is an exception: open the recovery page first,
then refresh its list and show a small inline spinner/message. Neither the shared
panel nor white veil is shown for that inspection. Explicit resume/rollback still
uses the shared file-work progress display; approval and inspection are unchanged.

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

The small panel uses explicit light/dark text colors and a native progress bar. This is a functional status display, not the final application/character design. Visual testing is left to the user; automated tests do not control the desktop.

## Verification

Core tests cover throttling without sleeps, final-byte delivery, large counters, bounded values, empty-file handling, no false completion at copy 100%, thread/nested-scope isolation, observer exceptions and unregistration. Background runner tests hold the worker until the GUI actually receives an intermediate 45% update, verify UI-thread property updates, and ensure failure starts with fresh counts and no stale copy bar. Native background workflows assert recorded completed counts, restored counts, and required copy/verification/commit/removal phase events; same-volume workflows assert the absence of copy percentages.

The full automatic suite passed 22/22. Actual C:/D: mixed and copy-only background move, last undo, history undo and explicit recovery passed with progress assertions. Owned fixtures were retained at `C:/Users/begon/AppData/Local/Temp/NookBit-bg-source-UnhXec` and `D:/NookBit/out/build/dev/tests/NookBit-bg-target-BsZDXL` (copy-only cases under `copy-only`). No user original or application DB was used. The application executable was rebuilt; its visual appearance and real 4 GB transfer display are user checks.
