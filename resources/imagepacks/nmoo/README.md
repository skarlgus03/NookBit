# Nmoo ImagePack

The bundled pack is named **Nmoo - Idle, Walk & Work**. Its own version is recorded in
`imagepack.json`; it is independent of the NookBit application version.

## Use

Open the Companion page and select **Nmoo - Idle, Walk & Work** in the pack list.
The installed application includes this folder at `imagepacks/nmoo` beside the
executable. You can also use the pack-import command to import a separate copy.
An imported personal copy is not automatically replaced when this bundled pack
is updated. Selection, size, placement and frame edits remain personal settings.

## Animation data

- `standing`: eyes open for 3,000 ms, then closed for 150 ms, looping.
- `walk`: frames 1, 2, 3, 2 at 140 ms per step, looping.
- `notice`: a single notice frame with a 1,000 ms duration, looping.
- `work-placeholder`: the original stable animation ID now plays eight dedicated
  organizing PNGs in order, at 250 ms each (2 seconds per loop). Both `working`
  and `file.work` select it. The historical ID is retained for personal-edit
  compatibility. Adjust step durations in the pack or Companion frame editor.
- Work frames use the same 1024 × 1024 canvas and `pixelsPerUnit: 4`. Their shared
  logical pivot `(128, 230)` places the table base at the desktop contact point;
  standing/walking keep their existing pivots. Source images are copied unchanged.
- `complete-placeholder`: a short, non-looping standing/blink completion cue.
- Resting uses `standing`; calling and file offers use `notice`; left/right
  walking use `walk`. Actual file work repeats the organizing clip until it ends;
  completion and failure keep their separate existing cues.

The manifest is authoritative for frame geometry, individual pivots, display
scale, filtering, cue bindings and speech-bubble attachment defaults. App-level
walking and interactions decide when and where to move; this pack only provides
visual frames. It does not execute file operations.
