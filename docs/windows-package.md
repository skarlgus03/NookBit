# Windows 배포

[개발 환경 안내](contributing.md#빌드와-테스트)에 따라 Windows x64 개발자 터미널을 준비한다. 저장소 루트에서 실제 Qt 키트 경로를 지정한다.

```powershell
cmake --preset release -DCMAKE_PREFIX_PATH="C:/Qt/6.11.2/msvc2022_64"
cmake --build --preset archive-release
```

`out/package/NookBit-Release`에 실행 파일과 런타임이 모이고, `out/package`에 `NookBit-<버전>-Windows-x64.zip`과 `.zip.sha256`이 생성된다. 폴더만 갱신할 때는 `cmake --build --preset package-release`를 쓴다. ZIP은 임시 파일로 작성한 뒤 같은 이름의 로컬 ZIP을 교체한다.

앱 버전은 루트 `CMakeLists.txt`의 `project(VERSION …)`과 `NOOKBIT_PRERELEASE`로 정해진다. 안정 버전에는 `-DNOOKBIT_PRERELEASE=`를 지정한다. 한 버전으로 배포한 뒤 내용이 바뀌면 새 버전을 지정한다. 빌드 명령은 로컬 파일을 생성한다. GitHub Releases에는 확인한 ZIP과 체크섬을 직접 첨부한다.

## 배포 파일 검증

1. `Get-FileHash <archive.zip> -Algorithm SHA256` 결과가 `.zip.sha256`의 해시와 일치하는지 확인한다.
2. ZIP을 새 폴더에 풀어 `README.txt`, `version.json`, `LICENSE`, `licenses/NotoSansKR-OFL.txt`, `imagepacks`가 들어 있는지 확인한다. 패키지는 앱과 런타임을 포함한다. 작업 기록과 개인 설정은 앱 폴더 외부에 저장된다.
3. Qt SDK 설치 경로가 `PATH`와 Qt/QML 플러그인 경로에 없는 환경에서 `nookbit.exe --version`을 실행한다. 출력, `version.json`, Windows 파일 속성의 버전을 비교한다.
4. 압축을 푼 앱에서 메인 화면, 컴패니언 미리보기, 샘플 파일 이동과 작업 기록을 확인한다. 자동 UI 검사는 저장소 루트에서 `ctest --preset release -R nookbit_qml_smoke`로 실행한다. CTest는 배포 앱과 시작 코드 및 QML을 공유하는 `nookbit_ui_checks`를 사용한다.

## 라이선스 고지

Windows 패키지는 NookBit의 MIT 원문, Noto Sans KR의 OFL, `licenses/qt`의 Qt LGPLv3·GPLv3 원문과 제3자 고지를 포함한다. 고지에는 Qt PDF와 PDFium, OpenGL 소프트웨어 렌더러 Mesa 및 LLVM도 포함된다. 동봉한 SPDX 파일에서 빌드 버전과 구성 요소를, `Qt-source-and-runtime.txt`에서 대응 소스 위치와 DLL 교체 방법을 확인할 수 있다. 공개 릴리스에 ZIP을 첨부할 때 이 소스 링크도 접근 가능한지 확인한다. [Qt LGPL 배포 안내](https://www.qt.io/development/open-source-lgpl-obligations) · [Qt PDF 라이선스](https://doc.qt.io/qt-6.11/qtpdf-licensing.html)

현재 Windows 배포에 준비된 고지는 Qt 6.11.2 MSVC x64용이다. 배포용 Qt 버전을 바꿀 때는 같은 버전의 고지와 SPDX 자료를 갱신한다. 다른 Qt 버전의 개발 빌드에는 배포용 고지 디렉터리가 필요하지 않다.

## 런타임 구성

`NOOKBIT_COMPACT_PACKAGE`의 기본값은 `ON`이다. `windeployqt`가 의존성을 모은 뒤, 패키징 스크립트가 생성된 배포 폴더에서 사용하지 않는 QML 디버깅 플러그인, SQLite 이외의 SQL 드라이버, Basic 이외의 지정된 Quick Controls 스타일을 제거한다. Basic과 기본 대화상자, Qt Quick 렌더링, 이미지 플러그인, SQLite와 Windows 플랫폼 플러그인, VC 런타임 설치 파일은 유지한다.

전체 런타임이 필요하면 `cmake --preset release -DNOOKBIT_COMPACT_PACKAGE=OFF`로 구성하고 패키지를 다시 만든다. 컨트롤 스타일이나 DB 드라이버를 바꿀 때는 배포 제외 목록도 확인한다.

Windows 패키지에서 사용하지 않는 Qt Quick 3D 유틸리티와 선택적인 시스템 DXC DLL은 제외한다. Direct3D 11용 셰이더 컴파일러와 OpenGL 소프트웨어 렌더러는 포함한다. QtCore는 SDK에서 받은 DLL을 그대로 배포한다.
