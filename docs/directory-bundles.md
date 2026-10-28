# Folder bundles

## User contract

A directly dropped folder is one classification/move target. Its name, files,
subfolders and empty directories travel together; children are never individually
classified. Watching a folder retains the previous file-scanning behavior.

Use an `itemType` condition (`kind: folder` or `kind: file`) to select a target kind,
optionally followed by name or modified-age children. Extension
and file-size predicates only match files. Folder sizes are not recursively summed
during scanning. The additive `itemType` condition uses rules JSON v3; older apps
without this condition reject it during import. Existing condition indices and
file-only approval signatures are preserved.

Folder collisions always use Skip, regardless of the file collision setting or
per-rule policy. A file or directory occupying the destination prevents that folder
from moving; other targets continue. Existing contents are never merged or replaced.
An overlapping selection (folder plus a descendant) is rejected. Moving a root,
moving into itself/a descendant, or moving app data/an ancestor of app data is rejected.

## Implementation boundary

`FileFingerprint.directory` defaults to false for old plans. It is persisted in
fingerprint JSON and included in the approval signature only for directory steps;
existing file-only plan signatures remain unchanged. Changing an object's type
invalidates approval. Folder-specific behavior remains in the filesystem layer;
controllers and rules never move files directly.

Same-volume metadata plans rename the actual directory object without enumerating
its contents. Mixed plans retain the existing content checks. Cross-volume plans
use a dedicated `WindowsDirectoryMover`, not the regular-file staged-copy adapter.
The same worker supports undo. Actual volume contracts still require fixed local
NTFS; a drive letter alone is not treated as volume identity.

Cross-volume folders are copied to a uniquely created `.nookbit-folder-*.tmp`
sibling. `CopyFileExW` preserves file data, alternate streams and file attributes.
Directory creation/modification times and empty directories are preserved; identical
ACLs and directory attributes are not promised. Content signatures include relative
names, kinds, file contents and named file streams, including Zone.Identifier.
Links/junctions and named directory streams are refused for cross-volume moves.

Source entries and streams are protected before final verification. After staging
verification, only copy-side locks are released for a no-replace directory rename.
The final destination is protected and verified again before deleting any source.
Deletion addresses the inventoried objects by their protected handles, in reverse
order. Newly added source items are not deleted: Windows refuses removal of a
nonempty directory, leaving the complete destination and remaining source for review.
No recursive delete is used on user data.

## Failure and recovery

Before source removal, failures preserve the source. A staging copy may also remain
in the destination parent; it is not automatically removed. Failures during source
cleanup preserve the completed destination and remaining source and mark the plan
for manual review. Interrupted folder plans never enter regular-file automatic
resume/cleanup paths. Review source, destination and the temporary sibling before
manually removing redundant copies. Resumable folder copies, merging and overwrite
are outside this implementation.

`nookbit_directory_bundle_tests` exercises real temporary folders, cross-volume
copy/undo, Unicode paths, empty directories, a download-marker stream, collision
policy, post-approval collisions and approval/SQLite/execution integration. Existing
file executor, planner, signature and pending-list tests cover compatibility.
