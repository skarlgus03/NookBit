# NookBit

내가 정한 규칙으로 파일과 폴더를 정리하는 Windows 데스크톱 앱입니다. 이동할 위치를 미리 보고, 확인한 항목만 정리할 수 있습니다.

## 다운로드 및 실행

1. [GitHub Releases](https://github.com/skarlgus03/NookBit/releases)의 **Assets**에서 `NookBit-<버전>-Windows-x64.zip`을 받습니다.
2. ZIP 전체를 새 폴더에 풉니다. DLL과 런타임은 실행 파일과 함께 둡니다.
3. `NookBit-Release` 폴더에서 `nookbit.exe`를 실행합니다.

`Source code (zip)`과 `Source code (tar.gz)`만 보이면 해당 릴리스에 실행용 ZIP이 아직 첨부되지 않은 상태입니다. 업데이트할 때는 실행 중인 NookBit을 트레이에서 완전히 종료한 뒤 새 폴더의 실행 파일을 엽니다. 설정과 작업 기록은 앱 폴더 밖에 저장됩니다.

## 처음 정리하기

**파일 정리**에 파일이나 폴더를 끌어놓고 정리 위치를 고릅니다. **결과 미리보기**에서 이동 예정 경로를 확인한 뒤 **정리하기**를 실행합니다. 완료한 작업은 **작업 기록**에서 검색하고 되돌릴 수 있습니다.

- **분류 규칙** — 노드로 조건과 목적지를 연결합니다. 샘플로 규칙을 시험하거나 JSON을 가져오고 내보낼 수 있습니다. 외부 AI로 규칙을 작성하려면 [작성 규격](docs/ai-rule-authoring-v3.md)과 [예제 JSON](resources/rules/ai-example.v3.json)을 사용하세요. [분류 규칙 사용법](docs/user/rules-guide.ko.md)
- **폴더 감시** — 지정한 폴더가 설정한 조건을 충족하면 정리를 제안합니다. 이동은 사용자 확인 후 시작합니다.
- **컴패니언** — 바탕화면에 정리 제안과 작업 상태를 표시합니다. 캐릭터 팩을 교체하거나 직접 만들 수 있습니다. [캐릭터 팩 제작 가이드](docs/user/character-pack-guide.md)

폴더는 기본적으로 통째로 다룹니다. 내용물을 개별로 정리하거나 중단된 작업을 확인할 때는 아래 안내를 참고하세요. [폴더 처리 흐름](docs/folder-processing-flows.md) · [폴더 이동·되돌리기](docs/directory-bundles.md) · [중단 작업 복구](docs/interrupted-operation-recovery.md)

## 개발

- [Windows 개발 환경, 빌드와 테스트](docs/contributing.md)
- [아키텍처와 안전 원칙](docs/architecture.md) · [기능 모듈](docs/feature-modules.md)
- [Windows ZIP 생성과 배포 전 검증](docs/windows-package.md)

## 라이선스

NookBit이 제작한 코드, 문서, Nmoo 이미지와 앱 아이콘은 [MIT 라이선스](LICENSE)로 배포합니다. Qt, Qt PDF, Noto Sans KR 등 외부 구성 요소에는 각각의 라이선스가 적용됩니다. [번들 폰트 안내](resources/fonts/README.md)
