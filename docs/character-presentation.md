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

## 컴패니언 관리 화면의 구성 시안

관리 페이지의 미리보기는 팩에서 제공하는 애니메이션을 선택하고 재생·정지할 수 있다.
하단 타임라인에서 프레임을 선택하면 해당 프레임에서 정지한다.
시간 트랙은 프레임 지속시간에 비례하는 구간과 시작 키, 초 단위 눈금,
현재 프레임 시작 위치를 표시한다. 트랙을 클릭하거나 끌면 해당 프레임으로 이동한다.
재생 중·일시정지·재생 완료 상태와 현재/전체 시간이 하단에 표시된다.
처음 프레임·재생/일시정지·마지막 프레임은 공통 크기의 아이콘 버튼으로 제공한다.
아직 프레임 지속시간이나 순서를 트랙에서 수정하지는 않는다.
`편집`을 켜면 캐릭터 기준점(파랑)과 말풍선 부착점(주황)을 드래그하여 배치를 확인한다.
현재 조정값은 프레임 ID별 메모리 초안으로만 유지하며, 팩 교체나 앱 종료 시 사라진다.
원본 팩, 개인 설정, 바탕화면 컴패니언의 상태에는 적용하지 않는다.
기존 `전체 기준점 보정…`은 별도 기능이며 기존 방식으로 개인 설정을 저장한다.

프레임별 영구 저장·적용·취소, 상속 설정, 팩 내보내기는 후속 설계 대상이다.

`CompanionPreview.qml`은 애니메이션 목록·말풍선 설정·테마를 입력받는 독립 부품이다.
컨트롤러, 현재 페이지 번호, 저장소를 직접 참조하지 않아 다른 화면에 배치할 수 있다.
캐릭터 표시 영역과 `CompanionPlayback.qml`의 재생 영역은 각각 테두리로 구분한다.
재생부는 값 입력과 재생 전환·프레임 선택 신호만 제공하며,
`PlaybackIconButton.qml`과 `CompanionTimeline.qml`도 별도로 재사용할 수 있다.
