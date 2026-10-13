# NookBit ImagePack v1

Status: the JSON loader, common resolved frame data and pivot-aware renderer are
implemented. The default test character now loads a complete ImagePack beside
the executable. Folder importing, pack selection, personal scale adjustment,
desktop rendering and default/custom bubble support are implemented.
The in-app Korean user guide is `docs/user/character-pack-guide.ko.md`.
Do not pass ImagePack manifests to the older `character.presentation.json`
loader; older custom profiles remain supported separately.

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

## Validation at import

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

`resources/imagepacks/test/` is a complete, ready-to-load test pack with its PNG.
Builds copy it beside the executable under `imagepacks/test/`; Release packaging
copies it into the same relative location. The default startup and headless app
check both use the ImagePack loader and the common-frame player. Existing custom
legacy profiles retain their legacy visual selection.

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

`CharacterController::setImagePack` checks common-frame invariants before replacing
the selected visual pack. Invalid input leaves previous playback unchanged.
The renderer consumes resolved step data, clips only the requested image region,
positions it relative to its logical pivot, applies personal scale and supports
different source files and step durations. It also converts old grid descriptors
to the same runtime step shape.

`CharacterSprite.qml` accepts `userScale`, `userPivotOffset`, and a local `anchor`.
The sidebar starts at scale 0.5 and offset (0, 0). The character settings dialog
allows selection, folder import and personal scale adjustment (10–200%). The
nominal widget rectangle does
not auto-fit or clip a wider pose. The actual app window still bounds rendering.
New PNGs may pause playback during their first asynchronous decode; the current
test pack uses one cached atlas.

## Folder import and selection

Choose a local folder containing `imagepack.json` and its declared PNG assets.
`IImagePackLibrary` separates storage from playback. `DirectoryImagePackLibrary`
validates the source, copies only the manifest and declared sources to an owned
temporary directory, revalidates that snapshot and publishes it by renaming the
directory. Failure removes only the owned temporary copy; the original folder
and current selection are untouched. Import runs off the UI thread without
creating file-organization activity. A header-valid PNG may still fail at decode
time, as described above; import does not guarantee full pixel integrity.

Imported packs live under the app data directory's `imagepacks/<UUID>/`. The
library key is not the pack's content `id`: reimporting never overwrites another
copy. The bundled test pack has key `builtin` and remains beside the executable.
Invalid stored packs are omitted from the list and rechecked when selected.

QSettings stores `character/selectedPack` and `character/scales/<library-key>`.
These are personal settings, not edits to the pack. Startup restores the chosen
key and scale; a missing/unusable selection falls back to the bundled character
(or an existing legacy presentation). `--verify-ui` uses only bundled data and
does not write these settings. Offset editing, pack deletion, ZIP import and
multi-character placement are deliberately not included in this step.

## Desktop presentation window

`CharacterWindow.qml` consumes the same animation and text values as the sidebar;
it never invokes organization workers. A transparent, frameless, always-on-top
tool window has no transient parent, so minimizing the main window does not hide
it. Its global pivot stays fixed across scale and pose changes; the window bounds
use the union of the current animation's steps, with a defensive 2048-logical-unit
extent limit. Extremely large or distant poses can be clipped, not auto-fitted.

`CharacterWindowController` manages only visibility and global anchor position.
Available screen rectangles are injected by the composition root. Saved anchors
are clamped to the nearest available screen after startup or monitor changes;
this protects the anchor, not the entirety of an arbitrarily large pose.
QSettings stores `character/windowAnchor` and `character/windowShown`. Dragging
uses the operating system's window move operation. Right-click opens settings;
closing the character hides only that window. Closing the main window hides it
when the system tray is available; organization work and character playback
continue. The tray's explicit exit closes both after active operations finish.
Hidden playback is paused and sidebar playback is paused while the desktop
character is shown. Transparency currently does not imply per-pixel input
pass-through. Explorer anchoring is not implemented.

`AppLifecycleController` separates window hiding from process exit using injected
availability/busy values and presentation signals. The composition root connects
native tray actions, window visibility and operation notifications. It does not
give the character or lifecycle controller access to file workers. Losing tray
availability restores the main window; without a tray, idle close requests exit
and busy close remains blocked. Native tray UI uses Qt Widgets alongside Qt Quick;
the Qt-independent core and pack format are unchanged. Startup registration and
automatic restart are not included.

## Speech-bubble configuration

ImagePack v1 now accepts an optional top-level `bubble` visual module. Existing
v1 packs that omit it still load unchanged and use the app-provided skin.
This does not introduce dialogue or feature execution into the pack; a future
CharacterPack container may reuse this visual module alongside other modules.

```json
"bubble": {
  "attachment": { "x": 0, "y": -120 },
  "pivot": { "x": 0.1, "y": 1 },
  "skin": { "kind": "default" }
}
```

Both `bubble` and each of its three fields are optional. Defaults are shown
above. Omit `skin` or select `default` to keep the app skin while changing only
the attachment point. Explicit null values are not allowed.

There are TWO independent coordinate systems:

- `attachment`: character-local logical units relative to the character pivot.
  Values must be finite and within [-4096,4096]. Personal character scale applies
  to this offset. It is not a PNG pixel coordinate or an absolute atlas position.
- `pivot`: normalized coordinates INSIDE the bubble's full canvas, including
  its tail. Values are within [0,1]. (0,0) is top-left, (1,1) is bottom-right.
  Usually this point is the tail tip. Normalization supports the variable-height
  app skin without changing its pivot whenever the text wraps. For the app skin,
  y=1 is recommended; x controls the tail's horizontal position.

Placement, before screen clamping:

```
A = characterGlobalPivot + characterUserScale * bubble.attachment
P = (bubble.pivot.x * renderedWidth, bubble.pivot.y * renderedHeight)
bubbleTopLeft = A - P
```

Horizontal flip transforms pivot.x to 1-pivot.x; vertical flip transforms
pivot.y to 1-pivot.y. Choose the orientation with less overflow, then clamp the
window inside the available monitor area. Full visibility wins if exact
alignment is impossible; screen clamping can therefore detach the tail tip
from A. The separate window never changes character position or sprite bounds.
Bubble size is UI size, not multiplied by personal character scale.

For a custom PNG, declare a `kind: "image"` entry in the existing `sources` list
(for example id `bubble-bg`, file `images/bubble.png`), then reference it:

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

Custom background sources must be whole PNG images, not atlas sources. They are
validated and copied by the same safe source pipeline, even if no character
animation references them. Logical size is independent of PNG pixel resolution:
width 100–1024, height 64–1024. Text/action rectangles use integer logical units,
must fit inside that size, must be at least 32x20 and must not overlap. An action
area is required even if a particular message has no action. The skin remains
fixed-height; text exceeding its area is clipped/elided. Current text is light
colored, so provide a sufficiently dark readable content area.

The background is stretched to the declared logical canvas, and that canvas may
shrink to fit a smaller work area. Flip the PNG and transform the content
rectangles, but do not mirror the text glyphs. Nine-slice scaling, atlas skins,
animated skins and per-animation overrides are not implemented. PNG decode
failure or changed dimensions falls back to an app-colored surface with the
configured canvas/content areas; header validation is not full pixel validation.

The composition in Main connects the generic action to opening an existing
watch proposal, rechecking proposal availability and operation state at click
time. It does not directly organize files. A 10-second placement test overrides
only displayed text and supplies a local dismiss button; it does not publish a
fake backend event or modify pending files. Dialogue packs and
dismiss/scheduling policies remain future work.
