# NookBit

NookBit은 사용자가 끌어놓은 파일을 규칙에 따라 정리하는 Windows 데스크톱 앱이다. 파일을 대기 목록에 넣고, 분류 결과를 미리 본 뒤, 계획을 저장·승인해야 실제 이동이 시작된다. 폴더 감시는 조건이 맞을 때 정리를 **제안**하며 자동으로 파일을 옮기지 않는다.

현재 저장소에는 동작하는 Qt/QML 앱, C++20 코어, 규칙 편집기, 안전한 이동·되돌리기와 테스트가 있다. 화면 디자인과 일부 설정 기능은 아직 발전 중이다. 팀원은 이 문서로 실행한 뒤 [설계와 확장 원칙](docs/architecture.md), [협업 안내](docs/contributing.md)를 읽고 작업하면 된다.

## 처음 실행하기 (Windows)

필요한 것:

- Visual Studio 2022의 **Desktop development with C++** (MSVC x64)
- CMake 3.24 이상, Ninja
- **MSVC 2022 x64용** Qt 6.5 이상: Core, Gui, Qml, Quick, QuickDialogs2, Sql (Qt Quick 앱 빌드용)

Qt의 컴파일러 종류와 Visual Studio 컴파일러가 같아야 한다. 아래 경로는 설치 예시이므로 자신의 Qt 설치 경로로 바꾼다. Visual Studio Developer PowerShell 또는 x64 개발자 명령 환경에서 저장소 루트로 이동해 실행한다.

```powershell
cmake --preset dev -DCMAKE_PREFIX_PATH="C:/Qt/6.11.2/msvc2022_64"
cmake --build --preset dev
ctest --preset dev
.\out\build\dev\src\nookbit.exe
```

`dev` 프리셋은 Debug 빌드·테스트를 `out/build/dev`에 만든다. Qt를 찾지 못하면 C++ 코어만 빌드되고 `nookbit.exe`는 생성되지 않는다. 그때는 CMake 구성 메시지와 `CMAKE_PREFIX_PATH`를 확인한다. 실행 시 Qt DLL이 없다는 오류가 나면 먼저 위 명령으로 **재빌드**한다. Windows에서는 빌드 후 `windeployqt`가 발견되면 실행 파일 옆에 Qt 런타임과 QML 모듈을 배치한다. 발견되지 않으면 Qt 설치의 `bin` 디렉터리를 PATH에 추가하거나 `windeployqt`를 실행해야 한다.

Qt Creator에서는 저장소 루트의 `CMakeLists.txt`를 열고 MSVC x64 Qt Kit를 선택한 뒤 `nookbit`을 빌드·실행할 수 있다. Visual Studio에서도 **폴더 열기**로 CMake 프로젝트를 열 수 있으며, 같은 Qt 경로를 CMake 구성에 지정해야 한다. 일반적인 `.sln` 프로젝트가 아니라 CMake 프로젝트다.

## 앱에서 해볼 수 있는 흐름

1. 파일을 대기 목록으로 끌어놓는다. 이 시점에는 원본 파일이 그대로 있다.
2. 규칙 편집 페이지에서 노드를 구성하거나 v3 JSON을 불러온다. 실제 파일 하나 또는 가상 파일 정보로 규칙을 시험할 수 있다. 시험은 파일을 이동하지 않는다.
3. 편집을 적용하려면 **전체 저장**을 누른다. **JSON 내보내기**는 현재 그래프를 별도 파일로 저장할 뿐 활성 규칙을 바꾸지 않는다. 불러온 내용도 저장 전까지는 편집 중인 초안이다.
4. 대기 파일의 미리보기를 만들고, **계획 고정·저장 → 저장 계획 검사 → 파일 이동 승인 → 승인한 파일들 이동** 순서로 진행한다. 이동 전 파일과 계획을 다시 검사한다.
5. 이동한 파일은 마지막 이동 되돌리기 또는 작업 기록에서 되돌릴 수 있다. 모호한 중단 상태는 복구 화면에서 확인한다.

폴더 감시는 앱이 켜져 있을 때 지정 폴더를 검사한다. 제안을 수락하면 후보가 대기 목록에 **등록만** 된다. 이후에도 위의 미리보기·승인 과정이 필요하다. 테스트할 때는 복사해 둔 임시 파일과 임시 목적지를 사용한다.

## 프로젝트 지도

| 위치 | 역할 |
| --- | --- |
| `include/nookbit/domain/` | 파일·규칙·계획 등 핵심 자료형과 안전 불변조건 |
| `include/nookbit/application/`, `src/application/` | 포트(계약)와 유스케이스(큰 작업의 조율) |
| `src/modules/` | 규칙 판단, 이동 계획, 감시 조건 평가 |
| `src/infrastructure/` | 파일시스템, 안전 실행, JSON, SQLite 등의 구체 구현 |
| `src/presentation/` | Qt 컨트롤러와 QML 화면, 앱 시작 시 객체 연결 |
| `schemas/`, `resources/` | JSON Schema, 기본 규칙, 앱 내장 리소스 |
| `tests/` | 코어·규칙·컨트롤러·이동 안전성·QML 시작 검사 |

화면과 코어의 역할, 현재 코드에서 원칙이 아직 완전히 적용되지 않은 지점까지 [설계 문서](docs/architecture.md)에 적었다. 기능을 붙일 때는 이를 참고하되 현재 구조를 이상화해서 가정하지 말자.

## 데이터와 규칙

앱은 운영체제가 지정한 사용자 앱 데이터 위치(`QStandardPaths::AppDataLocation`)에 활성 규칙 `rules.json`, 그래프 배치 `rule-graph-layout.json`, 감시 설정 `watch.json`, 작업 기록 `history.sqlite`를 저장한다. 첫 실행에 활성 규칙이 없으면 내장된 [기본 v3 규칙](resources/rules/default.v3.json)을 복사한다. 기본 정리 목적지는 사용자의 문서 폴더 아래 `NookBit Organized`다. 대기 목록은 현재 실행 중에만 유지된다.

v3 분류 규칙은 **노드 하나에 조건 한 종류**를 둔다. 자식은 조건을 이어 붙이고, 형제는 배열 순서대로 검사해 첫 일치 가지를 선택한다. 선택한 가지에서 더 내려갈 수 없어도 이전 형제로 돌아가지는 않는다. 어느 가지도 최종 목적지에 도달하지 못하면 전체 예외가 없는 한 미분류로 남는다. 명시적 제외는 이동하지 않는 별도 결과다. 목적지는 정리 루트에 대한 상대 경로다.

- [v3 규칙의 동작과 v2 호환성](docs/rule-tree-v3.md)
- [AI에 전달할 영어 JSON 작성 규격](docs/ai-rule-authoring-v3.md) · [검증용 예제](resources/rules/ai-example.v3.json)
- [v3 JSON Schema](schemas/rules-tree-v3.schema.json)

외부 JSON을 불러올 때는 앱에서 검증한다. AI가 만든 규칙도 바로 실제 파일에 적용하지 말고 편집기의 규칙 시험과 미리보기를 거친다.

## 변경하면 안 되는 안전 원칙

- 사용자의 명시적 승인 없이 파일을 이동하지 않는다. 감시 결과도 자동 실행하지 않는다.
- 영구 삭제·조용한 덮어쓰기·로그 없는 파일 작업을 허용하지 않고, 재분석 지점을 따라가지 않는다.
- 경로 범위, 파일 상태와 내용 해시, 충돌 정책을 이동과 되돌리기 전 다시 확인한다.
- 상태가 불확실하면 추측해 복구하지 않고 `NeedsRecovery`로 남긴다.

상세한 변경 규칙과 테스트 기준은 [설계 문서](docs/architecture.md)와 [협업 안내](docs/contributing.md)에 있다.

## 현재 남은 부분

설정 화면의 일반 설정 저장, 앱 종료 중 알림, 여러 감시 폴더의 동시 관리, 오래된 해시 없는 복구 기록을 처리하는 수동 절차는 아직 구현되지 않았다. 캐릭터 및 최종 화면 디자인도 작업 대상이다. 기존 v2 규칙은 읽을 수 있으나 편집은 명시적 변환이 필요하다.
