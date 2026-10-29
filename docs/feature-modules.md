# Feature modules: boundaries and communication

Status: incremental refactor, 2026-10-29. The pending-queue, classification-query
and integration-adapter seams (steps 1 through 3) are implemented. Persisted module
selection and the host-owned management page are implemented. Plugin loading remains planned.
Step 4 now has scoped runtimes for Notifications, Folder Watch, File Organizer
and Companion. Companion management-page/menu registration, an explicit desktop
renderer contribution and a scoped tray action are implemented. File Organizer registers its
sort, rule-editor, history, recovery and collision pages as one scoped batch.
Recovery/collision remain auxiliary routes rather than home menu entries. Organizer
settings and its recovery notice are supplied as optional feature-owned fragments;
general theme settings remain host-owned. Watch now supplies its settings-detail
route, summary entry and proposal renderer; Notifications supplies its page/menu.
FeatureRuntimeHost now creates only the effective features selected at startup.
Saved selections apply on the next launch; temporary command-line disables remain available.
Restart is manual: save, exit through the tray, and launch again.
The current adapter reuses the existing queue model; ownership of queue state has
not yet moved out of the presentation layer.

## Module units

A feature module owns a coherent capability, its use cases, state and UI entries.
It is larger than a worker such as Scanner or RuleEngine. The existing
`src/modules/` directories are implementation groupings, not switchable features.
Modules can use separate instances of the same stateless worker implementation.

| Module ID | Owns | Public capabilities | Required feature dependency |
| --- | --- | --- | --- |
| `file-organizer` | Pending queue, active/draft rules, rule editor, previews, approval, movement/undo, history and recovery | Pending-path query, enqueue command, read-only classification query, existing sort workflow | None |
| `folder-watch` | Watched paths, timers, file readiness, thresholds, proposals and snooze state | Watch configuration, current proposal, accept/dismiss/snooze actions | `file-organizer` |
| `notifications` | Type registration, active notification facts, per-type delivery preferences, notification settings | Existing `INotificationChannel`, notification delivery updates | None |
| `companion` | Pack library, desktop companion, preview/editor, visual behavior, interactions and personal overrides | Existing `ICharacterChannel`, interaction/behavior capabilities | None |
| `companion-feeding` | One-shot feeding handler and its scoped registration | `companion.feed` interaction / `interact.feed` presentation cue | `companion` |

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
| [WatchController](../src/presentation/watch_controller.hpp) | Uses `IWatchProposalUseCase`, `IPendingPathsQuery` and `IEnqueuePendingPaths`; no queue-model dependency | Requires an active Organizer; capability lifetimes remain borrowed |
| [Pending queue contracts](../include/nookbit/application/pending_queue.hpp) and [adapter](../src/app/pending_queue_adapter.hpp) | Standard C++ path snapshots and per-path admission receipts; host forwards model changes | Adapter deliberately retains the existing model and its admission policies |
| [Watch evaluation](../src/application/use_cases/evaluate_watch_folder_use_case.cpp) | Owns scanning, pending-path exclusion, readiness and thresholds; uses `IClassificationQuery` | Feature lifecycle is still assembled by the host |
| [Classification contract](../include/nookbit/application/classification_query.hpp) and [use case](../src/application/use_cases/classify_files_use_case.cpp) | Organizer-side active-rule loading and matching; ordered per-file decisions and content-based rule revision | Belongs to Organizer, not an independently selectable capability |
| [Notification channel](../include/nookbit/application/notification_channel.hpp) | Typed registration/update data; no knowledge of a renderer; source/delivery wiring is in FeatureConnections | NotificationRuntime owns controller creation and enabled-preference callbacks; type registration is still selected by the host |
| [Companion channel](../include/nookbit/application/character_channel.hpp) | Typed activity/attention input; separate interaction and behavior managers | CompanionRuntime owns desktop objects; host chooses available integrations |
| [Sort workflow](../include/nookbit/application/sort_workflow.hpp) | Preparation is separate from explicit approval/execution; absent when Organizer is disabled | Shared progress/approval UI remains host-owned |
| [Integration adapters](../src/app/feature_connections.cpp) | Watch-to-notification, notification-to-companion and file-activity-to-companion bindings have a scoped owner | No feature lifecycle/enablement registry yet |
| [Folder Watch runtime](../src/app/folder_watch_runtime.hpp) | Owns watch settings repository, scanner, evaluation, controller/timers and snooze persistence; supplies settings-detail route, summary entry and proposal renderer | Organizer capabilities and preferences are injected; host still chooses attention activation |
| [Notification runtime](../src/app/notification_runtime.hpp) | Owns notification controller/center and enabled preferences; registers its settings page/menu | Module enablement, source registration and delivery routes remain host responsibilities |
| [Organizer runtime](../src/app/file_organizer_runtime.hpp) | Owns queue persistence/availability, rules/editor, classification, previews, approval, file-operation assembly, history/recovery and a shared task runner; supplies five pages, settings and recovery-notice fragments | Queue state still resides in the existing model; host still composes shared progress/approval and developer entry points |
| [Companion runtime](../src/app/companion_runtime.hpp) | Owns presentation/pack loading, preferences/edits, import runner, desktop placement, interactions, motion and internal connections; supplies management-page/menu, desktop renderer and tray contributions | Host still chooses construction, attention actions, cross-feature routes and application exit policy |
| [UI-page registry](../src/app/ui_page_registry.hpp) and [page host](../src/presentation/qml/RegisteredPageHost.qml) | Lifetime-scoped feature pages, borrowed inputs, atomic registration, menu visibility and explicit extension/settings-detail placement | General settings/developer host routes remain; this is not module enablement or live unloading |
| [Application wiring](../src/presentation/main.cpp) | Loads saved choices, applies CLI disables, supplies FeatureRuntimeHost and hosts module management | Feature catalog is compiled in; no plugin discovery or live unload |

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
  -> FeatureRuntimeHost (creates the selected runtimes and their connections)
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
setting, but only when the feature is selected for this run.

Host-created integration bridges and QML endpoints must be released before their
runtime owners. FeatureRuntimeHost destroys bridges before feature owners; Main
destroys the QML engine and tray contribution before FeatureRuntimeHost. Companion management-page
registration is described below; live unloading is not implemented.

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
presentation/default verification pack to the host. The host loads the supplied
desktop renderer, combines task busy state for application exit, and chooses
notification/activity routes. The renderer receives explicit borrowed inputs;
individual Companion controllers are no longer QML context globals. Companion
owns no file-operation worker.

The existing verification mode uses bundled rules, skips queue restoration/saving
and availability polling, and ignores personal Companion preferences. Companion
verification edits use an in-memory map. Those test settings are not module toggles.

### First UI contribution: Companion management

`CompanionRuntime::registerUi()` supplies stable ID `companion.settings`, owner
`companion`, existing route `5`, menu title/icon/section/order, the bundled
CompanionPage QML URL and its character/settings/placement/motion/edit inputs.
Keeping route 5 preserves existing `windows/characterSettings/size` preferences and shared
UiMetrics defaults. No page numbers or saved formats are migrated in this step.

The host's `UiPageRegistry` stores value snapshots and borrowed QObject inputs;
it neither constructs services nor owns feature state. Inputs must outlive the
registration's QObject context. Companion uses its internal connection context,
which is destroyed before its controllers. Context destruction removes the page
and menu metadata. Registration requires the registry/context owning thread and
rejects duplicate stable IDs/routes, host-reserved routes, incomplete menu metadata
and sources outside bundled QRC QML. This permits compiled app pages, not arbitrary
external plugins. A short-lived registry disconnects its lifetime callbacks safely.

CompactHome merges registered menu entries with the remaining host entries.
RegisteredPageHost creates each registered page with its explicit feature inputs
plus host-provided guide service, overlay parent and live theme. Initial visibility
is supplied before completion, so hidden history pages do not query during startup.
Pages remain instantiated while
navigating elsewhere, retaining their draft and preview state. Page-owned popup
cleanup still runs on hiding; removal of registration releases its loader/page.
The host resolves the extension title from registration and rejects missing routes,
returning to home if an open registered page disappears. The stable-ID route lookup
is also used by the existing desktop Companion settings action.

### File Organizer page contributions

`FileOrganizerRuntime::registerUi()` registers five entries together:

| Stable ID | Existing route | Feature-owned UI adapter | Home menu |
| --- | --- | --- | --- |
| `organizer.sort` | 7 | OrganizerSortPage | Yes, order 10 |
| `organizer.rules` | 1 | OrganizerRulesPage | Yes, order 20 |
| `organizer.history` | 4 | OrganizerHistoryPage | Yes, order 30 |
| `organizer.recovery` | 10 | OrganizerRecoveryPage | No |
| `organizer.collision` | 6 | OrganizerCollisionPage | No |

The runtime supplies borrowed controller/model/progress inputs and menu metadata;
Main no longer constructs these pages or owns their menu titles. The existing
UiMetrics defaults and `windows/fileSort/size`, `windows/ruleEditor/size` and
`windows/history/size`, `windows/recovery/size` and `windows/collisionSettings/size`
preference keys are unchanged. General settings and developer UI remain host
contributions. Business logic is not moved
into the page registry. `inMenu` defaults to true; false keeps an auxiliary page
routable and titled without creating another home menu row.

`UiPageRegistry::addPages()` validates the entire batch before mutating the registry.
Duplicate names/routes within a batch or conflicts with existing/reserved entries
leave the previous registry intact. One update reports successful registration;
owner-context destruction removes the whole batch with one update. The single-page
API uses the same validation. These are startup composition operations, not a new
permission to replace active feature runtimes or discard dirty editors live.

OrganizerSortPage forwards existing file-sort navigation requests and exposes its
read-only workflow state to host progress/confirmation routing. The actual
FileSortPage keeps its selected files, preview/confirmation/movement/completion
flow and cancellation behavior. OrganizerRulesPage owns the old developer form
and its dirty/reset links to RuleEditorPage; leaving the page closes its helper
window while preserving the form draft. The developer form request now opens its
owning rules page first. OrganizerHistoryPage refreshes history on opening and
connects the shared task state and overlay parent to the existing history view.

OrganizerRecoveryPage owns read-only preparation on opening, including a repeated
open of an already visible page. A hidden initial page does not start inspection.
The existing RecoveryPage still owns approval/cancellation and its confirmation
overlay; hiding it cancels pending approval. The adapter observes the feature's
task runner for inline inspection state. OrganizerCollisionPage connects the
existing policy view to the same shared preview/settings controller.

PageSurface's `routeRequested(int)` distinguishes navigation to another page from
opening/closing the current registered page. Main only routes recovery requests
through `organizer.recovery`; it no longer starts the inspection itself.
Registered page lookup retains loaded instances;
ordinary navigation does not reconstruct them. Host file-sort overlays observe the
registered sort adapter rather than a directly declared FileSortPage. The final
approval path, serialized execution, undo and recovery contracts are unchanged.

### Organizer settings contributions

`FileOrganizerRuntime::settingsUi()` provides a settings-section QML URL with
borrowed sorting/destination inputs and a separate recovery-notice URL with its
read-only recovery input. These are compiled UI fragments rather than navigation
pages or a new saved-file format. As with the desktop descriptor, the host must
destroy QML before the runtime; normal app declaration order preserves that rule.

AppSettingsPage retains theme controls and receives optional fragments through
RegisteredSettingsSection. The common loader supplies live theme, a visual leading
entry slot and outward route requests. Its size comes from Loader's loaded item.
The notice's `hasContent` observes recovery facts independently of effective item
visibility, so it can appear/disappear without reconstructing the settings page.
Removing a contribution releases only its views and leaves host theme controls.

OrganizerSettingsSection owns the existing collision-policy picker and allowed
destination list/revocation controls. OrganizerRecoveryNotice only requests route
10; displaying it never executes recovery. The host inserts a Watch-provided entry
into the leading visual slot, preserving its placement above collision controls
without giving Organizer a WatchController dependency. AppSettingsPage
and SettingsWorkflow no longer receive sorting/destination controllers or a
host-computed recovery flag. SettingsWorkflow still owns the existing same-size
slide into the registered settings detail; the theme policy remains host-owned.
Colors, spacing, behavior, settings keys and the three visible settings groups
are preserved.

### Folder Watch and notification UI contributions

`FolderWatchRuntime::registerUi()` supplies `watch.settings`, existing route 3,
hidden from the home menu and placed in `UiPageHost::SettingsDetail`.
`NotificationRuntime::registerUi()` supplies `notifications.settings`, existing
route 11, in the normal extension host with home menu order 60. Source/type
registration and delivery wiring remain separate host decisions. Both runtimes
have a UI lifetime context declared after their controller: page registration is
removed before the controller's derived state is destroyed.

UiPageRegistry returns `host` placement metadata and an `extensionPages` view.
Main creates only extension-host pages in its ordinary repeater; SettingsWorkflow
receives the Watch settings-detail descriptor separately. This prevents duplicate
WatchSettingsPage instances and competing drafts. Unknown host values are rejected.
Settings-detail routes continue to share the general settings window size/key,
preserving the existing same-size slide. The current compiled host selects the
Watch detail slot explicitly; this is not arbitrary third-party page discovery.

FolderWatchSettingsPage adapts the existing watch editor to registered-page
services. SettingsWorkflow no longer creates WatchSettingsPage or receives a
WatchController; it only controls the transition, theme, overlay and navigation
requests. Its `detailOpen`/`detailPage` inputs replace the Watch-specific view input.
Settings drafts, rescan fields, pattern editing, explicit saves, failed-save draft
preservation and hide/reopen behavior stay in the existing editor.

`settingsEntryUi()` supplies FolderWatchSettingsEntry and its controller input.
The feature now owns summary formatting and the settings-open request. AppSettingsPage
only hosts that optional contribution in the Organizer leading slot; absent Watch
metadata leaves the slot/divider out. It has no WatchController or condition-format
dependency. NotificationsPage owns the type list/status and per-type enable calls;
the common home menu/title rendering uses its registration instead of a fixed row.

`proposalUi()` supplies FolderWatchProposal, which owns WatchProposalDialog wiring.
RegisteredDialogHost receives the optional descriptor, overlay, busy state and
theme, and forwards open/close and accepted events. The feature connects enqueue
and snooze; the host chooses when attention is actionable and collapses navigation
after acceptance. Closing/removing a proposal renderer does not enqueue, snooze or
execute file work. Admission still uses the existing queue contract and movement
still requires a new preview and approval. Main keeps Watch/notification fact
endpoints for home summaries and cross-feature attention selection; it does not
change their policies through the settings UI itself.

As with other borrowed UI descriptors, the QML engine/hosts must be destroyed before
their feature runtimes. Descriptor removal tests cover renderer cleanup, not live
backend unloading. No JSON format, preference key, thread or module-enable switch
is added by this refactor.

### Companion desktop renderer and tray contribution

`CompanionRuntime::desktopUi()` supplies a bundled DesktopCompanion QML URL and
explicit character/settings/placement/interactions/motion inputs. As with page
inputs, these are borrowed: the host must destroy QML before the runtime and keep
its preferences/application alive longer. A failed startup returns an empty
contribution. This descriptor is separate from UiPageRegistry; a desktop renderer
does not need a navigation route or menu title. No generic plugin registry is added.

RegisteredDesktopHost loads that optional descriptor and forwards theme, attention
availability, state commit, developer preview and outward requests. An empty
descriptor creates no window; clearing it destroys the renderer, character and
bubble windows and their QML timers. This is a rendering-boundary contract, not
permission to unload the runtime during active work. The host still controls safe
shutdown ordering and module changes remain restart-based.

DesktopCompanion owns the actual CharacterWindow bindings, visual bounds, gestures,
scale/placement, bubble preview and reaction gating. It knows no watch controller,
notification center or file-operation runner. Attention activation only emits a
request; Main chooses whether the current folder-watch offer is actionable and
opens the existing proposal UI. The settings request carries `companion.settings`.
The home portrait reads the renderer's summary, with an empty fallback; the developer
preview button is unavailable without a renderer. Closing the main window still
commits desktop position and leaves the companion running through the tray policy.

`createTrayAction()` returns a host-owned scoped CompanionTrayAction. It owns its
QAction and synchronizes the existing visibility controller in both directions.
Destroying the contribution removes only its menu entry. If the feature endpoint
dies first, the entry is removed and disabled; if the external menu dies first,
QPointer guards permit later feature/binding teardown. Ordinary shutdown releases
the contribution before the menu and runtime. The system-tray icon, open/exit
actions, exit-while-busy policy and application lifecycle remain host-owned.
Qt Widgets was already used by the app; this adds no new distribution dependency.

Desktop, tray and page contributions are now absent when Companion is not selected
at startup. This does not introduce live backend unloading.

## Conditional feature startup

`FeatureSelection` records the request; `resolveFeatureStartup` produces the
effective selection and dependency explanations before creating any runtime.
`FeatureRuntimeHost` owns only the effective runtime instances and their routes.
Main supplies shared paths/preferences/application services; QML receives borrowed
nullable endpoints and optional contribution descriptors. A disabled feature has
no runtime, worker, feature timer, image provider, page, settings fragment or tray
action. General settings and the application lifecycle remain available, including
when every feature is disabled. Existing feature data is retained for reactivation.

On first launch, all features are enabled. The host-owned **모듈 관리** page stays
available even with every feature disabled. Its five switches edit a draft;
**저장** persists the requested selection and **변경 취소** restores the saved draft.
Each row distinguishes the current runtime state from the effective next-launch
state. A requested Watch remains remembered when Organizer blocks it, with an
explicit dependency explanation. Enabling Organizer later restores that request.

Saving does not unload runtimes, hide current feature UI, or stop running jobs.
The page displays restart guidance only after a successful save changes effective
features. Use the tray's **완전 종료**, then launch again; closing the main window
normally just hides it. Automatic process restart is not implemented, preserving
the existing task-exit protection and avoiding overlapping app instances.

`FeaturePreferences` stores one compact JSON value in QSettings at
`modules/selection`, separate from all feature-specific keys/files:

```json
{"schemaVersion":1,"enabled":{"file-organizer":true,"folder-watch":true,"notifications":true,"companion":true,"companion-feeding":true}}
```

The document must have version 1 and actual Boolean values for the original four
feature IDs; documents over 16 KiB are rejected. Feeding is an additive optional
Boolean key: when absent in old documents it follows the saved Companion flag.
A missing selection document uses the
first-launch defaults without writing them. An unreadable/invalid document starts
no features, leaves the original stored value intact, and exposes repair guidance
in settings and module management. An explicit save can replace that value.
Failed writes report an error, retain the draft, and restore the previous in-memory
preference value rather than claiming success. Other settings are untouched.

`FeatureSettingsController` receives saved choices, an immutable active snapshot,
and a save callback. It owns no runtime or worker. The module page receives this
controller explicitly and uses shared UI controls/theme/metrics. Its host route is
12, with its own `windows/modules/size` preference; existing route numbers remain.

For one run, overlay the saved choices with repeatable command-line disables:

```powershell
.\nookbit.exe --disable-feature=companion
.\nookbit.exe --disable-feature=notifications --disable-feature=folder-watch
.\nookbit.exe --disable-feature=file-organizer
.\nookbit.exe --disable-feature=all
```

IDs are `file-organizer`, `folder-watch`, `notifications`, `companion` and `companion-feeding`.
Unknown or empty IDs fail before runtime creation. These options only disable
features from the saved baseline; they never enable a saved-disabled feature.
They do not save a module preference. The next launch without options uses the
saved selection. The module page labels sessions with temporary overrides.

Folder Watch requires File Organizer. Disabling Organizer leaves Watch inactive
and shows the dependency reason in general settings if Watch was requested.
No disabled dependency is silently enabled. Notifications and Companion are
optional consumers: missing consumers omit their connections rather than creating
replacement workers. Watch offers remain accessible using **정리 제안 확인** in
Watch settings, even without either consumer. Only active Watch registers its
notification type. Work-to-Companion activity does not depend on Notifications.

The shared approval/progress/developer UI and home portrait use conditional loaders;
stale routes to unavailable pages return home. Hosts are destroyed before borrowed
feature endpoints; connection adapters die before Companion/Notifications/Watch,
and Watch dies before Organizer. Existing runner destructors join work before
their dependencies disappear. No new thread or saved format is introduced.

CTest exercises all 32 requested combinations through the real app using
`--verify-feature-startup`. Each case uses temporary data and INI preferences,
disables automatic scanning, and checks runtime/page/window/tray presence, absent
notification sources, dependency messages, stale routes and QML binding warnings.
The normal all-enabled UI smoke test remains separate and ignores personal module
choices. Selection tests also cover invalid/repeated options and dependency state.
Preference/controller tests cover all 32 save/reopen combinations, invalid documents,
failed saves, draft discard, CLI overlays, preserved unrelated preferences and
immutable current state. Four `--verify-feature-preferences` app cases seed only a
temporary INI and exercise the real preference-loading path, module switches/save,
dependency resolution, disabled UI absence and the unchanged current desktop
window after saving. No verification mode reads or writes personal module settings.

## Enablement and UI ownership

Initially modules are compiled into one app. No DLL discovery or hot unloading is
required. The host reads requested enablement, resolves required dependencies,
constructs enabled modules and registers their pages, settings and bridges.
Changing module enablement takes effect after restart. The module-management page
and general app settings belong to the host and remain accessible.

| Disabled capability | Result after restart |
| --- | --- |
| File Organizer | No organizing/rules/history pages or file jobs; dependent Folder Watch is inactive with an explicit dependency reason |
| Folder Watch | No watchers/timers or watch-owned settings entry; manual organizing continues |
| Notifications module | No notification delivery/page; host omits optional routes; source features continue |
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
4. **Host lifecycle and UI contributions: implemented.** NotificationRuntime,
   FolderWatchRuntime, FileOrganizerRuntime and CompanionRuntime own their internal
   objects and preference connections. Companion supplies its management page/menu,
   desktop renderer and scoped tray action. Organizer supplies sort/rules/history,
   auxiliary recovery/collision pages, settings and recovery-notice fragments.
   Watch supplies the settings slide/entry and proposal renderer; Notifications
   supplies its settings page/menu. General settings/developer UI and shared
   progress/approval integration remain host-owned and conditional on Organizer.
   FeatureRuntimeHost supports absent features and optional connection routes.
5. **Persisted enablement and restart: implemented.** Host-owned module management
   edits requested choices and saves them separately from active runtimes. The next
   launch resolves dependencies and creates only effective features. Manual restart
   retains feature data and existing task-exit protection; no live unloading or
   automatic process restart is introduced.

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
UI-page tests cover duplicate/reserved route rejection, registration order,
independent owner lifetimes, actual Companion input registration, and failed-startup
non-registration. QML boundary tests cover explicit page inputs, live theme,
open/close routing, retained drafts, missing-registration cleanup and menu removal;
the app smoke test exercises the registered Companion page and existing editor.
Desktop-boundary tests cover absence/creation/removal of both independent windows,
explicit inputs, theme and attention updates, reaction gating, stable settings
requests, visibility cancellation, preview and pending-position commit without app
globals. Tray tests cover both-direction visibility updates and teardown in binding-,
feature- and menu-first order. Runtime tests check the desktop descriptor inputs and
failed-startup non-contribution. Existing app smoke tests retain gestures, anchors,
bubbles, editing and close-to-tray checks.
Batch tests cover conflicts late in a batch, duplicate names/routes inside a batch,
single-update registration/removal and actual Organizer endpoint ownership. QML
tests cover overlay-service injection and separate cross-page navigation. The app
smoke tests traverse the registered sort/rules/history views, including the manual
sort workflow, rule editor helper windows, history and preserved window sizes.
Auxiliary-page tests cover routability without a menu row and actual recovery,
collision and settings input ownership. QML tests verify hidden recovery startup,
read-only preparation/reopening, cancellation without execution, live notice
appearance/removal, feature-view cleanup, retained theme controls and unchanged
Watch sliding. Existing app smoke checks retain inline recovery/progress overlays
and unlocked window movement while tasks run.
Watch/notification runtime tests cover registered endpoint identity, settings-detail
placement, descriptor inputs and registration cleanup. Registry tests ensure detail
pages are not also extension pages. QML tests retain same-size sliding, numeric and
pattern edits, failed-save drafts and notification toggles; proposal tests distinguish
accept, snooze, close, busy input blocking, absent contributions and cleanup while
open without extra commands. App smoke tests exercise the actual registered screens.
Startup matrix tests additionally cover disabled-feature composition and QML absence.
