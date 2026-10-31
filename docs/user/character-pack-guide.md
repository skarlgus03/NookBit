# NookBit ImagePack v1: authoring contract

Standalone authoring contract for humans and LLMs producing `imagepack.json`.
Contract revision: 2026-10-28. Instructions are in English; preserve the user's
asset filenames, display names and paths in their original language.
An ImagePack contains images and visual mappings only, not dialogue, scripts,
file operations or new application behavior. Use only the fields below.

## Package and output

```text
MyCharacter/
  imagepack.json
  images/
    character.png
```

- Output one UTF-8 JSON object without comments, trailing commas or extra fields.
- Assets are PNG files. Transparency is recommended; no fixed resolution is required.
- Asset `file` paths are relative to `imagepack.json`. Use `/`; never use absolute
  paths, URLs, `..`, links or references outside the pack.
- Import the pack folder in the Companion page. The app copies the folder without
  modifying the original. Direct ZIP import is not supported.
- An LLM writing JSON still needs the real PNG files, dimensions, frame order and
  desired contact points. Ask for missing information; do not invent asset files.

## Required fields and references

| Top-level field | Contract |
| --- | --- |
| `schemaVersion`, `packType` | Integer `1`, string `"imagepack"`. |
| `id`, `name`, `version` | Pack ID, nonempty display name and nonempty author-supplied version string. |
| `display` | Required `referenceSize`, `pixelsPerUnit`, `pivot`, `filtering`. |
| `sources` | Nonempty array; each entry requires `id`, `kind`, `file`. |
| `frames` | Nonempty array; each entry requires `id`, `sourceId`, `region`. |
| `animations` | Nonempty array; each entry requires `id`, Boolean `loop`, nonempty `steps`. |
| `bindings` | Nonempty array; each entry requires `cueId`, `animationId`. |

Optional top-level fields: `$schema` (string), `author` (string), `bubble` (object).
Do not use explicit `null` for optional fields. References follow
`sources -> frames -> animations -> bindings` and must resolve.
IDs are case-sensitive and match `^[A-Za-z0-9][A-Za-z0-9._-]*$`.
Source, frame and animation IDs are unique within their separate namespaces;
binding `cueId` values are also unique. Unknown fields are rejected.

## Coordinate systems

- `display.referenceSize: {"width":128,"height":128}` is the artist's nominal
  logical canvas size, with finite positive dimensions. It does not resize or crop frames.
- `pixelsPerUnit` is the finite positive number of source pixels per logical unit.
- `pivot: {"x":64,"y":120}` is measured in **logical units** from the selected
  frame region's top-left. X increases rightward; Y increases downward. The pivot
  is placed at the character's screen anchor. It is a contact point: feet when
  standing, a seated contact point when sitting. Negative/outside coordinates are allowed.
- A frame may override `pivot` and `pixelsPerUnit`; otherwise it inherits `display`.
  Frames can have different dimensions and contact points. Never use full-atlas
  coordinates for a cropped frame's pivot.
- `filtering` is `nearest` for pixel art or `linear` for smooth artwork.
- User display scale is a separate application preference, not a manifest field.

Example: a 512x512 PNG at `pixelsPerUnit: 4` occupies 128x128 logical units.
A pivot of `(64,112)` anchors source pixel `(256,448)`. After cropping 128 source
pixels from the left, subtract `128/4 = 32` from this pivot's X coordinate.

## Individual PNGs and atlases

A pack may mix both source kinds. A frame's region must match its source kind.

| `source.kind` | `frame.region` | Meaning |
| --- | --- | --- |
| `image` | `{"type":"image"}` | Entire individual PNG. |
| `atlas` | `{"type":"cell","index":0}` | Cell in the source's required `grid`. |
| `atlas` | `{"type":"rect","x":0,"y":0,"width":128,"height":128}` | Rectangle in source pixels; grid is optional. |

`grid` requires positive integers `frameWidth`, `frameHeight`, `columns`, `rows`.
PNG dimensions must equal `(frameWidth * columns, frameHeight * rows)` exactly.
Cell indices start at zero, left to right then top to bottom, and must be less
than `columns * rows`. Rectangles require integer X/Y >= 0 and width/height >= 1
and must stay within the PNG. Cell spacing, outer margins and rotated regions
are not supported.

## Animation and cue bindings

- A step is `{"frameId":"pose","durationMs":1000}`. `durationMs` is an integer
  from 16 through 60000. Repeated frame IDs are allowed; 1000 ms equals one second.
- `loop: true` repeats the clip; `false` holds its last frame after completion.
  A one-step clip is a valid still pose.
- Bind all three required cues: `resting`, `calling`, `working`. They may reference
  the same clip. These are fallback visual states, not commands to file workers.
- Optional bindings can target actual application cues such as `file.offer` and
  `file.work`. An unbound cue uses the relevant fallback state. Inventing a cue
  name in JSON does not add a feature or cause that cue to be emitted.
- File work emits `file.work` during actual file-operation tasks, including undo
  and recovery. Read-only preparation and approval dialogs do not emit it. Use a
  looping work clip; its frame speed and loop count do not represent file counts
  or byte progress. Bind `file.work.completed` and `file.work.failed` to optional
  result clips. Result presentation lasts 2,400 ms; a non-looping result clip holds
  its final frame until that time expires. Without these bindings the current
  fallback artwork is used with the result message. The bundled Nmoo work and
  completion clips temporarily reuse its existing standing/blink frames.
- New file work immediately replaces any previous result cue. On timeout, the
  companion re-evaluates the current state (including pending offers), rather than
  restoring a saved old state. Results are presentation events, not notification
  types or user interactions, and cannot initiate file operations.
- The optional feeding module emits `interact.feed` for 2,400 ms. Bind that cue
  to an authored eating animation if available; otherwise normal fallback artwork
  is used with the feeding message. No new top-level field or schema version is
  needed. Feeding is started by the desktop right-click menu, not by pack data.
- Autonomous walking requires both `walk.left` and `walk.right`. If both bind to
  the same animation, it is treated as a left-facing original and mirrored about
  the pivot when facing right. Different animations use their own artwork without
  automatic mirroring. Looping is recommended, for example frames 1,2,3,2 at 140 ms
  each. Clip duration does not determine movement speed. No extra walking fields
  are required; packs missing either direction remain stationary.

## Complete minimal example

Provide a 128x128 `images/character.png`. Save this entire object as `imagepack.json`.
All three fallback cues use the same still image.

```json
{
  "schemaVersion": 1,
  "packType": "imagepack",
  "id": "my.character",
  "name": "Sample Companion",
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

## Atlas replacement fragment

Provide a 256x128 `images/sheet.png` with two 128x128 cells. Replace the minimal
example's `sources` and `frames` with this fragment. This is not a complete JSON file.

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

To animate it, replace `still.steps` with
`[{"frameId":"pose","durationMs":200},{"frameId":"pose2","durationMs":200}]`.

## Speech bubbles: two different attachment points

Omit `bubble` to use the default bubble. Its fields are optional when the object
is present, but explicit null is invalid.

- `attachment` is an offset from the character pivot, in **character logical units**.
  Both coordinates must be finite and within [-4096,4096]. User character scale applies.
- `pivot` is the contact point inside the entire bubble, including its tail, in
  **normalized [0,1] coordinates**: `(0,0)` top-left, `(1,1)` bottom-right.
  Use Y=1 for a default bubble above the character.
- Defaults: attachment `(0,-120)`, bubble pivot `(0.1,1)`, skin `{"kind":"default"}`.
- Screen contact = character screen anchor + character scale * attachment.
  Bubble top-left = contact - `(bubbleWidth * pivot.x, bubbleHeight * pivot.y)`.
  Bubble dimensions do not scale with the character. Desktop screen-edge correction
  may reposition or flip the bubble, changing exact tail alignment.

Custom backgrounds support an entire individual PNG only. Add
`{"id":"bubble-bg","kind":"image","file":"images/bubble.png"}` to `sources`,
then add the following top-level fragment. It is not a complete JSON file.

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

`size`, `textRect` and `actionRect` use **bubble logical units**, not PNG pixels.
Width must be 100..1024 and height 64..1024. Rectangles use integer coordinates
and dimensions, must each be at least 32x20, remain inside the bubble and not overlap.
`actionRect` is mandatory even when no action button is visible. Text currently uses
a light foreground, so use a dark background in the text area. Long text may be
truncated. Font settings, SVG skins, animated/atlas bubbles, nine-slice backgrounds
and per-cue skins are not manifest features; do not invent fields for them.

## Personal editing and unsupported extensions

The Companion preview can edit frame contact points, bubble attachment points and
per-animation step durations. Contact-point overrides are shared wherever the same
frame is used; step durations belong to their animation steps. Apply saves personal
overrides and updates the desktop companion; Cancel restores the last applied state;
Defaults previews the original values and Apply then clears the overrides.
The source pack is never rewritten. Personal edits cannot currently be exported as
a new pack. Preview zoom is independent of desktop display scale.

The editor provides coordinate entry, frame-to-frame point alignment, arrow-key
frame selection and optional 100 ms timeline snapping (Alt bypasses snapping).
Animation selection is locked during editing. Desktop bubble edge correction is
disabled in the editing preview so attachment points can be inspected directly.

Before import, check IDs, references, paths, PNG dimensions, frame bounds, required
cues and coordinate units. JSON validity alone does not verify artwork. Test import
and display in the app; failed imports do not replace the current companion.
DialoguePack, combined CharacterPack, module tags, companion-presence conditions,
two-character synchronized animations and executable scripts are not supported.
Do not describe planned extensions as fields accepted by the current loader.
