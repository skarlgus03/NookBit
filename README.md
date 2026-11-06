# NookBit

NookBit은 **파일과 폴더를 내가 정한 규칙으로 정리하는 Windows 데스크톱 앱**입니다. 파일을 대기 목록에 넣고 목적지를 미리 본 뒤, 확인한 항목을 이동할 수 있습니다.

## 다운로드 및 실행

1. [GitHub Releases](https://github.com/skarlgus03/NookBit/releases)의 **Assets**에서 `NookBit-<버전>-Windows-x64.zip`을 받습니다.
2. ZIP 전체를 새 폴더에 풉니다.
3. 압축을 푼 `NookBit-Release` 폴더에서 `nookbit.exe`를 실행합니다.

`Source code (zip)`과 `Source code (tar.gz)`만 보이면 해당 릴리스에 실행용 패키지가 아직 첨부되지 않은 상태입니다. `.zip.sha256`은 다운로드 검증용이며, 실행에는 DLL과 런타임을 포함한 배포 폴더 전체가 필요합니다.

업데이트할 때는 새 ZIP을 별도 폴더에 풀고, 기존 앱을 트레이 메뉴에서 완전히 종료한 뒤 새 버전을 실행합니다. 사용자 설정과 작업 기록은 앱 폴더 밖에 저장됩니다.

## 주요 기능

| 기능 | 할 수 있는 일 |
| --- | --- |
| 파일 정리 | 파일과 폴더를 끌어놓고, 이동 예정 경로를 확인한 뒤 정리 |
| 분류 규칙 | 노드 편집기에서 조건과 목적지를 연결하고, 샘플 파일로 테스트하거나 JSON을 가져오기·내보내기 |
| 작업 기록 | 이전 이동 내역 검색 및 되돌리기 |
| 폴더 감시 | 지정한 조건을 충족하면 정리를 제안. 이동은 사용자 확인 후 시작 |
| 컴패니언 | 바탕화면에서 정리 제안과 진행 상태를 표시하고, 캐릭터 팩을 교체 |

폴더는 기본적으로 통째로 다룹니다. 내용물을 따로 정리하려면 [폴더 처리 흐름](docs/folder-processing-flows.md)을 사용할 수 있습니다. 기능은 **설정 → 모듈 관리**에서 선택하고 앱을 완전히 종료한 뒤 다시 실행해 적용합니다.

## 사용 가이드

- [분류 규칙 사용법](docs/user/rules-guide.ko.md)
- [외부 AI에 전달할 규칙 작성 규격](docs/ai-rule-authoring-v3.md) · [예제 JSON](resources/rules/ai-example.v3.json)
- [컴패니언 팩 제작 가이드](docs/user/character-pack-guide.md)
- [폴더 이동·되돌리기 지원 범위](docs/directory-bundles.md) · [중단 작업 복구](docs/interrupted-operation-recovery.md)

NookBit은 승인된 경로로만 이동하고 기존 파일을 덮어쓰지 않습니다. 되돌릴 때는 원래 위치를 확인하며, 중단 후 상태가 불확실한 작업은 검토 대상으로 남깁니다.

## 개발 및 빌드

C++20과 Qt/QML을 사용합니다. Windows에서 Visual Studio 2022의 **Desktop development with C++**(MSVC x64), CMake 3.24 이상, Ninja, MSVC x64용 Qt 6.5 이상이 필요합니다. Qt 구성 요소는 Core, Gui, Widgets, Qml, Quick, QuickControls2, QuickDialogs2, Sql입니다.

x64 개발자 터미널에서 아래 Qt 경로를 자신의 설치 위치로 바꿔 실행합니다.

```powershell
cmake --preset dev -DCMAKE_PREFIX_PATH="C:/Qt/6.11.2/msvc2022_64"
cmake --build --preset dev
ctest --preset dev
.\out\build\dev\src\nookbit.exe
```

배포 ZIP을 만들려면 `cmake --preset release`에 같은 Qt 경로를 지정한 뒤 `cmake --build --preset archive-release`를 실행합니다. 배포·검증 절차는 [Windows 패키징 문서](docs/windows-package.md)를 참고하세요.

- [설계와 안전 원칙](docs/architecture.md) · [기능 모듈 구조](docs/feature-modules.md)
- [협업 및 테스트 안내](docs/contributing.md)
