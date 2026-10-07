# Download marker preservation and hash performance

## Implemented scope

New application VerifiedCopy drafts run a read-only support check before full content hashing. It checks ordinary single-link fixed-local-NTFS files and enumerates streams. The default data stream plus an optional `Zone.Identifier` of at most 64 KiB is supported. Unknown named streams, oversized markers, unsupported attributes and access failures are rejected early. Existing frozen/approved contracts are never silently upgraded: a marker appearing on a Basic contract requires a new preview.

The marker is not deleted, interpreted, or unblocked on the user's original. The application selects `PreservationPolicy::LocalNtfsBasicWithZone` (value 1) for new marked copy drafts. Value 0 remains the old Basic policy and still rejects named streams. The policy number is already part of the v2 approval signature and movement JSON. Database schema v9 adds a pre-removal receipt; rules JSON v3 and approval plan v2 are unchanged. Older binaries reject schema v9 before database writes.

## Copy and source removal

The copy session holds a read-only source-marker handle and snapshots its raw bytes when opening the protected source. It creates the marker only on its owned new staging file, flushes it, verifies the exact bytes, and holds the copied marker against writers. Source/staging markers are checked before commit and the final marker is checked after rename. No arbitrary streams are copied or silently discarded.

The separate removal session compares and protects source/final markers, and retains its initial marker expectation across removal checks. Marker mismatch or unreadability stops before deletion and preserves both files. Its own source-marker handles are released only after source deletion is marked, so they do not keep the original pending deletion. The retained final marker is checked again. The existing full-data hash, object, volume and metadata checks remain.

This policy preserves the marker observed at copy-session opening, not a marker byte digest frozen during preparation. An external marker change before opening is outside the original content-digest snapshot; a change during copying or mismatch before removal is rejected. This does not provide transactional filesystem/DB or power-loss guarantees.

Completed normal moves and explicit normal/history undo support the marker policy in both directions. Policy-1 forward recovery now supports source-retained boundaries described below. Old Basic-policy recovery continues unchanged. Do not reinterpret an old failed Basic transfer as a policy-1 transfer.

## Source-retained marker recovery

Read-only recovery inspection uses the signed preservation policy, reads supported markers, and compares source/copy raw bytes. A partial owned staging file may lack its marker because normal copying writes it after data verification. An existing marker must match; it is never overwritten to repair a mismatch. Verified staging and committed final copies require exact marker equality, including presence/absence.

Protected native sessions independently repeat these checks before any recovery write. Forward recovery supports partial-prefix continuation, verified staging commit, reconciliation of an unrecorded final rename, and source removal from a recorded committed copy. Marker handles and source/copy parent/base-file handles remain held during the relevant operations. Recovered source removal records completion while the same removal session still holds its independently verified final-content and marker evidence; it does not reopen a source-absent session and infer a historical marker.

Schema v9 now stores a pre-removal proof for new forward policy-1 removals. A source-absent interrupted transfer with a valid proof and a replayed normal/recovery removal intent can explicitly recover completion as described below. Old records lacking proof remain blocked with `recovery.zone_evidence_required`; migration never fabricates historical receipts. Already Completed attempts can be recognized when inspecting a larger interrupted batch, and their normal explicit undo remains supported.

Interrupted reverse policy-1 attempts and policy-1 staging cleanup remain unsupported. Metadata-only rename recovery is unchanged. Before the first receipt, the current source marker is authoritative: identical external changes to both markers before opening cannot be detected against a preparation-time marker, because no marker digest is frozen in approval. Once a receipt exists, a retry cannot replace it with different marker/metadata evidence. Full-content/object/volume/metadata checks remain independently required.

Application whole-operation inspection now runs on the shared background runner using thread-local database connections. It is read-only and never automatically resumes. Busy inspection rejects duplicate requests and stale execution choices; exceptions and failed inspections clear actionable cards and retain warnings. Only v1 compatibility records use the old synchronous legacy record-resolution inspection.

Verification uses `nookbit_cross_volume_tests.exe --zone-recovery-run --source-parent <existing C: directory> --destination-parent <existing D: directory>`. Seventeen owned scenarios cover source-retained recovery, source-removed/completion-record-failure recovery on reopened repositories, missing old proof, changed/missing markers, corrupt audits, both halves of transaction failure, equal-size/equal-time content changes, and immutable/idempotent receipt retries. Successful recovery is checked through durable completion and exact-marker normal undo.

## Durable proof after source removal

Only the independently verified native removal session can provide `TransferRemovalProof`: the selected approval hash, committed final object/content fingerprint, marker presence (including absence versus empty), bounded byte length and SHA-256, plus exact NTFS creation/write times and supported attributes. Marker bytes and embedded URLs are not stored in the receipt or shown in logs/UI.

Normal forward finalization and source-retained forward recovery save this receipt before removal intent/deletion while retaining protected source/final/marker handles. Unsupported storage or any transaction failure stops before deletion. SQLite binds the receipt to the committed transfer event and a separate audit event, saving both atomically under FULL synchronous WAL. A repeated identical receipt is accepted without adding another receipt; different evidence, stale attempt/approval, wrong policy or corrupt audit is rejected.

Read-only inspection replays the attempt and actual removal-intent history, validates receipt/audit/committed-event links and the selected plan hash, then checks the final marker and exact attributes. The mutating completion use case independently opens a read-only protected session and verifies approved volumes, final object, the entire data SHA-256, receipt metadata/marker, and proven source/staging absence. Destination/parent/marker handles stay held through atomic completion recording. This path only repairs transfer/step/plan records; it does not recreate or delete files. Startup never auto-resumes it.

Schema 9 migration is additive and transactional; old plan/approval hashes and transfer events are preserved without manufactured receipts or deletion intents. Old binaries refuse the newer database, so do not downgrade the executable after migration. Database durability still depends on SQLite, the OS and storage honoring flushes; this is not an atomic filesystem/database transaction or a guarantee against every power-loss/storage failure.

## Performance changes and remaining work

Windows `QtPlanHasher::sha256File` now uses BCrypt SHA-256 instead of the slow Debug Qt implementation. The digest value and old approval hashes remain compatible. Non-Windows retains Qt hashing; small document digests are unchanged. Streaming tests compare file hashes with the independent in-memory Qt digest.

An owned cached 256 MiB fixture took 3225 ms with the earlier Debug implementation and 155 ms with the Windows implementation; raw reading in the latter run took 36 ms. This is approximately a 20x improvement for this hash-only test, not a promise about real C:→D: transfer time. The user's 4 GB file was not read in this benchmark or moved by test tools.

Fresh preparation also no longer immediately rehashes the just-frozen file set: it reads back the committed plan and compares the signature/contents. Reopening a frozen confirmation, approving and executing still revalidate. The protected-session optimization below reduces additional transfer passes; approval and execution-boundary passes remain.

## Protected-session optimization

Fresh copying computes the source SHA-256 from the bytes already read by the copy loop. It still flushes and rereads the staging file once, compares both digests to the approved content digest, and checks source identity, size, timestamps, preserved metadata and supported streams. Nonempty partial-copy resume keeps the previous full-source verification because its copy loop does not read the existing prefix.

Commit reuses verified content evidence only while the same source and staging handles remain open without write/delete sharing, with their parents protected. It still checks identities, size/time, metadata and markers before and after the no-replace handle rename. `openVerified` recovery establishes fresh source/full-prefix evidence before creating a commit-only session; a persisted digest alone is never treated as a protected-session cache.

Removal is a separate session: the copy handles have closed, so it independently hashes both reopened files once. Only after all content/identity/metadata/stream checks succeed does that session cache the verification. Repeated pre-delete verification and the post-delete retained-file check reuse this evidence while the same exclusive handles remain held. Identity, size/time, metadata, marker and path checks are not removed. Cache state is local to the session, not persisted or shared between workers. A new session always verifies contents again, including equal-size/equal-time mutations.

The first protected-session optimization reduced one fresh application copy file's normal full-file read passes from 16 to 9 (including the actual copy read). The execution-boundary optimization below further reduces eligible copy-only plans to 6. Source hashing during copying still performs SHA-256 work, but adds no separate read pass. These counts exclude preparation and recovery, and count logical reads, not physical disk traffic.

An optional adapter benchmark (`NOOKBIT_BENCHMARK_COPY=1`, `nookbit_staged_copy_tests`) uses an owned cached 256 MiB fixture on D:, not the user's 4 GB file. Before/after observed stage times were: open 154/158 ms, copy plus readback 480/451 ms, commit 462/2 ms, first removal verification 303/302 ms, repeated removal verification plus deletion 799/17 ms. The benchmark deliberately calls an additional verification to exercise reuse; it is not application end-to-end latency or real C:→D: throughput. Timing is printed, not asserted as a machine-dependent pass/fail threshold.

Optimization verification: full automatic suite 19/19 passed. Added checks compare streaming binary contents to independent in-memory Qt hashing, deny writes/deletion while cached evidence is held, preserve single-use removal, and reject source/final content changes between sessions even with restored size and modification time. Existing cancellation, partial resume, marker, metadata, no-overwrite, journaling and recovery tests remain passing. Actual C:/D: `--run` scenarios passed 6/6; `--crash-run` passed 4/4 (mid-copy, after rename, after source removal, before completion). Test-only fixtures/reports were retained at `C:/Users/begon/AppData/Local/Temp/NookBit-cross-source-clfCHf`, `D:/NookBit/out/build/dev/tests/NookBit-cross-target-Rkyyao`, `C:/Users/begon/AppData/Local/Temp/NookBit-cross-source-tTSsDw`, and `D:/NookBit/out/build/dev/tests/NookBit-cross-target-DXVOXd`. No user's file or application DB was used. The app executable was rebuilt; real 4 GB latency is still a user check.

## Execution-boundary duplicate checks

User timing for a 4,182,218,328-byte copy showed 22.223 seconds total: approval, executor plan verification, batch source checking, per-file source checking and protected copy opening each spent roughly 2.35 seconds reading/hashing the same source. Copy/stream hashing took 3.245 seconds, staging readback 2.100 seconds and fresh removal checks 4.454 seconds. This is measured request data, not a general throughput promise.

Only an intact v2 approved plan whose steps are all still Planned and VerifiedCopy can defer the three executor-side full-content reads. The transfer port must explicitly report `verifiesSourceBeforeStaging()`: its copy adapter must compare the complete source digest against the approved digest before creating staging and retain write/delete-denying source and parent handles through copying/commit. Defaults on both interfaces are false. WindowsStagedCopy provides that guarantee; the application use case forwards it without knowing the concrete adapter.

Approval still calls the ordinary verifier and hashes the actual source. Protected execution uses a separate verifier entry point: stored plan signature, settings/rules revisions, volumes, selection, regenerated destinations, metadata/identity and complete signed contract still match. It carries the saved digest only as an **expected value**, not fresh content evidence. Batch and final pre-step checks inspect paths/identity/size/time without hashing; the protected copy opening then independently hashes the source and rejects a content mismatch before creating staging or a destination parent. A mutation after approval, even with equal size/restored modification time, is therefore rejected by the protected opening, never copied/deleted as approved contents. Such late rejection records NeedsRecovery for explicit review, rather than pretending approval had never been given.

Mixed/rename/skipped/applied plans and transfer adapters without the explicit guarantee retain the original full checks. Recovery calls keep the default strict execution path; the deferral flag is a call argument, not cached on an executor or persisted. There is no unsafe reuse of an earlier hash across an unlocked interval, no new signed flags, and no rule/plan/history schema migration.

Eligible fresh copy reads after confirmation are now: approval source (1), protected-opening source (1), actual source copy (1), staging readback (1), and new removal-session source/final verification (2): **6 logical full reads**. Further reduction of approval/opening or copy/removal checks would require extending protected-handle lifetimes across those boundaries; they are deliberately retained in this step. The earlier timing implies roughly 7.05 seconds of duplicate reads targeted, but real post-change latency must be measured again.

Added regression scenarios forbid/count the executor's content-hasher calls, show zero such calls with guaranteed protected copy versus three with ordinary strict copy, reject same-size/same-time content mutation both after approval and during intent recording before any staging creation, preserve approval's full-content rejection, and reject changed settings/volumes. Mixed batch tests now also advertise the source-validation guarantee and still retain their original behavior. The native background regression runs both mixed C:/D: and copy-only C:→D: workflows with move, last undo, history undo, explicit recovery and marker preservation.

Verification for this execution-boundary change: full automatic suite 21/21 passed. The rebuilt application main object's header dependencies are checked by the regression suite. Actual mixed and copy-only native workflows both passed at `C:/Users/begon/AppData/Local/Temp/NookBit-bg-source-LyBxlQ` and `D:/NookBit/out/build/dev/tests/NookBit-bg-target-ktwEKm`, with the copy-only workflow/reports under their `copy-only` subdirectories. These retained fixtures are newly created test files/DBs, not user application data or the user's 4 GB source.

## Verification

- Full automatic suite: 19/19; focused stream and controller tests also passed after adding mismatch and size-limit cases.
- Owned source-marker copying/commit, writer denial, exact final bytes, old Basic rejection, unknown-stream rejection and 65537-byte-marker early rejection.
- Changed source marker refuses source deletion and retains source/final files.
- Actual C:/D: marked-file application preparation, approval, move/source removal, last undo and history undo preserve the exact marker. The marker is removed only from the owned fixture before the separate legacy recovery regression.
- Final native fixtures retained at `C:/Users/begon/AppData/Local/Temp/NookBit-bg-source-lOwsFl` and `D:/NookBit/out/build/dev/tests/NookBit-bg-target-VwjRwM`.

User visual testing and real 4 GB end-to-end latency are still separate checks.
