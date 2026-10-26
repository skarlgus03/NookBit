# Character presentation contract v1

The default visual path now uses a complete ImagePack and the common-frame player;
see `imagepack-v1.md`. This older profile continues to supply default messages and
supports existing custom grid profiles. It is not the new ImagePack manifest format.

The character receives feature-independent updates through `ICharacterChannel`.
`sourceId` identifies the feature, `entryId` identifies its active item, and
`kind` is `Activity` or `Attention`. `active=false` removes only that item.
`cueId` is a presentation situation identifier, for example `file.offer` or
`another.feature.notice`. Features decide when the situation applies; the
character does not inspect files or implement feature conditions.

## Situation mapping

The JSON profile maps each cue ID to a UTF-8 `message` and optional `animationId`.
Adding a presentation for an already-emitted cue requires a JSON edit, not a new
character state branch. A new behavior still needs its feature to emit an update.
An explicit nonempty update message overrides the configured message.
The mandatory fallback cues are `resting`, `calling`, and `working`. Unknown or
empty cue IDs fall back to the current generic state. Activity outranks attention.
Multiple items of the same kind retain the existing identifier-sorted selection.

The application reads `character.presentation.json` in its app-data directory
when present; otherwise it reads the bundled default. Restart to reload edits.
An invalid custom profile is reported to stderr and the bundled default is used.
File sorting rule JSON and SQLite schema versions are unaffected.

## Sprite sheet layout

Use transparent PNG files with equal-sized cells in a rectangular grid.
`frameWidth` and `frameHeight` are pixels, not display dimensions. There is no
padding or spacing between cells. The image dimensions must be exactly
`frameWidth * columns` by `frameHeight * rows` when the renderer is implemented.
Keep the character's anchor position consistent inside every cell.

Frame indices are zero-based, left to right, then top to bottom:

```text
0  1  2  3
4  5  6  7
8  9 10 11
```

An animation references a sheet ID and an ordered list of frame indices.
Repeated indices are allowed. `frameDurationMs` applies to each list entry.
`loop=true` repeats; `loop=false` plays once and holds the last frame until the
selected situation changes. A one-frame animation represents a still image.
Different poses or directions can use separate animation IDs on the same sheet.
IDs use ASCII letters, digits, dots, underscores and hyphens and start with a
letter or digit.

Image paths are relative to the profile's directory, use `/`, and must end in
`.png`. Absolute paths, drive prefixes and parent traversal are rejected.
The JSON loader validates metadata and references only. Reading image pixels,
checking actual dimensions and playing frames are renderer responsibilities.
`CharacterSprite.qml` now checks the decoded image dimensions, clips a single
frame from the cached atlas and implements the declared frame order and looping.
Invalid or missing images leave the placeholder face visible with an error message.

## Files and current scope

- `schemas/character-presentation.schema.json`: JSON structure.
- `resources/character/default.presentation.json`: current test-sprite default.
- `resources/character/sprites/test-character.png`: included transparent 512-by-384
  test atlas, twelve 128-by-128 cells with visible frame numbers.
- `resources/character/example.sprite.presentation.json`: a 4-column, 3-row,
  128-by-128-cell example; its `sprites/nookbit.png` is a placeholder path, not
  an included asset.
- `CharacterController`: selects `cueId`, `message`, and `animationId` and provides
  the renderer with the resolved image URL and playback metadata.
- `CharacterBadge.qml`: displays the test sprite and retains a placeholder face
  when a profile has no animation or its image cannot be played.
- `tools/generate_test_character_sheet.ps1`: regenerates the test atlas from code.

Frame timers stop when the sprite is hidden, the application is hidden or minimized,
the image is invalid, playback has finished, or the animation contains one frame.
Text-only changes do not restart playback. Production character artwork, audio,
an expression editor and Explorer window integration remain future work.

## 컴패니언 미리보기와 개인 편집

관리 페이지의 미리보기는 팩에서 제공하는 애니메이션을 선택하고 재생·정지할 수 있다.
하단 타임라인의 위쪽 시간 눈금에서 원하는 프레임으로 이동할 수 있다.
시간 트랙은 프레임 지속시간에 비례하는 구간과 시작 키, 0.5초 간격 눈금,
연속 재생 위치를 표시한다. 트랙을 클릭하거나 끌면 해당 시각으로 이동하고 정지한다.
`CompanionPlaybackClock.qml`이 미리보기 시간과 프레임 선택을 함께 관리한다.
일시정지·재개는 프레임 내부의 시각을 보존하며, 반복 시 초과 시간을 다음 회차로 넘긴다.
이미지 로딩 중에는 재생 시계도 대기한다. 바탕화면 재생기는 기존 방식을 유지한다.
시간축은 애니메이션 길이와 무관하게 1초당 160 논리 픽셀을 유지한다.
짧은 애니메이션 뒤에는 빈 시간이 보이며 긴 애니메이션은 가로 스크롤한다.
재생 중·일시정지·재생 완료 상태와 현재/전체 시간이 하단에 표시된다.
처음 프레임·재생/일시정지·마지막 프레임은 공통 크기의 아이콘 버튼으로 제공한다.
시간 탐색은 위쪽 눈금에서만 동작한다. 편집 모드에서는 아래 프레임의 오른쪽 경계를
드래그하여 지속시간을 16–60000ms 범위에서 조정할 수 있다.
편집 모드의 스냅은 기본 100ms 간격이다. 재생부 버튼으로 켜거나 끄며 Alt를 누르는 동안 해제된다.
재생 중 시간은 양자화하지 않고 사용자가 눈금을 탐색하거나 길이를 드래그할 때만 적용한다.
프레임 끝의 절대 시각을 눈금에 맞추므로 시작점이 눈금 밖이어도 끝은 정렬된다.
기존 프레임은 조작하기 전까지 원래 시간을 유지하며 최소·최대 지속시간 제한을 우선한다.
조정한 구간 뒤의 프레임들은 연속해서 다시 배치되며 현재 시간 커서는 가능한 범위에서 유지된다.
지속시간 초안은 애니메이션 ID와 등장 순서별로 보관하므로 같은 이미지가 반복되어도 독립적이다.
프레임 순서 편집은 아직 제공하지 않는다.
`편집`을 켜면 캐릭터 기준점(파랑)과 말풍선 부착점(주황)을 드래그하여 배치를 확인한다.
편집 중에는 메모리 초안만 변경된다. `적용`은 검증·저장 성공 후 바탕화면 재생에도 반영하고,
`변경 취소`는 마지막 적용값으로 돌아간다. 미적용 변경이 있으면 표시하며 팩 교체·임포트는 잠근다.
`팩 기본값`은 초안을 원본 값으로 되돌린다. 이 상태에서 적용하면 개인 편집값이 제거된다.
아직 적용하지 않은 초안은 앱을 종료하면 사라진다.
기존 `전체 기준점 보정…`은 프레임별 값에 추가하는 별도 개인 보정이다.
미리보기와 바탕화면은 같은 보정값을 사용한다.

저장 대상은 프레임 ID별 `pivot`·`attachment`와 애니메이션 ID/등장 순서별 지속시간이다.
`CompanionEditController`는 주입받은 검증·적용·읽기·저장 창구만 호출한다.
Windows 사용자 설정의 `character/edits/<library-key>`에 버전 1 JSON 문서로 저장하며
앱 재시작·팩 재선택 시 참조와 수치 범위를 검사하여 복원한다. 원본 ImagePack은 변경하지 않는다.
수정값이 없는 프레임은 팩의 값을 상속한다. 저장·검증 실패 시 초안을 유지하고 오류를 표시한다.
팩 내보내기와 세밀한 프레임별 상속 UI는 후속 작업이다.

`CompanionPreview.qml`은 원본 애니메이션 목록·말풍선 설정·적용값·공통 보정·테마를 입력받고
적용 요청을 신호로 전달하는 독립 부품이다.
컨트롤러, 현재 페이지 번호, 저장소를 직접 참조하지 않아 다른 화면에 배치할 수 있다.
캐릭터 표시 영역과 `CompanionPlayback.qml`의 재생 영역은 각각 테두리로 구분한다.
재생부는 값 입력과 재생 전환·프레임 선택 신호만 제공하며,
`PlaybackIconButton.qml`과 `CompanionTimeline.qml`도 별도로 재사용할 수 있다.
