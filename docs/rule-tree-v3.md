# 한 노드에 조건 하나인 분류 트리

v3 규칙은 노드 하나가 조건 종류 하나만 검사한다. 자식으로 내려가는 조건은
모두 만족해야 한다(AND). 같은 높이의 형제는 위에서부터 검사해 처음 맞은
가지 하나만 선택한다. 선택한 가지의 자식 조건이 빗나가도 다른 형제로
되돌아가지 않는다. 따라서 그 경우의 목적지가 필요하면 그 높이에
`otherwise: true` 가지를 둔다. 어느 리프에도 목적지가 없을 때의
`globalOtherwise`는 선택 사항이다.

일반 노드의 `when`은 다음 네 가지 중 정확히 하나다.

| 종류 | 입력 | 의미 |
| --- | --- | --- |
| `extension` | `values` 배열 | 확장자가 목록 중 하나와 일치 |
| `nameContains` | `text` | 파일 이름에 글자가 포함됨 |
| `size` | `comparison`, `bytes` | 파일 크기를 바이트 단위로 비교 |
| `modifiedAgeDays` | `comparison`, `days` | 마지막 수정 후 경과한 온전한 일수 비교 |

크기 비교는 `greaterThan`(초과), `atLeast`(이상), `lessThan`(미만),
`atMost`(이하)를 지원한다. 경과일 비교는 `atLeast`와 `lessThan`을
지원한다. 확장자 목록은 같은 종류의 값 중 하나를 고르는 것이므로
한 조건으로 취급한다. 편집기에서는 쉼표로 입력하고 공백·앞의 점·중복을
정리한다. 파일 크기는 화면에서 MiB 또는 B로 입력하며 JSON에는 정확한
바이트 수를 저장한다. 생성 날짜는 아직 조건으로 제공하지 않는다.

```json
{
  "id": "larger-than-five",
  "enabled": true,
  "when": { "type": "size", "comparison": "greaterThan", "bytes": 5242880 },
  "children": [
    {
      "id": "image-extension",
      "enabled": true,
      "when": { "type": "extension", "values": ["png", "jpg"] },
      "destination": { "path": "Pictures", "collisionPolicy": "inherit" }
    }
  ]
}
```

기존 v2 파일은 계속 읽어 분류에 사용할 수 있다. 편집하려면 화면에서
명시적으로 변환해야 한다. 변환 전 원본 JSON을 같은 폴더에 백업한다.
v2 노드에 조건 종류가 둘 이상 있으면 분리 시 형제 규칙 선택 결과가
바뀔 수 있으므로 자동 변환하지 않고 원본을 유지한다. 파일명 글롭과
일부 범위 조건도 의미를 그대로 옮길 수 없으면 변환하지 않는다.
접기·펼치기는 화면 표시만 바꾸며 저장된 분류 순서와 결과에는 영향을 주지 않는다.
