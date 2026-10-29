# Nmoo ImagePack

The bundled pack is named **Nmoo - Idle & Walk**. Its own version is recorded in
`imagepack.json`; it is independent of the NookBit application version.

## Use

Open the Companion page and select **Nmoo - Idle & Walk** in the pack list.
The installed application includes this folder at `imagepacks/nmoo` beside the
executable. You can also use the pack-import command to import a separate copy.
An imported personal copy is not automatically replaced when this bundled pack
is updated. Selection, size, placement and frame edits remain personal settings.

## Animation data

- `standing`: eyes open for 3,000 ms, then closed for 150 ms, looping.
- `walk`: frames 1, 2, 3, 2 at 140 ms per step, looping.
- `notice`: a single notice frame with a 1,000 ms duration, looping.
- Resting uses `standing`; calling and file offers use `notice`; left/right
  walking use `walk`. Working currently falls back to `standing`.

The manifest is authoritative for frame geometry, individual pivots, display
scale, filtering, cue bindings and speech-bubble attachment defaults. App-level
walking and interactions decide when and where to move; this pack only provides
visual frames. It does not execute file operations.
