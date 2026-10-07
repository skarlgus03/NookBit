# Operation timing diagnostics

Preparation and confirmed movement now record elapsed times automatically. No stopwatch or extra UI button is needed. Preparation confirmation shows its elapsed seconds; movement success/failure shows the total approval-and-execution time. Waiting for the user to confirm is excluded. The UI elapsed time includes report writing; the diagnostic total excludes report writing and UI refresh.

Each request keeps timing entries in its own worker-thread memory. The filesystem adapters add scoped measurements only when that request has an active timing context. No domain interfaces, approval/rule JSON, history schema, or movement decisions changed. There are no per-buffer log writes. The latest report is written once when the request exits, and diagnostic write failures never change the operation result.

## Reports

Windows normal account location:

```text
%APPDATA%\NookBitProject\NookBit\diagnostics\last-prepare-timing.log
%APPDATA%\NookBitProject\NookBit\diagnostics\last-move-timing.log
```

The actual root follows Qt's `AppDataLocation` for the running account. Each request replaces only its own latest report, so report count does not grow with repeated runs. The files are developer diagnostics, not an audit/history store. They contain fixed stage names, timestamps, success/failure/incomplete outcome, planned file count/bytes and durations. File paths, names, content and digest values are not recorded. `planned_bytes` is an input size, not successfully copied bytes or physical disk I/O. A failed stage duration includes work performed before failure. A normal C++ exception can produce `incomplete`; forced termination/power loss cannot finish a report, so check `unix_started_ms` rather than assuming an old report belongs to the latest attempt.

The first two lines identify the format and overall request. Stage rows are tab separated. Durations use `steady_clock`, milliseconds with three decimal places and locale-independent formatting. Wall clock is only a report timestamp. Repeated stage rows correspond to separate file steps, but no filenames are retained.

## Reading the stages

| Stage | Meaning |
| --- | --- |
| `prepare_total` | Initial plan freeze/verification, including request setup and history writes |
| `request_setup` | Operation mutex acquisition and request-local storage/service setup |
| `source_support_check` | Read-only file type/stream preflight |
| `approval_check` | Displayed-plan checks, source revalidation and approval persistence |
| `execution_total` | Entire executor request; contains the following child stages |
| `executor_plan_check` | Saved approved plan and file revalidation |
| `batch_source_check` | Path safety and full source checks before any step executes |
| `file_source_check` | Final source check immediately before that file step |
| `protected_copy_open` | Protected source hash, volume checks, parents and staging creation |
| `copy_total` | Whole copy session; contains stream, flush, readback and metadata children |
| `copy_stream` | Source reading + streaming SHA-256 + staging writes; not pure disk copy throughput |
| `copy_flush` | Data flush before verification |
| `copy_readback` | Full staging reread and SHA-256 |
| `copy_source_digest` | Streaming hash finalization, or full-source hash for nonempty resume |
| `copy_metadata_and_markers` | Identity/metadata/marker checks, preservation and final metadata flush |
| `destination_commit` | Verified staging no-replace rename, flush and retained evidence checks |
| `removal_full_check` | Fresh protected source/final content and metadata checks |
| `source_remove_total` | Source removal including its nested cached recheck |
| `removal_cached_check` | Recheck while the same exclusive handles remain held |
| `source_delete_and_confirm` | Native source deletion and confirmation of source absence/final evidence |

Parent and child rows overlap: **do not add them together**. `depth` indicates nesting. Whole-request total also includes uninstrumented journaling, volume checks, service teardown and other small work. Small differences from the UI total are expected. The measurement does not promise a speed improvement; it identifies where the observed time is spent. Same-volume metadata moves do not copy/read back data, so their reports correctly omit copy stages. Interrupted recovery and explicit undo are not currently given a top-level report.

## Verification

Dedicated tests cover nested timing, explicit stop idempotence, worker-thread isolation, exception unwinding, latest-report replacement and log failure isolation. Background workflow regression checks preparation/movement elapsed text and actual transfer-stage reports, including C:/D: marked-file moves. Native movement/undo/recovery regression tests remain separate from performance measurements. No machine-dependent time limit is asserted.

The automatic suite passed 20/20. Actual C:/D: background move, last undo, history undo and explicit recovery passed with the timing-report assertions. Test-only files/reports were retained under `C:/Users/begon/AppData/Local/Temp/NookBit-bg-source-drcDoe` and `D:/NookBit/out/build/dev/tests/NookBit-bg-target-cbgMgn`; their `app-data/diagnostics` reports do not use the user's actual application DB or files. The user still tests the real 4 GB transfer.
