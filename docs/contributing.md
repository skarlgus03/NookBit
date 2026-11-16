# 팀원 작업 안내

이 문서는 NookBit에 처음 참여하는 사람을 위한 권장 작업 순서다.

## 시작 전

1. 아래 Windows 환경에서 앱을 빌드·실행하고 테스트를 돌린다.
2. [설계와 확장 원칙](architecture.md), 변경할 기능과 관련된 규칙 문서를 읽는다.
3. 코드는 `src/presentation/main.cpp`에서 객체 연결을, `src/presentation/qml/`에서 화면을, `include/nookbit/application/ports.hpp`에서 작업 계약을 따라가면 전체 흐름을 파악하기 쉽다.
4. 실제 개인 파일 대신 복사한 시험 파일과 임시 목적지를 사용한다. 앱이 이미 사용하는 사용자 데이터와 작업 기록은 실험용으로 지우거나 덮어쓰지 않는다.

## 빌드와 테스트

- Visual Studio 2022의 **Desktop development with C++** 워크로드와 MSVC x64 도구
- CMake 3.24 이상과 Ninja
- MSVC x64용 Qt 6.5 이상과 Core, Gui, Widgets, Qml, Quick, QuickControls2, QuickDialogs2, Sql 구성 요소
- PDF 본문 기반 폴더 추천을 쓰려면 같은 Qt 키트의 Qt PDF 구성 요소도 설치한다.

Visual Studio x64 개발자 터미널에서 저장소 루트로 이동하고, Qt 경로를 실제 설치 위치로 바꿔 실행한다.

```powershell
cmake --preset dev -DCMAKE_PREFIX_PATH="C:/Qt/6.11.2/msvc2022_64"
cmake --build --preset dev
ctest --preset dev
.\out\build\dev\src\nookbit.exe
```

Release ZIP을 만들고 확인하는 절차는 [Windows 배포](windows-package.md)를 참고한다.

## 브랜치와 변경 제안

`main`을 직접 수정하기보다 작업마다 브랜치를 만든다. 이름은 예를 들어 `feature/rule-search` 또는 `fix/preview-warning`처럼 목적을 드러내면 된다.

```powershell
git switch main
git pull --ff-only
git switch -c feature/your-task
```

한 브랜치에서는 한 가지 기능·수정에 집중하고, 중간중간 의미 있는 단위로 커밋한다. 검증 후 브랜치를 올리고 Pull Request에 **무엇을 바꿨는지, 어떻게 시험했는지, 남은 제한**을 적어 공유한다. 리뷰 결과가 반영되기 전에는 다른 사람의 미완성 작업이나 사용자 데이터를 임의로 정리하지 않는다.

## 구현·리뷰 기준

- 도메인·유스케이스는 Qt 화면 타입에 의존하지 않게 유지한다. 새 일꾼의 계약과 구현 위치를 구분하고, 컨트롤러 하나에 기능을 계속 몰아넣지 않는다.
- 함수 정의에는 “무엇을 하는 함수인지” 짧은 한국어 주석을 단다. 복잡한 분기에는 이유와 안전 조건도 적되, 코드 그대로를 반복하는 주석은 피한다.
- 규칙 순서·제외·미분류·목적지 경계와 승인 단계가 바뀌지 않았는지 확인한다. 안전 원칙을 완화하는 변경은 일반 기능 수정과 별도로 논의한다.
- 새 동작에는 자동화 테스트를 추가한다. 특히 파일 이동은 실제 사용자 파일이 아닌 테스트용 임시 디렉터리에서 검증한다.
- 생성된 `out/`, 로컬 Qt 경로와 `CMakeUserPresets.json`, 개인 앱 데이터의 JSON/SQLite, 비밀 정보는 커밋하지 않는다.
- UI의 최종 디자인은 아직 확정되지 않았다. 화면을 크게 바꾸는 작업은 범위와 사용 흐름을 먼저 합의한다.

UI 변경 시에는 직접 실행해서 사용자 흐름도 확인한다. `ctest`의 QML 스모크 테스트는 화면이 시작되는지 보는 검사이지, 모든 UI 동작을 대신하지 않는다. 문제가 생기면 사용한 Qt Kit, CMake 구성 명령, 실패한 테스트 이름과 재현 절차를 PR에 남긴다.

`main` 푸시와 `main` 대상 PR에서는 GitHub Actions의 **Windows CI**가 Release와 Debug를 빌드하고 각각 CTest를 실행한다. 두 빌드는 Qt와 Qt PDF 설치를 공유한다. 이후 실행에서는 같은 설치 구성의 캐시를 복원할 수 있다. 저장소의 **Actions → Windows CI**에서 빌드별 테스트 결과와 Qt 설치 단계의 캐시 로그를 확인할 수 있다.

실제 두 볼륨에서 파일을 옮기는 시나리오는 `nookbit_cross_volume_tests`로 별도 실행한다. 준비 방법은 [두 볼륨 테스트](cross-volume-test-run.md)를 참고한다.

MSVC + Ninja 구성은 `cmake/msvc_showincludes.ps1`로 컴파일러의 헤더 의존성 출력을 UTF-8과 고정 접두사로 통일한다. 헤더 변경 후 관련 객체 파일이 재빌드되지 않으면 아래 검사로 앱의 의존성이 등록됐는지 확인한다.

```powershell
ctest --preset dev -R nookbit_header_dependency_regression
```

개발 중 정리 목적지를 고정하려면 `cmake --preset dev -DNOOKBIT_DEV_DESTINATION_ROOT="D:/NookBitTest"`처럼 로컬 빌드에 지정하고 재빌드한다. 이 값은 사용자 기억값보다 우선하며 Git에 공유되지 않는다. 다시 사용자 설정을 사용하려면 같은 옵션에 빈 문자열을 지정한다. 실행 파일에 `--print-default-destination`을 붙이면 창을 열지 않고 실제 시작 기본 경로를 확인할 수 있다.
