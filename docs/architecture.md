# NookBit 설계와 확장 원칙

File-work state uses `ICharacterChannel`; bounded result presentation uses the
separate `ICharacterReactionChannel`. BackgroundTaskRunner emits a completion fact
only for FileWork tasks, and FeatureConnections maps it to optional pack cues.
CharacterController owns the short presentation deadline and re-evaluates current
behavior when it expires. Active file work wins over result and interaction cues;
a new work request discards an old result instead of replaying it later. Result
delivery does not register notifications or execute interactions/file operations.

Typography is defined by UiMetrics. Startup builds a fresh UI font from those
roles, clears inherited named styles/variable axes, and assigns it to both the
application default and `UiMetrics.applicationFont`. Main pins that full font;
the extension window inherits it explicitly. Page controls override only role
sizes/weights. This avoids relying on ambient system/theme font inheritance.
Font tests inject a Thin named style/weight axis and verify stable regular/bold
selection across separate windows and loader-created content, without displaying
the test windows.

The NativeTextRendering experiment worsened text on the affected machine and was
reverted. Qt's default scalable text renderer is retained. The application now
embeds static Noto Sans KR faces under the private family `NookBit UI Sans` and
registers them before creating controls. It no longer selects the installed
variable font by name. Roles and sizes are unchanged. Font tests verify actual
Korean glyph runs use this private family, static font tables and the expected
weights. See [font provenance and reproduction](../resources/fonts/README.md).
The exact cause of the original intermittent rendering issue was not reproduced;
visual verification on the affected machine remains necessary.

Companion preview edit drafts live in `CompanionEditState.qml`, independently of
the view, playback clock and persistence service. The view supplies frame metadata
and personal offsets, owns playback/camera requests, and emits apply requests.
Standalone state tests cover draft isolation, coordinate offsets, frame alignment,
duration overrides and reset/cancel. Preview playback additionally requires a
visible, non-minimized containing window, while preserving the user's playback
request during temporary window hiding. Rule connection cancellation clears only
temporary wire state on pointer cancellation and page exit.

The optional [feeding feature](companion-feeding.md) borrows only the interaction
registration capability. Its scoped owner registers a handler and menu metadata;
Companion owns generic input/rendering and the host enforces the dependency.

History search uses a separate read-only `IHistoryQuery` capability. The host
injects it alongside history persistence and file operations; SQL queries do not
scan live files. Filtered pagination and full-plan undo scope are documented in
[History search](history-search.md).

Feature-module ownership, communication contracts, disabled-state behavior and
the incremental refactor plan are recorded in [Feature modules](feature-modules.md).
The plan distinguishes existing interfaces from module lifecycle work not yet implemented.

The first feature-module seam is implemented: WatchController queries pending
paths through `IPendingPathsQuery` and registers accepted proposals through
`IEnqueuePendingPaths`. An Organizer-owned `PendingQueueAdapter` reuses the current
model's admission rules; host connections forward queue changes. WatchController
does not include or hold PendingFilesModel. Queue state is still owned by that
model, so this is an incremental boundary rather than a completed backend rewrite.

Watch evaluation now delegates active-rule loading and matching to
`IClassificationQuery`. The Organizer-side `ClassifyFilesUseCase` returns ordered
decisions from one rule snapshot and a content-based revision, with no file-content
reads or writes. Watch retains scanning, file readiness and proposal thresholds;
the revision invalidates old offers and is rechecked before enqueueing. Actual
movement still requires the existing fresh preview and approval workflow.

Host-side `FeatureConnections` now owns Watch-to-notification,
notification-to-companion and file-activity-to-companion signal routes. Main
constructs services and selects those bindings; the bridge owns no feature state
or worker and preserves the existing input contracts and IDs. Notification
preferences remain independent from file-work activity. Lifetime guards and
scoped QObject connections prevent delivery to destroyed endpoints.
FeatureRuntimeHost now selects only available routes from resolved startup choices.

NotificationRuntime, FolderWatchRuntime, FileOrganizerRuntime and CompanionRuntime
now own feature-level object groups. File Organizer owns its queue, rule/query and
sort/history/recovery assembly; Companion owns its packs, personal edits, placement,
interaction and motion assembly. Main supplies preferences/paths, injects Organizer
capabilities into Watch, and exposes borrowed endpoints to QML and integration
bridges. Internal timers and preference callbacks end with their feature owner.
File work joins before its dependent repositories/controllers are destroyed; pack
imports join before their library is destroyed. Saved settings/data remain intact.
CompanionRuntime now registers its management page and menu in UiPageRegistry.
The generic page host receives explicit feature inputs, keeps drafts while hidden,
and uses shared theme/navigation services. DesktopCompanion now owns the independent
character-window bindings; Main loads its explicit descriptor instead of publishing
individual Companion controller globals. A scoped CompanionTrayAction owns the
visibility menu entry. Main retains the system-tray icon, app lifecycle, attention
action selection, general settings and module management.
FileOrganizerRuntime also registers sort/rules/history and auxiliary recovery/
collision pages as one atomic batch. Only primary pages create home menu rows.
Their small QML adapters own feature-internal screen wiring and recovery inspection
preparation;
the host supplies theme/guide/overlay services and routes navigation. Existing page
numbers and window-size preference keys are retained. Organizer supplies optional
settings and recovery-notice fragments; AppSettingsPage retains theme controls and
Watch navigation while RegisteredSettingsSection receives borrowed feature inputs.
The Watch entry is a visual slot, so Organizer does not depend on WatchController.
FolderWatchRuntime now supplies its settings-detail page, summary entry and proposal
renderer. SettingsWorkflow only hosts the registered slide; AppSettingsPage no
longer receives a WatchController. NotificationRuntime registers its settings page
and menu, with policy changes wired inside NotificationsPage. UiPageRegistry
distinguishes normal extension pages from settings-detail placement so Watch has
only one editor instance. Main retains source facts and attention selection,
general settings/developer UI, and progress/approval integration. Organizer-owned
host widgets and the home portrait are loaded only with their feature.
FeatureRuntimeHost conditionally creates the selected runtimes; disabling Organizer
also leaves Watch inactive with an explicit dependency reason. Notifications and
Companion are independent optional consumers. Temporary `--disable-feature=<id>`
options overlay the saved choices for one run without saving those overrides.
The host-owned module page edits a separate draft and persists requested selection
through FeaturePreferences at `modules/selection`. The current runtimes remain
unchanged until a full exit and fresh launch. Saved-disabled features retain their
own settings/data. Invalid module preferences start no features and leave module
management accessible for explicit repair. All 32 requested startup combinations,
saved-state startup cases and QML binding warnings are checked with isolated data.
See [runtime ownership](feature-modules.md#current-runtime-ownership)
for lifetime order and verification-mode behavior.

이 문서는 팀원이 기능을 붙일 때 **현재 코드가 어떻게 동작하는지**와 **어떤 방향으로 확장할지**를 구분하기 위한 안내다. 저장소의 코드가 최종 형태라는 뜻은 아니다. 특히 일부 컨트롤러는 아직 구체 구현을 직접 보유하고 있다.

## 제품의 기본 흐름

NookBit의 기본 입력은 사용자가 끌어온 파일의 **대기 목록**이다. 폴더 감시는 별도 입력 경로로서, 조건 충족 시 파일을 대기 목록에 넣을지 물어본다. 감시와 분류는 모두 파일 이동을 직접 시작하지 않는다.

```text
끌어놓기 ─┐
          ├→ 대기 목록 → 스캔 → 규칙 판단 → 이동 계획 미리보기
감시 제안 ─┘                                  ↓
                  계획 고정·재검사 → 승인 기록 → 안전 실행 → 작업 기록/되돌리기
```

규칙 편집기의 시험 기능은 편집 중인 초안에 대해 판단 경로를 보여주는 **읽기 전용** 작업이다. JSON 가져오기·내보내기와 활성 규칙에 적용하는 **전체 저장**은 서로 다르다. 실제 실행은 미리보기와도 분리되어 있다.

## 책임과 의존 방향

2026-10-06 파일 이동·마지막/기록 되돌리기·전체 중단 복구 실행은 main이 공유하는 BackgroundTaskRunner에서 한 작업씩 비동기로 실행한다. 컨트롤러는 승인 계획·선택 경로·설정을 값으로 고정하며 작업 스레드에서 화면 모델/QObject를 만지지 않는다. ThreadLocalOperations 조립부가 요청 스레드 안에서 SQLite 연결·일꾼·유스케이스를 생성하고 같은 스레드에서 해제한다. 파일 변경은 기존 시스템 잠금과 승인 재검사를 그대로 따른다. 결과는 화면 스레드로 돌아온 뒤 목록/기록에 반영한다. 화면 입력과 닫기는 실행 중 차단하고 앱 수명 종료 시 작업을 강제 종료하지 않고 기다린다. 정리 전 고정·재검사와 확인 후 승인 해시 계산도 백그라운드로 실행한다. 미리보기와 시작 복구 조회는 아직 동기이며 정확한 진행률·안전 취소는 후속이다.

| 층 | 이 프로젝트에서 하는 일 | 주요 위치 |
| --- | --- | --- |
| 도메인 | 파일 사실, 분류 결정, 계획·상태와 정책 표현 | `include/nookbit/domain/` |
| 애플리케이션 | 유스케이스가 작업 순서를 조율하고 필요한 일꾼을 포트로 요청 | `include/nookbit/application/`, `src/application/use_cases/` |
| 모듈 | 규칙 판단·계획 작성·감시 조건 평가 같은 개별 계산 | `src/modules/` |
| 인프라 | 디스크 스캔·이동, JSON, SQLite, 해시 등 외부 세계와 접촉 | `src/infrastructure/` |
| 표현 | QML 이벤트/화면 상태를 애플리케이션 작업에 연결 | `src/presentation/` |

대표적인 포트는 [`ports.hpp`](../include/nookbit/application/ports.hpp)에 있다. Scanner, RuleEngine, Planner, Executor, 저장소 등은 호출자에게 **무엇을 할 수 있는지**만 보여준다. 구체 구현은 바깥쪽에서 연결한다. 유스케이스는 예를 들어 Scanner → RuleEngine → Planner 같은 순서를 조율하지만, 서로 무관한 일꾼이 다른 일꾼을 직접 소유·호출하는 식의 결합은 늘리지 않는다. 데이터를 주고받을 때는 명시적 요청·결과 자료형을 사용한다.

같은 일꾼을 여러 유스케이스가 이용해도 된다. 객체를 유스케이스마다 새로 만들지 공유할지는 상태, 수명, 동시성에 따라 결정한다. **모든 일꾼이 싱글턴이어야 한다는 원칙은 없다.** 반면 SQLite 기록처럼 공유되는 영속 상태는 트랜잭션과 동시 접근을 신중하게 다뤄야 한다. 파일 작업에는 별도 직렬화와 기록 일관성이 필요하다.

Qt/QML은 화면 계층이다. QML이 파일 이동·DB·규칙 해석을 직접 구현하지 않도록 한다. 컨트롤러는 화면 입력을 변환하고 유스케이스를 호출하며 결과를 화면에 돌려준다. Feature-internal creation and wiring live in the runtime owners under `src/app/`; [`main.cpp`](../src/presentation/main.cpp) selects cross-feature routes and exposes UI endpoints. [`CompositionRoot`](../src/app/composition_root.cpp)는 서비스 묶음 검증에 쓰이지만, 현재 데스크톱 연결의 전부를 대신하지는 않는다.

2026-10-06 [`PreviewController`](../src/presentation/preview_controller.hpp)와 HistoryController의 Scanner/RuleEngine/Planner/Hasher 보유 및 SafeExecutor 직접 생성을 제거했다. main에서 조립한 IFileOperations 계약을 공유하고 FileOperationService가 요청별 SafeExecutor를 조립한다. 보호 경로와 전송 포트도 화면 밖에서 소유한다. PreviewController의 규칙 저장소 등 일부 구체 저장소 의존은 남아 있다. 이는 우리가 지향하는 “화면은 포트와 유스케이스에 의존” 방향에 남아 있는 결합이다. 새 기능에서 이 패턴을 무작정 복제하지 말고, 기능 경계가 분명할 때 유스케이스/포트로 옮기자. 다만 작은 수정을 위해 대규모 구조 개편을 끼워 넣지는 않는다.

## 규칙 모델

정리 버튼의 `ISortWorkflow` 계약은 확인 전 prepare와 확인 후 approveAndExecute를 분리한다. prepare는 작업 스레드에서 고정·내용 해시·저장 계획 재검사를 끝내지만 승인/이동하지 않는다. 결과를 화면에 반영한 뒤 awaitingConfirmation 상태로 확인창을 연다. 확인 이후에도 작업 스레드 전용 저장소로 표시 계획 해시·현재 파일/설정/규칙을 재검사하고 승인한 다음 기존 안전 실행기를 호출한다. 실패·취소에는 대기 목록을 제거하지 않으며 이동 결과 반영은 공통 finishMovement로 처리한다. 컨트롤러나 작업 스레드가 QML을 직접 호출하지 않는다.

전체 중단 복구도 main이 조립한 `IInterruptedOperations`를 RecoveryController에 주입한다. 컨트롤러는 표시 스냅샷과 확인 상태만 관리하고, 전체 검사/재개/되돌리기 일꾼을 생성하지 않는다. 구형 v1의 기록 확인을 위한 기존 Scanner/Hasher 의존은 호환 경로에 남아 있다. 공유 복구 서비스는 OperationMutex로 직렬화하고 실제 현재 설정·규칙을 다시 검사한다. 시작 시 자동 파일 작업·기록 정합화는 하지 않는다.

The primary rules format is the v3 tree. Each ordinary node checks one condition:
item type (file/folder), extension, name substring, file size or modified age.
Nested conditions compose as AND; the first matching sibling wins without
backtracking. Local `otherwise` branches and a global fallback remain separate;
explicit exclusion takes precedence. Every leaf has a destination or exclusion.

Destinations can be relative to the configured organizing root or absolute inside
separately user-approved external roots. Rules do not grant external-path access;
parent traversal remains invalid. See the [v3 notes](rule-tree-v3.md),
[current authoring contract](ai-rule-authoring-v3.md) and
[schema](../schemas/rules-tree-v3.schema.json). Update documentation, schema,
conversion and tests together when changing these semantics.

## 안전과 데이터 소유

다른 볼륨 복사는 [다운로드 표식 계약](download-marker-copy.md)의 서명된 보존 정책 1을 사용한다. 새 초안의 빠른 지원 검사 뒤에만 선택하고 기존 서명을 바꾸지 않는다. 표식 원문은 메타데이터로 보존하고 내용 해시는 기본 데이터 스트림을 검사한다. 제거 세션은 Qt 없는 `TransferRemovalProof`를 제공하며 유스케이스가 기록 포트로 삭제 전에 영수증·감사를 저장한다. Schema v9 기록 일꾼은 승인·확정 이벤트 연결과 증거 불변성을 검증한다. 원본 없는 정방향 복구는 새 읽기 전용 보호 세션에서 저장 증거와 파일을 대조해 기록만 완료한다. 옛 무증거 기록·중단 역전송은 차단하며 완료된 작업의 정상 되돌리기는 지원한다.

새 같은 볼륨 전용 작업은 [MetadataRename 계약](same-volume-fast-move.md)으로 전체 내용 해시를 요구하지 않는다. 이 정책은 서명에 포함되며 기존 승인이나 다른 볼륨/혼합 작업에 소급 적용하지 않는다. 확실한 실패는 파일별 감사로 남기고 계속하며, 불확실한 결과/DB 실패는 보존·중단한다. 새 정책의 중단 복구는 아직 차단한다. 아래 내용 해시·역순 보상 설명은 기존 Rename/VerifiedCopy 계약의 설명이다.

미리보기는 읽기 전용 `Draft`다. `previewOutcomes`는 미분류·제외까지 화면에 설명하기 위한 일시적 판단이며, 실제 실행 대상인 `steps`와 다르다. 계획 고정 때 파일·규칙·설정을 재검사하고 파일 내용 SHA-256을 기록한다. 저장 계획 검사와 승인도 현재 상태를 다시 확인한다. 승인 기록과 실제 실행은 별도 단계다. 실행기는 덮어쓰기 없는 이동과 이벤트 기록을 사용한다. 중간 실패 때 확실한 것만 역순으로 되돌리고, 모호하면 `NeedsRecovery`로 멈춘다. 복구 화면도 확신할 수 없는 파일을 자동으로 옮기지 않는다.

앱 데이터의 위치는 운영체제의 `QStandardPaths::AppDataLocation` 결과로 정한다. `rules.json`은 활성 규칙, `rule-graph-layout.json`은 그래프 표시 위치, `watch.json`은 감시 설정, `history.sqlite`는 계획·승인·실행 기록이다. 저장소 안의 `resources/rules/default.v3.json`은 첫 실행 기본값이지 사용자 규칙의 실시간 원본이 아니다. 작업 중인 초안과 활성 규칙, SQLite 기록을 혼동하지 않는다.

Pending-list persistence uses the `IPendingFilesRepository` contract and `JsonPendingFilesRepository` implementation. `FileOrganizerRuntime` owns restoration, change-signal persistence and the repository; PendingFilesModel only manages paths and availability. The `pending-files.json` format is `{ "schemaVersion": 1, "paths": ["absolute path"] }`, without plans or approvals. Whole-document validation limits it to 16 MiB, and QSaveFile replaces it atomically. A failed restore preserves the original document and disables automatic writes for that run. Restoration deduplicates paths while retaining unavailable entries. Availability polling invalidates stale previews; actual movement still requires a new preview and final confirmation after restart.

## 기능을 추가할 때

알림·Interact·행동의 책임과 데이터 수명은 [컴패니언 행동 설계](companion-behavior-design.md)를 따른다. 알림 종류는 `INotificationChannel`로 발생 상태를 전달하고, 등록 정보로 설정 목록을 만든다. 표시 허용 여부와 실제 기능 실행은 분리한다. `InteractionManager`는 등록된 처리기로 요청을 전달하고 세션을 관리하며, `BehaviorManager`는 등록된 조건으로 기본 행동을 고른다. `IdleWalkState`는 시간과 수평 범위만으로 모션을 계산하고 Qt 어댑터가 필요한 갱신 시점을 예약한다. 실제 창 위치와 행동 창구의 연결은 조립부가 담당한다. 기본 행동과 일시 반응을 분리하되 반응 큐·일반 행동 스케줄러·동적 모듈 로딩은 후속 단계다.

정리 실패·재시작·되돌리기·감시 등록 흐름의 재현 테스트와 현재 보장 범위는 [백엔드 안전 점검](backend-safety-checks.md)에 정리한다.

1. 사용자 동작과 안전 경계를 먼저 정의한다. 파일을 바꾸는지, 단순 조회인지, 승인이 필요한지 구분한다.
2. 입력·결과 자료형과 필요한 포트를 코어에 두고, 유스케이스가 순서를 조율하도록 한다. 기존 포트가 맞으면 재사용한다.
3. 구체 일꾼은 해당 모듈·인프라에 구현하고 앱 시작 지점에서 연결한다. QML에는 표시·입력 로직만 둔다.
4. 정상 경로뿐 아니라 미분류·충돌·파일 변경·중단·되돌리기 실패를 테스트한다. 규칙 형식을 바꾸면 Schema와 AI 작성 규격도 갱신한다.
5. 사용법이나 지원 범위가 달라지면 README와 관련 문서를 같은 변경에 포함한다.

목표는 계층을 늘리는 것 자체가 아니라, 기능 하나를 바꾸거나 시험할 때 다른 화면·저장소·파일 작업을 불필요하게 건드리지 않는 것이다.
