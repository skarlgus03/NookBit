# NookBit ImagePack v1

Status: the JSON loader and common resolved frame data are implemented. The
application still uses the existing character presentation profile and grid
player. Importing, selecting and pivot-aware rendering are subsequent tasks.
Do not pass these examples to the existing `character.presentation.json` loader.

## Scope and layout

An ImagePack contains visual assets and presentation mappings only. It contains
no dialogue, executable scripts, file-management operations or worker threads.
A future CharacterPack can reference an ImagePack and an independent DialoguePack.
ImagePack IDs identify pack types; runtime character instance IDs live outside
the manifest. Loading the same pack twice does not create two different pack IDs.

```text
MyImagePack/
  imagepack.json
  images/
    idle-a.png
    idle-b.png
    work-atlas.png
```

`schemas/imagepack.schema.json` defines the JSON structure. Top-level fields:

| Field | Meaning |
| --- | --- |
| `schemaVersion` | Must be `1`; independent from existing presentation/rule/DB versions |
| `packType` | Must be `imagepack` |
| `id`, `name`, `version`, `author` | Stable pack ID, display name, author-supplied version string, optional author |
| `display` | Artist's reference size, source-pixel density, default pivot, filtering |
| `sources` | Individual PNG files or PNG atlas files |
| `frames` | Named regions with effective pivot and pixel density |
| `animations` | Ordered frame references, per-step duration, looping |
| `bindings` | Situation ID to animation ID |

IDs are case-sensitive ASCII letters/digits/dots/underscores/hyphens, starting
with a letter or digit. Source, frame and animation namespaces are separate.
IDs must be unique within each namespace. Binding cue IDs must also be unique.
Unknown fields are rejected in v1 rather than silently treated as implemented
behavior. Runtime selection can evolve separately from how a frame is stored.

## Individual images and atlases

Each source uses `kind: "image"` or `kind: "atlas"`. A pack may use both.
All asset files are PNGs. There is no pack-wide mode that forces every file to use
the same layout. PNG paths are relative to `imagepack.json`, use `/`, and must
remain inside the imported pack directory. Absolute paths, URLs and parent
traversal are not valid asset references.

A frame names its `sourceId` and a `region`:

- `{ "type": "image" }`: entire individual PNG; source kind must be `image`.
- `{ "type": "cell", "index": 4 }`: cell in an atlas with a declared `grid`.
- `{ "type": "rect", "x": 128, "y": 256, "width": 128, "height": 128 }`:
  arbitrary rectangle in an atlas, with or without a grid declaration.

Atlas grid fields `frameWidth`, `frameHeight`, `columns` and `rows` are declared
in JSON. Cell indices start at zero, left to right, then top to bottom:

```text
0  1  2  3
4  5  6  7
8  9 10 11
```

`cellX = (index % columns) * frameWidth`

`cellY = floor(index / columns) * frameHeight`

v1 grids have no cell spacing or outer margin, and decoded image dimensions must
exactly equal the declared grid dimensions. Explicit rectangles allow differing
frame sizes and hand-packed atlases. Rotated entries and automatic atlas packing
are outside v1. A future importer resolves all three region forms into the same
frame description; a renderer does not need feature-specific storage branches.

## Pivot and size: precise coordinate rules

`display.referenceSize` is the artist's nominal logical canvas size. It is not an
image-size requirement, a clipping rectangle, or an instruction to stretch each
frame to the same width. Gestures may exceed the reference rectangle.

`display.pixelsPerUnit` tells how many source pixels represent one logical unit.
For a 512-by-512 image on a 128-by-128 logical canvas, use `4`. A 128-by-128 image
on the same logical canvas uses `1`. A frame may override `pixelsPerUnit` when a
pack intentionally mixes asset resolutions. The ratio must be finite and positive.

`display.pivot` is mandatory. Every resolved frame has a pivot in logical units.
Frames inherit that default or override it with their own `pivot`. Coordinates
are relative to the selected frame region's top-left, never to the full atlas.
Positive X goes right and positive Y goes down. The pivot is the point placed at
the character's screen anchor, defining local `(0, 0)`. It can be outside the
visible region, so negative/outside pivot coordinates are valid and not clamped.

For example, a 512-pixel frame with `pixelsPerUnit: 4` and pivot `(64, 112)` has
its anchor at source pixel `(256, 448)`. This works identically for an individual
PNG and a 512-by-512 region extracted from an atlas. The atlas region's absolute
X/Y coordinates do not get added to its pivot.

Keep a common logical canvas when initially drawing frames. If trimming 128
source pixels from the left and 64 from the top at a ratio of 4, the new pivot
must subtract `(32, 16)`. Original pivot `(64, 112)` becomes `(32, 96)`. This is
how unequal, cropped frame dimensions preserve the same character position.

The renderer's intended coordinate calculation is:

```text
effectivePivot = frame.pivot ?? display.pivot
effectiveRatio = frame.pixelsPerUnit ?? display.pixelsPerUnit
screenPoint = characterAnchor
            + userScale * (frameLocalPixel / effectiveRatio
                           - (effectivePivot + userPivotOffset))
```

`frameLocalPixel` is measured from the extracted region's top-left. Do not
auto-fit each frame to `referenceSize`: doing so changes body size across poses.
Source image dimensions still determine decoded memory usage.

The user's `userScale`, optional common `userPivotOffset`, and screen/window
anchor are application settings per character instance. They do not modify the
pack. Import can offer a size preview and initialize user scale; the pack only
provides an artist's reference. `nearest` filtering suits pixel art; `linear`
supports smooth illustration scaling.

## Animation and situation selection

Each animation has an `id`, `loop`, and `steps` of `{frameId, durationMs}`.
Repeated frame IDs and different per-step durations are valid. Durations are
16 through 60000 milliseconds. A one-step animation is a still image.
`loop=false` holds the last frame after its duration until the selected situation
changes. Changing text alone does not restart visual playback.

Bindings for `resting`, `calling` and `working` are required and serve as generic
fallbacks. Additional cue IDs, such as `file.offer`, can map to any animation.
Unknown situations use the relevant generic fallback. Dialogue belongs to a
different pack and is not stored in ImagePack bindings.

Multi-character expression selection belongs before animation playback. A future
selection context may include the target character instance, active companions,
and a shared expression session/start time. The renderer receives a resolved
animation and anchor; it does not execute jobs or manage companions. Basic packs
need no companion information. v1 does not claim to implement conditional bindings
or synchronized paired animations.

## Validation at future import

JSON Schema checks shape and field types. The importer must additionally check:

- Unique IDs, valid source/frame/animation references and the three fallback bindings.
- Region/source compatibility: individual image, grid cell or atlas rectangle.
- Existing, readable PNGs inside the imported pack directory.
- Actual image dimensions, in-bounds rectangles and cell index ranges.
- Finite coordinate and density values and supported manifest version.

Pivot coordinates outside a cropped frame are valid. Import errors must identify
the relevant source/frame/animation ID; a failed import must retain the currently
selected pack. The existing grid presentation profile is not silently converted
or relabeled as an ImagePack.

## Examples and next step

`resources/imagepacks/examples/individual.imagepack.json` assumes 512-by-512
frames at density 4; `carry` demonstrates a trimmed-frame pivot override.
`resources/imagepacks/examples/atlas.imagepack.json` demonstrates density 1,
128-by-128 grid cells and a rectangle reference to the existing test atlas layout.
Both examples are manifests only: their `images/` paths are placeholders, not
ready-to-import packs with included assets.

`IImagePackLoader` returns Qt-independent `ImagePack` values. `JsonImagePackLoader`
reads an absolute local manifest path and the v1 schema, validates references and
resolves full-image/cell/rectangle regions to source pixel rectangles. Every
returned frame has a canonical absolute image path, actual image dimensions,
effective logical pivot and positive pixel density. It does not copy/import files,
modify selected packs, render images, store user scale or execute feature work.

Asset validation probes the PNG header and dimensions without decoding all pixel
data. A valid header is not proof that every compressed pixel is intact; decoding
errors and files changed after loading remain renderer checks. Asset paths are
canonicalized and must remain under the manifest directory, including symlink
targets. Unusable input returns a field/item-specific error rather than partial
pack data. The shared JSON validator now handles v1's `oneOf` variants and positive
numeric bounds.

Next adapt the current renderer to resolved ImagePack frame data, then build import UI.
