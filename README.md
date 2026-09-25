# NookBit

끌어놓은 파일을 대기 목록에서 정리하고, 감시 폴더는 조건에 맞을 때 정리를 제안하는 Windows 데스크톱 정리 도우미의 초기 골격이다. 실제 이동은 사용자가 계획을 승인한 뒤에만 실행한다.

현재 버전은 2026-10-16 데모를 향한 기반 단계다. 앱 화면에서 파일 하나의 계획을 저장하고 명시적으로 승인한 뒤 이동·되돌릴 수 있다.

## 현재 구현

- C++20 도메인 모델
- Scanner, RuleEngine, Planner, Executor, History, Settings, RuleSet 포트
- 한 곳에서 구현체를 연결하는 `CompositionRoot`
- 설정·분류 규칙 로드 → 대기 목록의 선택 파일 검사 → 계획 생성을 조율하는 읽기 전용 미리보기 유스케이스
- 변경 불가능한 안전 불변조건
- 경로 범위 검증
- 충돌 정책 `rename`, `skip`, `abort`
- SQLite 초기 스키마와 WAL 설정
- 버전이 있는 settings/rules JSON Schema와 예제
- Qt를 이용한 설정·분류 규칙 JSON 읽기 및 Schema 검증 (현재 코어 모델에 필요한 값만 추출)
- 확장자·파일명 패턴·크기·수정 후 경과 일수로 판단하는 RuleEngine (우선순위와 다중 일치 정책 적용)
- 선택 파일 또는 감시 폴더를 별도 입력으로 읽는 FilesystemScanner (재분석 지점 회피, 파일 크기·수정 시각·Windows 파일 ID 수집)
- Scanner → RuleEngine → Planner를 연결한 읽기 전용 이동 초안 (안전 경로·충돌 정책 검증, 파일 변경 없음)
- 드래그 앤 드롭으로 로컬 파일 경로만 담고 목록에서 뺄 수 있는 QML 대기 화면 (실제 파일 변경 없음)
- 대기 목록을 기본 JSON 규칙에 연결해 예상 목적지와 경고를 표시하는 읽기 전용 미리보기 화면
- 미리보기 뒤 현재 파일·설정·규칙을 재검사하고 SHA-256 계획 해시와 함께 SQLite에 고정 저장하는 흐름
- 저장 계획을 SQLite에서 다시 읽어 해시·현재 파일 정보·설정·규칙과 대조하는 읽기 전용 재검사
- 승인 기록·실행 상태 이벤트를 SQLite 트랜잭션으로 저장하는 백엔드
- 승인된 단일 파일 계획을 기록 후 덮어쓰기 없이 이동하고, 원본 위치가 비었을 때 되돌리는 실행기와 화면 버튼
- 앱 시작 시 중단된 단일 파일 작업의 파일 ID·크기·수정 시각을 대조해 이력만 정리하는 복구기 (판단 불가 시 `NeedsRecovery`로 표시, 파일 이동 없음)
- 외부 테스트 라이브러리가 필요 없는 코어 안전 테스트

## 아직 구현하지 않은 부분

- 설정 GUI 편집·저장
- 감시 조건 충족 시 제안 알림
- 여러 파일을 한 계획으로 실행·부분 실패 처리
- 판단 불가 상태를 사용자가 해결하는 복구 화면
- 사용자 규칙 편집 화면

이 항목들은 인터페이스가 고정된 뒤 세로 흐름 단위로 추가한다.

현재 설정 저장은 지원하지 않는다. 코어 모델에 없는 JSON 항목이 사라지지 않도록 저장 요청은 오류로 반환한다. 대기 화면은 현재 실행 중에만 경로를 기억한다. 화면의 목적지 입력은 이번 미리보기에만 사용하며 기본 JSON 규칙은 앱에 포함된다. 미리보기는 `Draft` 초안이고, `계획 고정·저장`을 누르면 현재 파일 정보·설정·규칙을 다시 검사한 뒤 `Planned`로 SQLite에 저장한다. DB는 앱 데이터 폴더의 `history.sqlite`이다. `저장 계획 검사`는 DB의 계획을 다시 읽고 해시와 현재 상태를 비교하지만, 검사 결과는 그 순간에만 유효하다. 파일 하나가 이동 예정일 때 `파일 하나 승인`을 누르면 저장 계획과 현재 파일 상태를 다시 검사해 승인 기록을 남긴다. `승인한 파일 이동`을 눌러야 실제 이동이 시작되고, 실행기는 그 직전 다시 검사한다. 성공하면 `마지막 이동 되돌리기`가 활성화된다. 이 버튼은 같은 앱 실행 중 마지막으로 이동한 파일에만 제공된다. Executor는 승인된 계획을 다시 검사하고 DB에 이동 의도를 남긴 뒤 Windows의 덮어쓰기 없는 같은 볼륨 이동을 호출한다. Undo는 대상 파일의 ID·크기·수정 시각과 원본 경로의 비어 있음을 검사한 뒤 되돌린다. 앱을 다시 시작하면 중단된 단일 파일 작업의 원본·목적지 상태를 확인해 확실한 경우에만 DB 기록을 정리하고, 애매하면 `NeedsRecovery`로 남기고 화면에 알린다. 복구 중에는 파일을 자동으로 이동하거나 삭제하지 않는다. 파일 내용 SHA-256 비교와 수동 복구 화면은 아직 없으며, Undo 후 생성된 빈 목적지 폴더는 남을 수 있다. 감시 스캔은 후보를 찾기 위한 입력일 뿐 자동 이동을 뜻하지 않는다.

## 디렉터리

```text
include/nookbit/domain/             공개 핵심 값과 안전 불변조건
include/nookbit/application/        공개 모듈 포트와 도메인 이벤트
src/app/                            CompositionRoot
src/modules/planner/                경로·충돌 안전 정책
src/infrastructure/                 SQLite 등 외부 구현
src/presentation/                   Qt/QML 셸
schemas/                            설정과 규칙 JSON Schema
resources/                          기본 설정과 규칙 예제
tests/                              코어 테스트
```

## Windows 빌드

### 권장 환경

- Visual Studio 2022의 Desktop development with C++
- CMake 3.24 이상
- Ninja
- Qt 6.5 이상: Qt/QML 앱을 빌드할 때만 필요

Qt가 없으면 `nookbit_core`와 `nookbit_core_tests`만 빌드된다.

### Developer PowerShell

```powershell
Set-Location 'D:\NookBit'
cmake --preset dev -DCMAKE_PREFIX_PATH=C:/Qt/6.11.2/msvc2022_64
cmake --build --preset dev
ctest --preset dev
```

### Visual Studio에 포함된 CMake와 Ninja를 직접 사용할 때

Developer PowerShell for VS 2022를 먼저 연 뒤 다음과 같이 실행한다.

```powershell
Set-Location 'D:\NookBit'
$cmake = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$env:Path = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;' + $env:Path
& $cmake --preset dev
& $cmake --build --preset dev
& $cmake --build out/build/dev --target test
```

## 안전 정책

다음은 설정으로 해제할 수 없다.

- 승인 없는 실행 금지
- 영구 삭제 금지
- 조용한 덮어쓰기 금지
- reparse point 추적 금지
- 로그 없는 파일 작업 금지

사용자 자유도는 규칙, 알림, 감시 기준, `rename/skip/abort` 같은 정책에 제공한다. 복구 가능성을 없애는 선택지는 제공하지 않는다.

## 다음 구현 순서

1. 파일 내용 해시와 판단 불가 작업의 수동 복구 흐름
2. 여러 파일의 순차 실행 및 부분 실패 시 되돌리기
3. 이전 실행 기록에서 되돌릴 작업을 다시 선택하는 화면
4. 감시 조건 평가와 정리 제안 알림 (자동 이동 금지)
