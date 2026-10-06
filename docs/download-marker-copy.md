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

Fresh preparation also no longer immediately rehashes the just-frozen file set: it reads back the committed plan and compares the signature/contents. Reopening a frozen confirmation, approving and executing still revalidate. Multiple full-file passes remain in the approval/copy/removal pipeline. Computing the source digest while copying and reducing more repeated passes requires a separate explicit approval/transfer-evidence design; it is not complete in this change.

## Verification

- Full automatic suite: 19/19; focused stream and controller tests also passed after adding mismatch and size-limit cases.
- Owned source-marker copying/commit, writer denial, exact final bytes, old Basic rejection, unknown-stream rejection and 65537-byte-marker early rejection.
- Changed source marker refuses source deletion and retains source/final files.
- Actual C:/D: marked-file application preparation, approval, move/source removal, last undo and history undo preserve the exact marker. The marker is removed only from the owned fixture before the separate legacy recovery regression.
- Final native fixtures retained at `C:/Users/begon/AppData/Local/Temp/NookBit-bg-source-lOwsFl` and `D:/NookBit/out/build/dev/tests/NookBit-bg-target-VwjRwM`.

User visual testing and real 4 GB end-to-end latency are still separate checks.
