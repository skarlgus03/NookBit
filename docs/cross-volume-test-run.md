# Real two-volume backend test

## 2026-10-04 실행 결과

실제 서로 다른 고정 NTFS 볼륨 C:와 D:에서 6개 시나리오가 모두 통과했다. 강제로 VerifiedCopy를 지정한 대역이 아니라 실제 볼륨 GUID 조회로 승인 계획 v2를 만들고 서명·저장·승인한 뒤 Windows 전송 포트를 사용했다. 일반 C:→D: 이동과 D:→C: 되돌리기에서 전체 SHA-256·객체 지문·단계/계획 완료 기록을 확인했다.

| 시나리오 | 결과 |
| --- | --- |
| 정상 이동 → 원래 위치로 되돌리기 | PASS |
| 목적지 기존 파일 충돌, 기존 파일과 원본 보존 | PASS |
| 1MiB 복사 후 취소 → 이어쓰기/전체 검증 → 확정/원본 제거 → 되돌리기 | PASS |
| 최종 이름 변경 후 결과 기록 실패 → 새 DB 연결로 확정 기록 복구 → 마무리 → 되돌리기 | PASS |
| 원본 제거 후 결과 기록 실패 → 새 DB 연결로 완료 기록 복구 → 되돌리기 | PASS |
| 되돌릴 위치에 기존 파일 충돌, 두 파일 모두 보존 | PASS |

테스트 파일은 4MiB+37바이트(4,194,341바이트)의 결정적 이진 데이터다. 복원된 4개 파일을 별도 PowerShell SHA-256 조회로 다시 확인했으며 모두 `0f3d42aa1953c5eec4f644beb02e835ff574ad51ea26b8afbc0d0ea68bc3c852`였다. 없는 다단계 목적지 폴더와 한글 파일 이름도 사용했다. 인자 없이 실행하면 파일을 만들지 않는 안내만 출력하며, 같은 볼륨을 두 부모로 지정하면 폴더 생성 전에 거부하는 것도 확인했다.

이 PC의 실행 증거는 다음 위치에 남겨두었다. 재실행하면 별도의 고유 폴더를 만든다. 이 경로는 저장소에 의존하는 설정이나 기본 사용자 경로가 아니다.

- 원본/복원 자료: `C:\Users\begon\AppData\Local\Temp\NookBit-cross-source-ppkzTw`
- 목적지/시나리오별 SQLite/JSON 보고서: `D:\NookBit\out\build\dev\tests\NookBit-cross-target-spNnfH`
- 보고서: 위 목적지 폴더의 `report.json`, UTC `2026-10-04T13:36:41.735Z`

## 재실행 방법

`tests/cross_volume_tests.cpp`는 Windows 전용 수동 실행 테스트다. 평소 CTest 실행이 다른 드라이브를 변경하지 않도록 자동 테스트 목록에는 등록하지 않았다. `nookbit_cross_volume_tests` 빌드 대상을 먼저 빌드한다.

PowerShell에서 Qt DLL 경로를 추가하고, **서로 다른 로컬 고정 NTFS 볼륨의 기존 디렉터리** 두 개를 명시한다. Visual Studio 개발 환경에서 CMake 빌드를 실행해야 한다.

```powershell
& 'C:\Qt\Tools\CMake_64\bin\cmake.exe' --build out/build/dev --target nookbit_cross_volume_tests
$env:PATH = 'C:\Qt\6.11.2\msvc2022_64\bin;' + $env:PATH
& '.\out\build\dev\tests\nookbit_cross_volume_tests.exe' --run `
    --source-parent 'C:\Users\begon\AppData\Local\Temp' `
    --destination-parent 'D:\NookBit\out\build\dev\tests'
```

Qt 버전과 부모 경로는 각 PC에 맞게 바꾼다. 부모 디렉터리 자체를 이동/삭제하지 않으며, 그 안에 `NookBit-cross-source-XXXXXX`와 `NookBit-cross-target-XXXXXX`라는 새 고유 폴더만 만든다. 기존 사용자 파일·앱 데이터 DB·활성 규칙을 사용하지 않는다. 원본 제거와 되돌리기는 이 실행이 만든 파일만 대상으로 한다.

모든 실행은 기존 `OperationMutex`로 앱 실행·복구와 직렬화한다. 결과 파일과 SQLite를 삭제하지 않고 남긴다. 실패 시에도 남은 자료를 보존하며 재실행은 새 자료로 시작한다. 끝나면 출력된 폴더 이름을 확인해서 사용자가 별도로 정리할 수 있다. 약 30~60MiB의 테스트 여유 공간을 권장하며 실패 시 잔여 파일이 더 남을 수 있다.

종료 코드 0은 전부 통과, 1은 시나리오 실패, 2는 잘못된 인자·볼륨/잠금/초기화/보고서 저장 실패다. 개별 실패 이유는 출력과 `report.json`에 저장된다.

## 아직 검증하지 않은 범위

기록 실패는 실제 파일 작업 이후 기록 포트의 일회성 실패로 주입했다. 프로세스 강제 종료·전원 차단·실제 디스크 부족·장치 분리는 이 시험으로 검증하지 않았다. 새 DB 연결로 재생했지만 새 OS 프로세스에서의 복구 시험은 아니다.

혼합 배치의 실제 두 볼륨 실행·보상, 역전송 도중의 부분 복사/기록 실패, 임시 파일 정리의 실제 두 볼륨 시험은 별도다. 기존 18개 자동 테스트 묶음은 계속 유지한다. 앱 조립부는 아직 v1/같은 볼륨 실행을 사용하므로 이번 통과가 앱의 다른 드라이브 이동 활성화를 뜻하지 않는다.
