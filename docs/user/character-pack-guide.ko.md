# NookBit 캐릭터팩 제작 가이드 — ImagePack v1

이 문서만으로 제작할 수 있는 현행 규격입니다. 사람과 AI 모두 필드 이름·자료형·좌표 단위를 그대로 따라야 합니다. 팩에는 이미지와 연출 설정만 들어갑니다. 파일 정리 작업·실행 스크립트·대사는 포함하지 않습니다.

## 1. 전달할 파일

```text
MyCharacter/
  imagepack.json
  images/
    character.png
```

- `imagepack.json`: UTF-8 JSON 객체 하나. 주석·후행 쉼표·정의되지 않은 필드 금지.
- 이미지: PNG만 지원. 투명 배경 권장. 128·256·512 등 특정 해상도로 고정하지 않음.
- `file`: 설정 파일 기준 상대 경로, `/` 사용. 절대 경로·URL·`..`·팩 밖의 파일 참조 금지.
- 앱에서 **팩 폴더 가져오기…**로 폴더를 선택. 원본을 변경하지 않고 복사함. ZIP 직접 가져오기는 미지원.

## 2. 필수 항목과 연결

| 최상위 항목 | 내용 |
| --- | --- |
| `schemaVersion`, `packType` | 숫자 `1`, 문자열 `"imagepack"` 고정 |
| `id`, `name`, `version` | 팩 식별자·표시 이름·버전 문자열. name/version은 빈 문자열 금지 |
| `display` | referenceSize, pixelsPerUnit, pivot, filtering 모두 필수 |
| `sources` | PNG 목록. 각 항목 id, kind, file 필수 |
| `frames` | 프레임 목록. 각 항목 id, sourceId, region 필수 |
| `animations` | 애니메이션 목록. 각 항목 id, loop, steps 필수 |
| `bindings` | 상황 연결. 각 항목 cueId, animationId 필수 |

선택 항목은 `$schema`(문자열), `author`(문자열), `bubble`입니다. 각 목록은 비어 있으면 안 됩니다.
sources → frames → animations → bindings 순서로 참조가 이어집니다. 모든 참조 ID가 존재해야 합니다.
ID 형식은 `^[A-Za-z0-9][A-Za-z0-9._-]*$`이며 대소문자를 구분합니다. source/frame/animation은 각각의 목록 안에서 ID가 유일해야 하고, binding의 cueId도 중복할 수 없습니다.

## 3. 크기와 피벗: 픽셀과 표시 단위 구분

- `referenceSize: {width, height}`: 작가의 기준 크기. 양의 유한수. 프레임을 이 크기로 강제 늘이거나 잘라내는 설정이 아님.
- `pixelsPerUnit`: 원본 몇 픽셀이 표시 기준 1단위인지 정하는 양의 유한수.
- `pivot: {x, y}`: 프레임 왼쪽 위에서 측정한 **표시 기준 단위**. X는 오른쪽, Y는 아래쪽. 화면의 캐릭터 기준점에 피벗이 놓임.
- `filtering`: 도트는 `"nearest"`, 부드러운 그림은 `"linear"` 권장.
- 각 frame은 pivot/pixelsPerUnit을 개별 지정할 수 있고 생략하면 display 값 상속.
- 피벗은 음수이거나 그림 바깥이어도 됨. 아틀라스 전체가 아닌 잘라낸 프레임의 왼쪽 위 기준.
- 사용자의 표시 크기는 앱 개인 설정이며 팩 그림이나 JSON을 바꾸지 않음.

예: 512×512 PNG, pixelsPerUnit=4, pivot=(64,112) → 기준 크기 128×128, 원본 픽셀 (256,448)이 화면 기준점에 놓입니다. 자세마다 피벗을 맞추면 같은 위치에 서 있어요. 왼쪽에서 128픽셀을 잘랐다면 이 예제의 pivot.x에서 128/4=32를 빼세요.

## 4. 개별 PNG와 아틀라스

한 팩에 두 방식을 섞을 수 있습니다. kind와 region.type 조합을 지키세요.

| source.kind | frame.region | 사용 조건 |
| --- | --- | --- |
| `image` | `{"type":"image"}` | 개별 PNG 전체 |
| `atlas` | `{"type":"cell","index":0}` | source에 grid 필수. 번호는 0부터, 왼쪽→오른쪽→다음 줄 |
| `atlas` | `{"type":"rect","x":0,"y":0,"width":128,"height":128}` | 원본 픽셀의 사각 영역. grid 없이도 가능 |

grid 필드는 frameWidth/frameHeight/columns/rows이며 모두 1 이상의 정수입니다. PNG 크기는 정확히 (frameWidth×columns, frameHeight×rows)여야 합니다. 칸 간격·바깥 여백·회전 영역은 미지원입니다. index < columns×rows여야 합니다. rect의 x/y는 0 이상의 정수, width/height는 1 이상의 정수이며 PNG 밖으로 나갈 수 없습니다.

## 5. 애니메이션과 필수 상황

- `steps`: 비어 있지 않은 `{"frameId":"pose","durationMs":1000}` 목록. durationMs는 16~60000 정수, 1000ms=1초. 같은 frameId 반복 가능.
- `loop`: Boolean. true는 반복, false는 종료 뒤 마지막 그림 유지. 한 프레임으로 정지 그림 가능.
- 필수 cueId: `resting`(휴식), `calling`(말 걸기), `working`(작업 중). 모두 있어야 하며 같은 animationId를 연결해도 됨.
- 선택 cueId: `file.offer`, `file.work` 등 앱이 실제 보내는 상황. 모르는 상황은 해당 기본 상태로 대체됨. 새 이름을 적는 것만으로 새 기능이 생기지 않음.

## 6. 최소 예제

128×128 PNG를 images/character.png로 준비하고 아래 객체 전체를 imagepack.json으로 저장하세요. 세 상황 모두 같은 정지 그림을 사용합니다.

```json
{
  "schemaVersion": 1,
  "packType": "imagepack",
  "id": "my.character",
  "name": "내 캐릭터",
  "version": "1.0.0",
  "display": {
    "referenceSize": { "width": 128, "height": 128 },
    "pixelsPerUnit": 1,
    "pivot": { "x": 64, "y": 120 },
    "filtering": "linear"
  },
  "sources": [
    { "id": "picture", "kind": "image", "file": "images/character.png" }
  ],
  "frames": [
    { "id": "pose", "sourceId": "picture", "region": { "type": "image" } }
  ],
  "animations": [
    { "id": "still", "loop": true, "steps": [ { "frameId": "pose", "durationMs": 1000 } ] }
  ],
  "bindings": [
    { "cueId": "resting", "animationId": "still" },
    { "cueId": "calling", "animationId": "still" },
    { "cueId": "working", "animationId": "still" }
  ]
}
```

아틀라스로 바꾸려면 256×128 PNG images/sheet.png를 준비하고 위 sources/frames를 아래 항목으로 교체하세요.

```json
"sources": [
  { "id": "sheet", "kind": "atlas", "file": "images/sheet.png",
    "grid": { "frameWidth": 128, "frameHeight": 128, "columns": 2, "rows": 1 } }
],
"frames": [
  { "id": "pose", "sourceId": "sheet", "region": { "type": "cell", "index": 0 } },
  { "id": "pose2", "sourceId": "sheet", "region": { "type": "cell", "index": 1 } }
]
```

움직이게 하려면 still 애니메이션의 steps를 `[{"frameId":"pose","durationMs":200},{"frameId":"pose2","durationMs":200}]`로 교체하세요. 이 조각은 독립 JSON 파일이 아니라 전체 설정의 항목을 교체하는 예제입니다.

## 눅빗 안에서 개인 설정 조정하기

컴패니언 페이지에서 `편집`을 켜면 현재 프레임의 파란 캐릭터 기준점과 주황 말풍선 부착점의
십자 중심을 끌어 조정할 수 있습니다. 미리보기 안에서 이미지 왼쪽 위 기준 X·Y 좌표도 확인할 수 있습니다.
위쪽 시간 눈금으로 프레임을 고르고, 아래 구간의 오른쪽 끝을 끌면
그 구간의 지속시간이 바뀝니다. 기준점은 같은 프레임을 쓰는 모든 애니메이션에 공유되고,
지속시간은 각 애니메이션의 구간마다 따로 적용됩니다.
편집 모드의 `스냅 · 0.1초`를 켜면 시간 이동과 구간 끝이 0.1초 눈금에 맞춰집니다.
버튼으로 끄거나 Alt를 누른 채 드래그하면 더 세밀하게 조절할 수 있습니다.

- **적용:** 개인 설정에 저장하고 바탕화면 컴패니언에도 즉시 반영합니다. 재실행해도 유지됩니다.
- **취소:** 마지막으로 적용한 설정으로 돌아갑니다.
- **기본값:** 미리보기를 원본 값으로 되돌립니다. 이어서 적용하면 개인 수정값을 지웁니다.

변경이 있을 때만 적용 버튼이 활성화됩니다. 저장되면 미리보기 위에 짧은 알림이 나타났다 사라집니다.

원본 팩 파일은 바뀌지 않습니다. 미적용 변경이 있으면 다른 팩을 선택하기 전에 적용하거나
취소해야 합니다. 적용하지 않은 초안은 앱 종료 시 사라집니다.
기존 `전체 기준점 보정`은 모든 프레임에 더해지는 별도 개인 보정입니다.
수정한 내용을 팩으로 내보내는 기능은 아직 제공하지 않습니다.

## 7. 말풍선: 서로 다른 두 기준점

bubble 생략 시 기본 말풍선을 사용합니다. bubble과 내부 항목은 선택 사항이지만 명시적 null은 금지합니다.

- `attachment`: 캐릭터 피벗에서 말풍선이 붙는 지점까지의 거리. **캐릭터 표시 기준 단위**, 유한수 [-4096,4096]. 캐릭터 개인 표시 크기가 적용됨.
- `pivot`: 말풍선 전체(꼬리 포함) 안의 맞닿을 지점. **정규화 좌표 [0,1]**. (0,0) 왼쪽 위, (1,1) 오른쪽 아래. 기본 말풍선은 y=1 권장.
- 기본값: attachment=(0,-120), pivot=(0.1,1), skin=`{"kind":"default"}`.
- 배치: 맞닿을화면점 = 캐릭터화면피벗 + 개인표시크기×attachment. 말풍선왼쪽위 = 맞닿을화면점 - (말풍선너비×pivot.x, 말풍선높이×pivot.y).
- 말풍선 크기 자체에는 캐릭터 크기를 곱하지 않음. 모니터 밖이면 위치·방향을 보정하므로 꼬리가 정확히 맞지 않을 수 있음.

커스텀 말풍선을 쓰려면 sources에 `{"id":"bubble-bg","kind":"image","file":"images/bubble.png"}`를 추가하고 아래 bubble 항목을 최상위에 넣으세요.

```json
"bubble": {
  "attachment": { "x": 12, "y": -140 },
  "pivot": { "x": 0.1, "y": 1 },
  "skin": {
    "kind": "image",
    "sourceId": "bubble-bg",
    "size": { "width": 270, "height": 120 },
    "textRect": { "x": 14, "y": 12, "width": 242, "height": 42 },
    "actionRect": { "x": 194, "y": 72, "width": 64, "height": 28 }
  }
}
```

- 말풍선 source는 개별 PNG 전체만 가능. size/textRect/actionRect는 PNG 픽셀이 아닌 표시 기준 단위.
- size: 너비 100~1024, 높이 64~1024. 영역 좌표·크기는 정수, 각 영역 최소 32×20. 전체 안에 들어가고 서로 겹치지 않아야 함.
- actionRect는 버튼이 안 나오는 상황에도 필수. 현재 글자는 밝은 색이므로 글자 영역의 배경은 어둡게 제작.
- 고정 크기 배경에 글자·버튼 영역 배치. 긴 글자는 잘릴 수 있음. 말풍선 애니메이션·아틀라스·나인슬라이스·상황별 말풍선 변경 미지원.

## 8. 가져오기 전 확인

참조 ID·파일 경로·PNG 크기·프레임 범위·필수 상황 3개·좌표 단위를 확인하세요. JSON 검증만으로 그림이 정상이라는 보장은 없습니다. 앱에서 가져오기와 표시도 시험하세요. 실패한 가져오기는 현재 캐릭터를 바꾸지 않습니다.
대사팩·통합 CharacterPack·동반 캐릭터 조건·두 캐릭터 동기 연출·실행 스크립트는 미지원입니다. AI에게 도움을 요청한다면 이 문서와 실제 PNG 크기·파일 이름·상황별 프레임 순서를 함께 전달하고, 미지원 필드를 만들지 않도록 요청하세요.
