# Companion feeding module

`companion-feeding` is a small optional feature requiring `companion`. It adds
**먹이 주기** to the desktop companion's right-click menu. The same menu retains
**컴패니언 설정**, even when no optional interaction feature is active.

Selecting the action submits `companion.feed` through the existing interaction
controller/channel. The handler returns cue `interact.feed`, a short Korean
message, and a 2,400 ms reaction duration. The existing reaction timer clears it
and reveals the current base behavior. Autonomous walking continues according to
the existing idle/walk policy. Opening a context menu holds autonomous movement
until the menu closes.

The action is enabled only while the target is visible and resting or walking,
with no gesture session or current reaction. Work and attention prevent starting
feeding. Repeated requests are rejected without clearing or extending the current
reaction. A work/attention fact arriving after feeding starts still updates the
base behavior underneath the short reaction; feeding does not pause backend work.
Hiding the companion or changing packs cancels the reaction as for other inputs.
There is no hunger, inventory, cooldown database, growth statistic, food sprite,
file-operation dependency, additional worker or independent feature timer.

## Pack binding

An existing ImagePack works without modifications. If it has no `interact.feed`
binding, normal character artwork is used with the feeding message. A pack author
can add a binding to any authored animation:

```json
{"cueId":"interact.feed","animationId":"eat"}
```

The referenced `eat` animation and its frames must be defined normally. The
reaction lasts 2,400 ms regardless of clip length, so design the animation for
that window. This adds an optional cue, not a new ImagePack schema version.

## Ownership and extension

`IInteractionRegistry` is a standard C++ registration capability separate from
`IInteractionChannel` input submission. A registration includes an InteractionType,
handler, optional context predicate and a flag for menu visibility. The manager
validates registration and input; predicate exceptions mean unavailable.
Only one-shot actions may be menu entries. Existing click/drag/pet registrations
retain their IDs and are not added to the menu.

CompanionFeedingRuntime borrows only this registry and owns its successful
registration. It does not create or call a renderer, CharacterController, other
feature runtime or file worker. Failed duplicate registration does not unregister
another owner. FeatureRuntimeHost creates the owner only when active and destroys
it before CompanionRuntime, after the QML hosts have gone away. Registration
teardown is not live backend unloading.

The companion controller exposes generic menu rows from the manager. Character
state/reaction changes refresh availability. QML builds menu items from those rows
and submits their IDs; it contains no feeding-specific condition or cue selection.
Right-click menus use a separate popup window when supported by Qt to avoid
clipping against the small transparent companion window.

## Selection compatibility

Module management adds a fifth switch with a Companion dependency explanation.
Save and fully exit/relaunch to apply changes. Disabling feeding removes both
its runtime registration and menu entry. Disabling Companion leaves requested
feeding inactive and explains the dependency. Existing pack selections, edits,
history and file settings are retained.

The existing `modules/selection` JSON remains schemaVersion 1. New saves include
Boolean `enabled["companion-feeding"]`. Old documents with only the original four
keys remain valid: an absent feeding key follows the saved Companion flag without
writing the document. Thus previously enabled companions gain the optional menu,
while saved-disabled companions do not gain a pending feeding request. An explicit
new feeding value must be Boolean. `--disable-feature=companion-feeding` applies to
one run, and `--disable-feature=all` includes it.

## Verification

Tests cover reaction expiry, fallback/custom pack artwork, work/attention/hidden
target restrictions, rejected-repeat preservation, duplicate registration and
teardown, exception-safe availability, generic menu request routing and movement
hold while open. Startup and saved-selection coverage includes all 32 combinations
of the five requested features, alongside existing gesture and UI smoke checks.
