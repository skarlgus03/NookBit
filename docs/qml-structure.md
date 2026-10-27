# QML 화면 구조

`src/presentation/qml/Main.qml`은 창 구성과 기능 연결을 맡는다. 각 페이지의
입력 필드·선택 상태·목록 표시·보조 창은 해당 페이지가 소유한다. 화면 디자인,
페이지 번호, 기존 승인·파일 이동·복구 계약은 이 구조 정리에서 변경하지 않았다.

## 역할과 상태의 소유자

| 파일 | 담당하는 내용 |
| --- | --- |
| `Main.qml` | 메인·확장 창, 페이지 선택, 창 크기 복원, Controller 전달, 기능 간 요청 연결 |
| `PageSurface.qml` | 페이지 공통 바탕, 열기·닫기 요청. 페이지 번호와 다른 화면은 모른다. |
| `AppSettingsPage.qml` | 앱 설정 표시, 다른 설정 화면으로 이동 요청 |
| `CollisionSettingsPage.qml` | 파일 이름 충돌 처리 설정 |
| `WatchSettingsPage.qml` | 감시 조건 입력 초안, 저장·검사 요청 |
| `HistoryPage.qml` | 기록 표시, 작업 선택, 되돌리기 확인창 |
| `RecoveryPage.qml` | 중단 작업 목록과 복구 승인 확인창 |
| `CompanionPage.qml` | 팩 선택·표시 설정, 미리보기 연결, 가져오기·가이드·개인 보정 창 |
| `FileSortPage.qml` | 대기 목록과 미리보기·확인·진행·결과 화면의 전환 상태 |
| `RuleEditorPage.qml` | 규칙 노드 편집 상태, 캔버스와 도구 연결 |
| `DeveloperToolsPage.qml` | 기존 개발용 정리 진단 화면과 테스트 요청 |
| `LegacyRuleEditorDialog.qml` | 개발용 예전 규칙 폼의 선택·접힘·미저장 입력 |
| `WatchProposalDialog.qml` | 감시 제안의 수락·미루기 |
| `SortConfirmationDialog.qml` | 인라인 파일 정리 이외 경로의 기존 최종 승인 |
| `OperationProgressPopup.qml` | 전달받은 작업 진행 정보 표시 |
| `NavigationButton.qml` | 페이지 이동 버튼의 공통 규격과 피드백 |

## 연결 원칙

- 페이지는 필요한 Controller·데이터를 `required property`로 받는다. 앱 컨텍스트의
  `previewController`, `watchController` 같은 이름이나 Main의 `root`·`extensionHost`를
  직접 참조하지 않는다. Controller는 빌려 사용하며 새로 생성하거나 소유하지 않는다.
- 페이지 이동과 다른 기능의 테스트는 `signal`로 요청한다. Main이 이 요청을 받아
  페이지 번호를 바꾸거나 해당 기능의 창구에 전달한다.
- `PageSurface.open()`·`close()`는 요청을 발생시키는 함수다. 실제 표시 여부는
  Main의 페이지 선택에 바인딩한다. 페이지가 다른 페이지를 직접 닫지 않는다.
- 선택·줌·드래그·미저장 입력처럼 화면에 속한 상태는 QML에서 관리한다.
  분류·검증·저장·이동·복구 판단은 기존 C++ Controller와 유스케이스가 담당한다.
- 페이지는 전환 때 제거하지 않는다. 규칙·컴패니언 편집 초안과 스크롤 상태를
  유지한다. 감시 설정은 열 때 저장된 값으로 입력 초안을 다시 채우는 기존 동작을 유지한다.
- History의 되돌리기 확인창과 Recovery의 실행 확인창은 페이지가 소유하지만,
  전달받은 확장 창의 `overlayHost` 위에 표시한다. 해당 페이지를 떠나면 닫는다.
  Recovery는 남아 있는 승인 대기도 취소하며 파일 작업을 자동 승인하지 않는다.
- 컴패니언 페이지를 떠날 때 가져오기·가이드 저장·개인 보정 창만 닫는다.
  바탕화면 컴패니언과 미리보기의 편집 데이터는 별개다.
- 개발용 기존 화면과 규칙 폼은 삭제하지 않고 별도 파일에 보존했다.
  그래프 저장·불러오기로 초안이 교체되면 연결부가 예전 폼의 입력 상태만 초기화한다.

## 검증

- `nookbit_qml_smoke`: 실제 앱 QML에서 창 크기 복원, 각 페이지 열기·닫기,
  다크모드, 규칙 편집, 파일 정리 전환, 진행 표시·승인창의 소속을 확인한다.
- `nookbit_qml_page_boundaries_tests`: 앱 전역 Controller를 등록하지 않은 별도
  엔진에서 페이지를 생성한다. 두 감시 폼의 초안 분리, 재진입 시 복원,
  기록·복구 페이지 이탈 시 확인창 닫기와 실행 미발생을 검사한다.
- `nookbit_ui_metrics_contract`: 공통 토큰 사용과 함께 페이지에서 Main 및
  앱 전역 Controller를 직접 참조하는 회귀를 검사한다.

## 다음 구조 정리 범위

이번 변경은 Main에 섞여 있던 페이지와 상태를 분리한 단계다.
`RuleEditorPage.qml` 안에는 아직 그래프 편집 상태·캔버스 입력·노드 표시·도구·설정·
테스트 패널이 모여 있다. 다음에는 이 내부 역할을 나누되, 파일 분리만을 위해
상태를 여러 곳에 복제하거나 조작을 모두 C++로 옮기지 않는다.
