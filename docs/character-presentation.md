# Character presentation contract v1

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

## Files and current scope

- `schemas/character-presentation.schema.json`: JSON structure.
- `resources/character/default.presentation.json`: current text-only default.
- `resources/character/example.sprite.presentation.json`: a 4-column, 3-row,
  128-by-128-cell example; its `sprites/nookbit.png` is a placeholder path, not
  an included asset.
- `CharacterController`: selects `cueId`, `message`, and `animationId` and retains
  validated metadata for a future renderer.
- `CharacterBadge.qml`: current placeholder face; it does not play sprite sheets.

No sprite image, animation timer, audio system, expression editor or Explorer
window integration is introduced by this contract.
