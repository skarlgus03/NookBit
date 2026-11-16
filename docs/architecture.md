# NookBit 설계와 확장 원칙

NookBit은 파일과 폴더의 분류 결과를 미리 보여주고, 사용자가 확인한 이동만 실행하는 Windows 데스크톱 앱이다. 분류 규칙은 노드 편집기나 JSON으로 작성하고, 폴더 감시는 정리 후보를 제안한다.

## 파일 정리 흐름

```text
끌어놓기 또는 수락한 감시 제안
  → 대기 목록 → 스캔·규칙 평가 → 결과 미리보기
  → 이동 계획 고정·재검사 → 사용자 확인 → 승인 기록·이동
  → 작업 기록 → 되돌리기 또는 중단 복구
```

미리보기의 `previewOutcomes`에는 미분류와 제외를 포함한 판단 결과가 들어간다. 승인할 이동 대상은 `steps`에 기록한다. 정리 확인창을 열기 전에 `ISortWorkflow::prepare`가 계획을 고정하고, 사용자가 확인하면 `approveAndExecute`가 현재 파일·설정·규칙을 다시 검증한 뒤 실행한다. 규칙 편집기의 시험 기능은 편집 중인 초안을 평가하고, **전체 저장**은 앱이 사용할 활성 규칙을 갱신한다.

## 코드에서 따라갈 곳

| 역할 | 주요 위치 |
| --- | --- |
| 앱 시작, 공통 설정, QML과 트레이 연결 | [main.cpp](../src/presentation/main.cpp) |
| 모듈 생성·종료와 모듈 간 연결 | [FeatureRuntimeHost](../src/app/feature_runtime_host.cpp), [FeatureConnections](../src/app/feature_connections.cpp) |
| 파일 정보, 규칙, 이동 계획 자료형 | [도메인 모델](../include/nookbit/domain/model.hpp) |
| 작업 순서와 의존성 계약 | [유스케이스](../src/application/use_cases/), [포트](../include/nookbit/application/ports.hpp) |
| 규칙 평가, 경로 계획, 감시 조건, 캐릭터 행동 | [모듈 구현](../src/modules/) |
| 파일 시스템, JSON, SQLite, 해시 | [인프라 구현](../src/infrastructure/) |
| 화면 상태와 작업 요청 | [컨트롤러](../src/presentation/), [QML](../src/presentation/qml/) |

유스케이스는 입력과 결과를 값으로 다루며 포트를 통해 스캔, 규칙 평가, 저장, 실행을 요청한다. 앱 조립부가 포트에 구체 구현을 연결한다. QML은 화면과 입력을 담당하고 컨트롤러가 유스케이스 호출과 결과 반영을 맡는다. 공통 글꼴과 화면 토큰, 페이지별 상태 소유 범위는 [QML 구조](qml-structure.md)와 [UI 규격](ui-metrics.md)에 정리한다.

`FileOrganizerRuntime`이 소유한 [BackgroundTaskRunner](../src/presentation/background_task_runner.hpp)는 계획 고정·승인·이동·되돌리기·전체 중단 복구를 작업 스레드에서 한 번에 하나씩 실행한다. [ThreadLocalOperations](../src/app/thread_local_operations.hpp)에서 작업 스레드용 SQLite 연결과 서비스를 조립하고 같은 스레드에서 해제한다. 컨트롤러는 결과를 받아 화면 스레드에서 모델을 갱신한다.

## 기능 모듈과 통신

앱은 시작 시 저장된 모듈 선택과 의존 관계를 해석하고, `FeatureRuntimeHost`가 그 결과에 따라 활성 모듈을 생성한다. 모듈 관리에서 저장한 선택은 다음 앱 시작에 적용된다. 폴더 감시는 파일 정리의 분류 조회와 대기 목록 등록 포트를 이용한다. 컴패니언 먹이 주기는 컴패니언의 상호작용 등록 기능을 이용한다.

`FeatureConnections`가 감시 제안을 알림으로, 표시가 허용된 알림과 파일 작업 상태를 컴패니언에 전달한다. 파일 작업 결과의 일시 표현은 별도 반응 채널로 전달한다. 각 모듈은 자기 UI와 튜토리얼 기여를 `UiPageRegistry`에 등록하고 앱은 이 등록 정보로 메뉴와 안내를 구성한다. 수명과 설정 소유 범위는 [기능 모듈](feature-modules.md), [컴패니언 행동](companion-behavior-design.md), [튜토리얼](tutorial.md)을 참고한다.

## 규칙 모델

- **기본 분류.** v3 규칙은 파일·폴더 종류, 확장자, 이름, 원본 폴더, 크기, 수정 경과일 등의 조건을 평가한다. 트리의 부모·자식 조건과 AND·OR·NOT 논리식을 조합할 수 있다. 같은 레벨에서는 첫 일치 분기로 진행한다. 해당 분기에 결과가 없더라도 다음 형제로 돌아가지 않는다. 해당 레벨의 `otherwise`와 전체 기본 목적지는 각각의 범위에서 적용된다.
- **폴더 처리.** 기본값은 폴더를 하나의 대상으로 옮기는 방식이다. 사용자가 이름 있는 흐름을 선택하면 순회, 별도 흐름 호출, 조건, 목적지 등을 조합할 수 있다. 미리보기에서 확정한 입력 경로와 순회 지점은 승인 검증에 포함한다.
- **목적지.** 상대 경로는 사용자가 지정한 정리 루트에서 계산한다. 정리 루트 밖의 절대 경로는 사용자가 별도로 허용한 위치 안에서만 사용할 수 있다.
- **문서 분석.** 미리보기의 TF-IDF 문서 추천은 기존 문서의 내용으로 후보를 표시한다. [폴더 추천 어댑터](../src/infrastructure/filesystem/folder_suggestions.cpp)가 로컬 문서를 읽고, [TF-IDF 코어](../src/modules/document_recommendation/tfidf.cpp)는 본문에서 센 글자 조각의 빈도로 유사도를 계산한다. 이동 계획은 사용자가 확인하는 규칙 평가 결과로 유지된다. 규칙 편집기의 문서 분석 노드는 편집 초안으로만 제공하며, 적용 시 검증에서 차단한다.

자세한 규칙은 [v3 분류 트리](rule-tree-v3.md), [폴더 처리 흐름](folder-processing-flows.md), [JSON 스키마](../schemas/rules-tree-v3.schema.json), [AI 작성 규격](ai-rule-authoring-v3.md)을 따른다.

## 파일 작업의 안전과 저장 데이터

- [SafeExecutor](../src/infrastructure/filesystem/safe_executor.cpp)는 승인한 계획을 기준으로 덮어쓰기 없는 이동을 실행하고 단계별 결과를 기록한다. 이미 목적지에 있는 항목은 이동 계획에서 제외한다. 되돌리기는 작업 기록의 원본 경로를 사용하며, 상태나 기록을 확정할 수 없으면 복구가 필요한 상태로 남긴다.
- 모든 단계가 같은 볼륨 내 이동인 일부 새 계획에는 내용 해시 대신 파일 ID로 검증하는 [MetadataRename](same-volume-fast-move.md)을 쓴다. 다른 볼륨의 복사는 내용 검증과 [다운로드 표식 보존](download-marker-copy.md)을 적용한다. 취소는 확정 전 지원되는 복사 구간에 한해 가능하다. 복구 범위는 [중단 작업 복구](interrupted-operation-recovery.md)에서 확인한다.
- `QStandardPaths::AppDataLocation` 아래에 활성 규칙 `rules.json`, 그래프 배치 `rule-graph-layout.json`, 대기 경로 `pending-files.json`, 감시 설정 `watch.json`, 승인·실행 기록 `history.sqlite`를 저장한다. 분류 규칙의 파일 메뉴에서 저장하는 편집 초안은 `rule-graph-draft.json`에 한 개를 보관한다. 모듈 선택과 화면 설정은 `QSettings`에 저장한다. 대기 목록은 절대 경로만 원자적으로 저장하고, 복원이 실패하면 기존 파일을 보존한다. 앱을 다시 실행한 뒤에는 새로 미리보기와 확인을 거친다.

## 기능을 추가할 때

1. 사용자 입력과 데이터 소유자를 먼저 정한다. 파일을 변경하는 동작은 미리보기·승인·실행 경로를 따른다.
2. 코어가 필요한 경우 기존 요청 자료형과 포트를 재사용한다. 구체 구현과 모듈 간 연결은 앱 조립부에 둔다.
3. 규칙 의미가 바뀌면 스키마, JSON 입출력, AI 작성 규격과 테스트를 함께 갱신한다. 파일 작업은 정상 이동 외에도 충돌, 파일 변경, 취소, 중단, 되돌리기를 확인한다.
4. [협업 및 테스트 안내](contributing.md)에 따라 CTest를 실행한다. UI 자가 검사는 배포용 앱과 같은 시작 코드 및 QML을 쓰는 `nookbit_ui_checks`에서 실행한다. 파일 작업의 검사 방법과 범위는 [백엔드 안전 점검](backend-safety-checks.md)을 참고한다.
