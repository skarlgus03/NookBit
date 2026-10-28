# Feature modules: boundaries and communication

Status: incremental refactor, 2026-10-28. The pending-queue, classification-query
and integration-adapter seams (steps 1 through 3) are implemented. Module toggles,
a module registry and plugin loading remain planned.
Step 4 now has scoped runtimes for Notifications, Folder Watch, File Organizer
and Companion. UI-contribution registration and enablement remain pending.
The current adapter reuses the existing queue model; ownership of queue state has
not yet moved out of the presentation layer.

## Module units

A feature module owns a coherent capability, its use cases, state and UI entries.
It is larger than a worker such as Scanner or RuleEngine. The existing
`src/modules/` directories are implementation groupings, not switchable features.
Modules can use separate instances of the same stateless worker implementation.

| Proposed module ID | Owns | Public capabilities | Required feature dependency |
| --- | --- | --- | --- |
| `file-organizer` | Pending queue, active/draft rules, rule editor, previews, approval, movement/undo, history and recovery | Pending-path query, enqueue command, read-only classification query, existing sort workflow | None |
| `folder-watch` | Watched paths, timers, file readiness, thresholds, proposals and snooze state | Watch configuration, current proposal, accept/dismiss/snooze actions | `file-organizer` |
| `notifications` | Type registration, active notification facts, per-type delivery preferences, notification settings | Existing `INotificationChannel`, notification delivery updates | None |
| `companion` | Pack library, desktop companion, preview/editor, visual behavior, interactions and personal overrides | Existing `ICharacterChannel`, interaction/behavior capabilities | None |

The host owns startup/shutdown, module enablement preferences, shared UI theme and
metrics, navigation registration, tray/window lifecycle and integration adapters.
It does not absorb each feature's business rules or repositories. File operations
retain their existing serialized execution and operation lock. Companion animation
is not file work, and module boundaries do not introduce new worker threads.

Rule matching/loading must work without opening the rule editor. For the first
split the editor belongs to File Organizer; it need not become a separate module.
One module may register several pages: organizing, rules and history can share one
owner without becoming one menu button.

## Evidence in the current code

| Current seam | What exists | Remaining coupling |
| --- | --- | --- |
| [WatchController](../src/presentation/watch_controller.hpp) | Uses `IWatchProposalUseCase`, `IPendingPathsQuery` and `IEnqueuePendingPaths`; no queue-model dependency | Host still assembles it unconditionally |
| [Pending queue contracts](../include/nookbit/application/pending_queue.hpp) and [adapter](../src/app/pending_queue_adapter.hpp) | Standard C++ path snapshots and per-path admission receipts; host forwards model changes | Adapter deliberately retains the existing model and its admission policies |
| [Watch evaluation](../src/application/use_cases/evaluate_watch_folder_use_case.cpp) | Owns scanning, pending-path exclusion, readiness and thresholds; uses `IClassificationQuery` | Feature lifecycle is still assembled by the host |
| [Classification contract](../include/nookbit/application/classification_query.hpp) and [use case](../src/application/use_cases/classify_files_use_case.cpp) | Organizer-side active-rule loading and matching; ordered per-file decisions and content-based rule revision | Still part of the shared app, not a separately enabled module |
| [Notification channel](../include/nookbit/application/notification_channel.hpp) | Typed registration/update data; no knowledge of a renderer; source/delivery wiring is in FeatureConnections | NotificationRuntime owns controller creation and enabled-preference callbacks; type registration is still selected by the host |
| [Companion channel](../include/nookbit/application/character_channel.hpp) | Typed activity/attention input; separate interaction and behavior managers | CompanionRuntime owns the desktop objects; host still constructs it unconditionally |
| [Sort workflow](../include/nookbit/application/sort_workflow.hpp) | Preparation is separate from explicit approval/execution | No feature-level startup/disable boundary yet |
| [Integration adapters](../src/app/feature_connections.cpp) | Watch-to-notification, notification-to-companion and file-activity-to-companion bindings have a scoped owner | No feature lifecycle/enablement registry yet |
| [Folder Watch runtime](../src/app/folder_watch_runtime.hpp) | Owns watch settings repository, scanner, evaluation, controller/timers and snooze persistence | Injected Organizer capabilities and application preferences remain host-owned |
| [Notification runtime](../src/app/notification_runtime.hpp) | Owns notification controller/center and persistent or transient enabled preferences | Module enablement and registration ordering remain host responsibilities |
| [Organizer runtime](../src/app/file_organizer_runtime.hpp) | Owns queue persistence/availability, rules/editor, classification, previews, approval, file-operation assembly, history/recovery and a shared task runner | Queue state still resides in the existing model; module UI registration remains host-owned |
| [Companion runtime](../src/app/companion_runtime.hpp) | Owns presentation/pack loading, preferences/edits, import runner, desktop placement, interactions, motion and internal connections | Actual QML windows, tray integration and feature-to-feature routes remain host-owned |
| [Application wiring](../src/presentation/main.cpp) | Explicitly chooses and binds the existing routes | Startup, QML context objects and navigation are not yet driven by enabled modules |

`CompositionRoot` currently validates a service bundle; it is not the complete
desktop module host. Do not describe its presence as completed module loading.

## Requests and updates

Use a request when a caller needs a result. Use an update/event when it reports
state to interested observers. Contracts contain values and errors, not QML items,
concrete controllers, database connections or another module's private workers.

| Boundary | Request/update | Important result or constraint |
| --- | --- | --- |
| Watch -> Organizer | Query current pending paths | Snapshot; Watch cannot mutate the queue through it |
| Watch -> Organizer | Enqueue accepted proposal paths | Accepted/rejected paths and reasons; registration does not move anything |
| Watch -> Organizer | Query classification of scanned candidates | Matched/excluded/unmatched results and rule revision; not an approved move plan |
| Organizer -> interested observers | Pending queue/rules changed | Revision/count sufficient to trigger a fresh read; no direct controller calls |
| Watch -> Notifications | Existing `NotificationUpdate` for `file.offer` | Same type/instance updates or withdraws an existing fact |
| Organizer -> host adapter -> Companion | Work activity/progress/completion | Presentation only; can reuse existing progress values and `CharacterUpdate` |
| Notifications -> host adapter -> Companion | Deliver/withdraw allowed attention | Does not execute a notification's source operation |

Conceptual flow:

```text
Watch scans -> Organizer classification query -> Watch decides whether to offer
Watch offer -> Notification channel -> enabled presentation adapter
User accepts -> Organizer enqueue command -> pending queue
User confirms a fresh plan -> existing approved file-operation workflow
File-operation activity -> host adapter -> Companion visual state
```

`ClassificationSnapshot` identifies one loaded rules copy and returns one result
per input in the same order, echoing each source path. No decision means unmatched;
`decision.excluded` means explicit exclusion; other decisions are matched results.
The query reads active rules once per batch, uses FirstMatch (the previous watch
policy), and returns a SHA-256 of their canonical representation. It uses supplied
metadata and time; it never scans, hashes file contents, creates plans, writes
history or executes operations. Empty candidate batches still identify active rules.
Matched destinations are classification decisions, not proof that a path is allowed
or a file can currently move; the ordinary preview/approval checks remain required.

Watch rejects missing revisions and mismatched result counts/paths. Read or query
failure is an error, not an empty successful classification, and resets readiness
evidence. Physical file readiness stays with Watch. Rules revisions participate in
offer identity; a dismissed offer can reappear when rules change, even if file
metadata and the rules' human-readable version text are unchanged. Acceptance
rechecks that revision and asks for a fresh confirmation if it changed. These are
in-memory proposal values; rule JSON, watch settings and approval formats do not change.

The publisher knows a contract, not a specific consumer. The host supplies
implementations and translates between contracts where needed. Calling a narrow
interface directly is valid; not every call needs an event. Existing Qt signals
with explicit receiver contexts can remain in host-side adapters. Start with
small typed contracts; do not introduce a universal string/JSON event bus.

Observers are optional for progress and presentation. Failure to display a
reaction must not invalidate a successful file operation. Command validation,
approval and history writes still return actionable failures to their caller.
Existing `DomainEvent`/history records are not replaced with transient UI updates.
Commands must retain the current approval requirements; accepting an offer or
receiving an event never authorizes movement.

Contracts do not automatically switch threads. Host adapters dispatch updates to
the receiver's owning thread, pass value snapshots across threads and disconnect
before destroying receivers. File workers never call QML objects. Repeated
activity/progress replaces current state rather than creating an unbounded queue.

### Current integration adapter

`FeatureConnections` is a host-side QObject, not a core event bus or feature owner.
Its header depends on the input contracts and forward declarations; concrete
controller dependencies stay in its implementation. Each binding accepts a target
QObject lifetime context plus a channel interface. The channel must remain valid
for that context's lifetime. Current bindings require the bridge, source and target
to share an owning thread; unsupported cross-thread binding returns an error.
No new runtime thread, timer or event queue is introduced by this adapter.

Bind notification consumers before connecting Watch: the notification subscription
does not replay prior deliveries. Watch and work bindings immediately synchronize
their current state, then forward changes. Existing registration/persistence IDs
remain `file.offer` / `folder-watch` / `organization-offer`; work still maps
`file-operations` / `current-operation` to `file.work`. The
`folderWatchOfferType()` factory centralizes the existing type metadata.

The bridge QObject owns signal subscriptions. Destroying it disconnects routes and
removes its queued deliveries; guarded source/target pointers prevent queued events
from dereferencing deleted endpoints. A missing optional companion route does not
block Watch or notification state. Delivery errors are logged separately from the
source operation's result. Muting attention never mutes file-work activity.

Disconnecting routes does not resolve source facts or unregister notification
types. A future lifecycle owner must stop producers and withdraw active state
while channels are still alive before unloading a feature. Hot unloading and
module enablement are not implemented by this extraction.

### Current runtime ownership

The ownership groups use ordinary C++ composition, not a plugin framework:

```text
Host preferences, paths and application lifetime
  -> FileOrganizerRuntime
       queue / rules / classification / workflow / history / recovery
       shared file-operation runner
  -> FolderWatchRuntime (borrows Organizer classification and queue contracts)
       settings repository -> scanner -> evaluation -> controller and timers
  -> CompanionRuntime
       presentation / pack library / import runner / personal edits
       desktop placement / interaction manager / motion and timers
  -> NotificationRuntime
       enabled-preference callbacks -> controller and notification center
  -> FeatureConnections (subscribes to the exposed controller/channel endpoints)
```

Members are declared in dependency order and destroyed in reverse: a controller
dies before its evaluator, scanner, settings connection or callback state. The
runtimes do not take ownership of injected capabilities, QSettings or the application;
the host must keep these alive longer. Owners are noncopyable and nonmovable;
Organizer and Companion keep their larger concrete state behind a private,
heap-stable `State` so host headers need not expose all internal repositories and
workers. Controller accessors return borrowed references valid only for that
runtime's lifetime. This adds no generic container, plugin loader or worker thread.

Folder Watch keeps existing `watch.json` behavior and the existing
`watchSnoozeFolder` / `watchSnoozeUntilUtcMs` preference keys. Snooze is restored
only for the same watched folder; its persistence connection uses the owned
controller's QObject lifetime. Destruction removes watchers, timers and pending
startup checks through controller destruction; it does not erase settings or
enqueue/move files. Classification and pending-queue dependencies remain interfaces.

NotificationRuntime retains `notifications/types/<id>/enabled` and the existing
rollback-on-save-failure behavior. Its transient mode replaces only notification
preferences with an in-memory map. FolderWatchRuntime's `persistSnooze: false`
only disables snooze preference reads/writes; its explicitly supplied watch JSON
repository remains separate. These are existing verification-mode behaviors, not
new module-enable switches. Construction still honors the saved watch-monitoring
setting; the app currently constructs both runtimes on every ordinary startup.

Host-created integration bridges and QML endpoints must be released before their
runtime owners. Current main declares the bridges and QML engine after the owners,
so normal stack destruction follows that ordering. This incremental change does
not register/remove navigation entries or implement live unloading.

FileOrganizerRuntime preserves `pending-files.json`, rule/layout paths, SQLite
history/journal paths, default-rule seeding, collision preferences and destination
permissions. Its availability timer and all feature-internal connections are owned
with the feature. The shared `BackgroundTaskRunner` is destroyed before controllers,
operation assembly, settings and repositories: its destructor joins pending work,
then drops queued UI completion/progress callbacks. Startup recovery inspection,
fresh preparation and explicit approval still follow the existing workflow. The
classification query is independent of the editor. The host injects that query and
the pending-queue contracts into Folder Watch, then destroys Watch before Organizer.

CompanionRuntime preserves the presentation fallback, pack selection/import paths,
per-pack scales, placement and frame edits, window anchor/visibility and walking
preferences. Pack import uses its own existing runner; the library/loader outlive
that runner's join. A settings controller may be destroyed before the import runner;
import work only borrows the still-live library, and its QPointer-guarded UI
completion is discarded. Screen/application connections and position-save timers
have feature-owned QObject contexts. `startupResult()` reports an unusable required
presentation/default verification pack to the host. The host still creates QML
windows, exposes borrowed endpoints, combines task busy state for application exit,
and chooses notification/activity routes. Companion owns no file-operation worker.

The existing verification mode uses bundled rules, skips queue restoration/saving
and availability polling, and ignores personal Companion preferences. Companion
verification edits use an in-memory map. Those test settings are not module toggles.

## Enablement and UI ownership: target behavior

Initially modules are compiled into one app. No DLL discovery or hot unloading is
required. The host reads requested enablement, resolves required dependencies,
constructs enabled modules and registers their pages, settings and bridges.
Changing module enablement takes effect after restart. The module-management page
and general app settings belong to the host and remain accessible.

| Disabled capability | Result after restart |
| --- | --- |
| File Organizer | No organizing/rules/history pages or file jobs; dependent Folder Watch is inactive with an explicit dependency reason |
| Folder Watch | No watchers/timers or watch-owned settings entry; manual organizing continues |
| Notifications module | No notification delivery/page; optional publishers can use a host-provided no-op channel; source features continue |
| Companion | No desktop companion, animation timers or companion pages; organizing/watch/notification state does not depend on it |
| One notification type | Existing center still tracks its active/withdrawn state while delivery is muted; unrelated notification types and work activity continue |

Whole-module disablement differs from muting a type or hiding the desktop
companion. A disabled notification module need not retain transient facts; enabled
sources republish current facts at startup. Saved preferences, rules, packs,
pending paths, history and recovery evidence are never deleted by disablement.
Do not silently re-enable an explicitly disabled dependency.

The host removes all UI contributions owned by an inactive module and rejects
stale navigation to them. Feature settings remain on their feature's page;
module management controls enablement and dependency explanations. Companion
artwork/cues remain presentation data. Module tags, new pack fields and third-party
plugin manifests are outside this first refactor.

Native tray notifications as an alternative renderer are a later feature, not a
current guarantee. Without a companion, proposals must remain reachable through
ordinary feature UI. Notification muting cannot hide required operation approval
or recovery decisions. Shutdown waits for active jobs before destroying their
owners; no runtime feature unload is introduced.

## Adding a future capability

A feeding feature could own feeding rules/state and request a companion
interaction through its public capability, without depending on File Organizer.
Its optional visual cues would be resolved by the companion's presentation layer.
Missing artwork needs a defined fallback rather than preventing unrelated features
from starting. This is an extension example, not a change to ImagePack v1:
feeding assets, module tags and new manifest fields still require their own design.
The same ownership/request/update rules apply to additions unrelated to companions.

## Incremental implementation order

1. **Pending-queue seam: implemented.** WatchController receives separate query and
   enqueue interfaces. `PendingQueueAdapter` uses the current model's typed
   `addPath` result so duplicate, overlap, availability and persistence behavior
   are reused. Host-side connections forward queue changes. Query failure
   withdraws stale offers; command failure is displayed without inventing a
   successful admission. Partial success reports both admitted and rejected paths.
   Proposal paths are copied before enqueueing because queue changes clear offers.
   Both adapter methods require the model's owning thread; no worker thread touches
   the model. Only whole-command failures precede mutation; individual failures are
   returned as per-path rejections.
2. **Classification query: implemented.** `ClassifyFilesUseCase` provides the
   Organizer capability. Watch has no rule-repository or rule-engine dependency.
   The host wires the query to active rules and the existing engine/hasher. Watch
   retains scanning, file readiness and thresholds. Rule changes invalidate old
   offers; query results never bypass fresh preview or movement approval.
3. **Integration adapters: implemented.** `FeatureConnections` owns the existing
   notification/activity routes outside `main.cpp`, retaining channel contracts,
   stable source/cue IDs, initial synchronization and ordinary delivery behavior.
   Main chooses the routes; features do not call peer controllers to render them.
4. **Host lifecycle and UI contributions: in progress.** NotificationRuntime,
   FolderWatchRuntime, FileOrganizerRuntime and CompanionRuntime own their internal
   objects and preference connections. Next introduce owned UI contributions,
   initially with all modules enabled.
5. **Persisted enablement and restart.** Add module management only after disabled
   startup combinations work. Check missing optional consumers, required-dependency
   reasons, preserved settings/data and absence of timers from inactive features.

Each step is independently reviewable. Tests cover queue-model-free WatchController
operation, read/command failure, invalidation during enqueue, per-path partial
results and real-model admission/persistence behavior. Classification tests cover
snapshot consistency, matched/excluded/unmatched/folder results, same-version rule
changes, invalid query responses and failure propagation. Acceptance still only
enqueues valid paths and leaves original files untouched. Integration tests also
cover initial delivery, muting/re-enabling, inspection versus file work, work and
delivery failures, missing optional consumers, queued-event lifetimes and invalid
thread binding. Runtime tests cover destruction of exposed controllers, preserved
settings, transient-mode isolation, failed preference writes, same-folder snooze
restoration and cancellation of delayed watch scans after owner destruction.
Organizer/Companion ownership tests exercise real enqueue/restore, explicit
preparation/confirmation, move/undo, queue persistence, shutdown during real
preparation, worker joining and discarded stale completions. They also cover
per-pack selection/scale/placement/edit restoration, verification-mode isolation,
reported startup failure, and removal of delayed position/application callbacks.
Step 4 continues with UI registration.
