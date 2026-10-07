# Download marker preservation and hash performance

## Implemented scope

New application VerifiedCopy drafts run a read-only support check before full content hashing. It checks ordinary single-link fixed-local-NTFS files and enumerates streams. The default data stream plus an optional `Zone.Identifier` of at most 64 KiB is supported. Unknown named streams, oversized markers, unsupported attributes and access failures are rejected early. Existing frozen/approved contracts are never silently upgraded: a marker appearing on a Basic contract requires a new preview.

The marker is not deleted, interpreted, or unblocked on the user's original. The application selects `PreservationPolicy::LocalNtfsBasicWithZone` (value 1) for new marked copy drafts. Value 0 remains the old Basic policy and still rejects named streams. The policy number is already part of the v2 approval signature and movement JSON; schema v8 and rules JSON v3 are unchanged. Older binaries reject policy 1 rather than silently ignoring it.

## Copy and source removal

The copy session holds a read-only source-marker handle and snapshots its raw bytes when opening the protected source. It creates the marker only on its owned new staging file, flushes it, verifies the exact bytes, and holds the copied marker against writers. Source/staging markers are checked before commit and the final marker is checked after rename. No arbitrary streams are copied or silently discarded.

The separate removal session compares and protects source/final markers, and retains its initial marker expectation across removal checks. Marker mismatch or unreadability stops before deletion and preserves both files. Its own source-marker handles are released only after source deletion is marked, so they do not keep the original pending deletion. The retained final marker is checked again. The existing full-data hash, object, volume and metadata checks remain.

This policy preserves the marker observed at copy-session opening, not a marker byte digest frozen during preparation. An external marker change before opening is outside the original content-digest snapshot; a change during copying or mismatch before removal is rejected. This does not provide transactional filesystem/DB or power-loss guarantees.

Completed normal moves and explicit normal/history undo support the marker policy in both directions. Interrupted policy-1 transfers are currently explicitly blocked and preserved by startup inspection and the recovered-runner boundary; partial-copy resume/cleanup and interrupted commit/removal recovery have not been extended to this policy. Old Basic-policy recovery continues unchanged. Do not reinterpret an old failed Basic transfer as a policy-1 transfer.

## Performance changes and remaining work

Windows `QtPlanHasher::sha256File` now uses BCrypt SHA-256 instead of the slow Debug Qt implementation. The digest value and old approval hashes remain compatible. Non-Windows retains Qt hashing; small document digests are unchanged. Streaming tests compare file hashes with the independent in-memory Qt digest.

An owned cached 256 MiB fixture took 3225 ms with the earlier Debug implementation and 155 ms with the Windows implementation; raw reading in the latter run took 36 ms. This is approximately a 20x improvement for this hash-only test, not a promise about real C:→D: transfer time. The user's 4 GB file was not read in this benchmark or moved by test tools.

Fresh preparation also no longer immediately rehashes the just-frozen file set: it reads back the committed plan and compares the signature/contents. Reopening a frozen confirmation, approving and executing still revalidate. The protected-session optimization below reduces additional transfer passes; approval and execution-boundary passes remain.

## Protected-session optimization

Fresh copying computes the source SHA-256 from the bytes already read by the copy loop. It still flushes and rereads the staging file once, compares both digests to the approved content digest, and checks source identity, size, timestamps, preserved metadata and supported streams. Nonempty partial-copy resume keeps the previous full-source verification because its copy loop does not read the existing prefix.

Commit reuses verified content evidence only while the same source and staging handles remain open without write/delete sharing, with their parents protected. It still checks identities, size/time, metadata and markers before and after the no-replace handle rename. `openVerified` recovery establishes fresh source/full-prefix evidence before creating a commit-only session; a persisted digest alone is never treated as a protected-session cache.

Removal is a separate session: the copy handles have closed, so it independently hashes both reopened files once. Only after all content/identity/metadata/stream checks succeed does that session cache the verification. Repeated pre-delete verification and the post-delete retained-file check reuse this evidence while the same exclusive handles remain held. Identity, size/time, metadata, marker and path checks are not removed. Cache state is local to the session, not persisted or shared between workers. A new session always verifies contents again, including equal-size/equal-time mutations.

For one file in the fresh application VerifiedCopy path after confirmation, normal full-file read passes decrease from 16 to 9 (including the actual copy read). Source hashing during copying still performs SHA-256 work, but adds no separate read pass. This count excludes preparation and recovery, and counts logical reads, not physical disk traffic. The remaining passes include approval/executor validation, source validation at copy-session opening, staging readback and fresh removal-session checks. Further reduction across session/approval boundaries needs a separate evidence-lifetime design.

An optional adapter benchmark (`NOOKBIT_BENCHMARK_COPY=1`, `nookbit_staged_copy_tests`) uses an owned cached 256 MiB fixture on D:, not the user's 4 GB file. Before/after observed stage times were: open 154/158 ms, copy plus readback 480/451 ms, commit 462/2 ms, first removal verification 303/302 ms, repeated removal verification plus deletion 799/17 ms. The benchmark deliberately calls an additional verification to exercise reuse; it is not application end-to-end latency or real C:→D: throughput. Timing is printed, not asserted as a machine-dependent pass/fail threshold.

Optimization verification: full automatic suite 19/19 passed. Added checks compare streaming binary contents to independent in-memory Qt hashing, deny writes/deletion while cached evidence is held, preserve single-use removal, and reject source/final content changes between sessions even with restored size and modification time. Existing cancellation, partial resume, marker, metadata, no-overwrite, journaling and recovery tests remain passing. Actual C:/D: `--run` scenarios passed 6/6; `--crash-run` passed 4/4 (mid-copy, after rename, after source removal, before completion). Test-only fixtures/reports were retained at `C:/Users/begon/AppData/Local/Temp/NookBit-cross-source-clfCHf`, `D:/NookBit/out/build/dev/tests/NookBit-cross-target-Rkyyao`, `C:/Users/begon/AppData/Local/Temp/NookBit-cross-source-tTSsDw`, and `D:/NookBit/out/build/dev/tests/NookBit-cross-target-DXVOXd`. No user's file or application DB was used. The app executable was rebuilt; real 4 GB latency is still a user check.

## Verification

- Full automatic suite: 19/19; focused stream and controller tests also passed after adding mismatch and size-limit cases.
- Owned source-marker copying/commit, writer denial, exact final bytes, old Basic rejection, unknown-stream rejection and 65537-byte-marker early rejection.
- Changed source marker refuses source deletion and retains source/final files.
- Actual C:/D: marked-file application preparation, approval, move/source removal, last undo and history undo preserve the exact marker. The marker is removed only from the owned fixture before the separate legacy recovery regression.
- Final native fixtures retained at `C:/Users/begon/AppData/Local/Temp/NookBit-bg-source-lOwsFl` and `D:/NookBit/out/build/dev/tests/NookBit-bg-target-VwjRwM`.

User visual testing and real 4 GB end-to-end latency are still separate checks.
