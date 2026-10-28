# Shared UI metrics

The source of truth is `src/presentation/qml/UiMetrics.qml`, a presentation-only QML singleton. Core services and use cases must not depend on it. The native application font also reads `textBody` from this singleton.

Shared colors live separately in `UiTheme.qml`. Light/dark foundation palettes
derive semantic roles for surfaces, text, borders, companion-inspired navy/lavender
accents and status feedback.
All QML pages, rule nodes and default companion bubbles consume these roles; hex
colors outside `UiTheme.qml` fail the contract test. Rule-node categories retain a
small shared categorical palette; success/warning/error colors convey state.
Pack-provided PNG skins are not recolored. App inputs and native menus inherit the
same palette. Theme is presentation-only; no backend dependency is introduced.

The light accent is hood navy (`#3e3a6e`) with white `onAccent`; the dark accent is
eye lavender (`#a9b0f0`) with dark `onAccent` (`#16163a`). `accentText` derives from
that foreground, including hover and press. `accentSoftText` is for soft accent
backgrounds. Focus uses its own brighter outline, while navigation, editor emphasis
and wires use the actual accent. Selection marquee RGB derives from a typed accent
color and uses alpha 0.2; palette strings must not be accessed as `.r`/`.g`/`.b`.
Companion contact/bubble guides use theme-aware `pivotGuide` and `bubbleGuide` roles.

Status colors and categorical node colors retain their existing values. Light
warning text on the proposed raised surface measures about 4.49:1, so the developer
preview's warning rows now use `warningSurface` (about 4.87:1). New status text should
use its semantic surface rather than placing warning labels on raised surfaces.
Primary labels on their normal accents measure about 10.32:1 light / 8.39:1 dark.
The only color arithmetic outside the theme is the file-drop overlay: it changes
the alpha of `UiTheme.white` using the shared overlay opacity, without adding a hue.

`fontFamily` prefers installed Noto Sans KR; `fontFallback` is Malgun Gothic, followed by the platform's original default. No font files are bundled or downloaded. The application font is inherited by ordinary QML text and native menus/dialogs. `--verify-ui` reports the resolved family so a missing font is not mistaken for a successful substitution.

## Roles

| Role | Token | Initial value |
| --- | --- | --- |
| Body text / standard button | `textBody` | 14 px |
| Supporting text | `textCaption` | 14 px |
| Primary action label | `textAction` | 14 px |
| Emphasized content | `textEmphasis` | 16 px |
| Window title | `textWindowTitle` | 20 px |
| Section title | `textSectionTitle` | 20 px |
| Application title | `textAppTitle` | 28 px |
| Page title | `textPageTitle` | 28 px |
| Standard field / command | `controlHeight` | 40 px |
| Primary action | `primaryActionHeight` | 46 px |
| Paired file actions | `largeActionHeight` | 60 px |
| Home navigation | `navigationHeight` | 48 px |
| Home companion portrait | `homePortraitSize` | 64 px |
| Home pending-count badge | `homeBadgeHeight` | 22 px |
| Window button | `windowButtonSize` | 32 px |

Typography uses only four foundation sizes: `typeBody` (14), `typeEmphasis` (16),
`typeTitle` (20), and `typeDisplay` (28). Semantic names alias these sizes; change
the foundation rather than adding intermediate values. Supporting text uses muted
color, while titles use weight and spacing for hierarchy. These are application
decisions, not universal UX standards. Choose a semantic role for each use.

Use `AppButton`, `AppComboBox`, `AppTextField`, and `AppSpinBox` for ordinary controls. Specialized themed buttons must also read the shared size tokens. Use `controlPadding`, `buttonPadding`, spacing, radius, and border tokens instead of inline common metrics. Icons use `iconSize`; dropdown indicators use their own shared width and height.

Use `LargeActionButton` for preview/sort and cancel/confirm pairs. It owns their common 60 px height, typography, padding, radius, and focus border. `primary` changes only emphasis, not size. Paired instances use equal preferred widths. Smoke tests compare confirmation button dimensions to the actual preview button, avoiding a token-only check that could miss a smaller generic button.

## Button feedback

`AppButton` is the only factory for `ButtonFeedback`. Large actions, home menus,
file-page buttons, navigation and rule-editor commands inherit it without replacing
their backgrounds. `appearance` selects `Standard`, `Navigation`, `Menu`, `Surface`, or `Soft`;
`primary` selects a primary action, `selected` contributes to persistent selection,
`accentOutline` requests an idle accent outline, and `backgroundRadius` changes only
shape. Pages do not supply hover/pressed colors. The contract test rejects separate
`ButtonFeedback` backgrounds outside `AppButton`.

`ButtonFeedback` resolves the actual state using `button.down`, hover, keyboard
focus, selection and enabled state. Colors come from `buttonTheme` in `UiTheme`.
One opaque interaction layer fades over the ordinary background. Its RGB never
interpolates from transparent black: only its opacity changes. This avoids the dark
intermediate flash that can occur when animating a transparent navigation background
directly toward a pale hover color. There is no color or geometry animation on the
button background itself.

Ordinary commands use a full outline; pointer hover/press on home navigation remains
borderless. Keyboard focus can show a full focus ring. Selected items retain their
fill while hovered. Primary and selected navigation foregrounds are resolved in
`AppButton`, including disabled text. Numeric +/- indicators keep their own input
behavior and use the same theme hover/press tokens.

Home navigation is borderless: 24 px icon and 16 px label are both left aligned,
separated by `spaceGroup`, in a 48 px row. Unselected rows have no fill; hover adds a
neutral surface, and the selected row keeps a subtle navy/lavender rounded fill and accent
foreground. Keyboard focus and press feedback remain visible. Menu command names
do not include manually appended ellipses; genuine constrained text can still elide
with a full-text tooltip.

Home uses dedicated `navigationHover` / `navigationSelected` / `navigationPressed`
colors. Light fills are `#dedce9` / `#d4d0e6` / `#c9c5df`: selection is stronger than
hover and softer than pressing. Dark mode keeps raised/accentSoft/pressedSoft.

Soft actions use `softButton`, `softButtonHover`, and `softButtonPressed`, derived
from the existing soft accent and typed accent color in `UiTheme`; text uses
`accentSoftText`. Pages select the appearance without defining state colors.

The home status card uses surface/radiusCard, shared 16 px padding and 12 px gaps.
Its static 64 px portrait fits the current descriptor's first frame, including atlas
regions, pixels-per-unit, pivot and mirroring. It does not run an animation timer or
guess a face crop. Missing art uses the shared companion icon. The 40 px soft action
only requests the sorting page. The card and menu share one scroll area; the file
menu's count badge reserves label space and hides when the queue is empty.

`buttonTransitionMs` (90 ms) and `buttonReleaseMs` (120 ms) control the hover layer's
fade in/out. Press state is immediate and ends when the pointer is released; there
is no synthetic post-click timer. The top-edge click stripe, background scaling and
vertical translation have been removed. Button bounds and content remain still.
Smoke tests sample the whole hover transition for a stable opaque RGB and increasing
opacity, then send real pointer press/release events across five button types in both
themes. They also check selection, geometry, home outlines and disabled input state.

Pending rows use `filePreviewSize` (48 px), with a `fileRowHeight` of 64 px. The presentation-only preview provider asynchronously decodes a small first-frame thumbnail for PNG/JPEG/WebP/BMP/GIF, honors image orientation, and preserves aspect ratio. To bound work, originals larger than 32 MiB or 16 megapixels are not decoded. Corrupt, missing, unsupported, or oversized images fall back to file-type icons. Thumbnails are not persisted to disk.

The redesigned sorting input uses `pendingRowHeight` (56 px) and displays the same
cached previews at `pendingPreviewSize` (36 px). Names and paths reserve a flexible
column; extension labels use `pendingTypeWidth` (72 px). Full paths and notices have
hover tooltips. File sizes are not queried by the UI or invented from names.
Rules and destination share a two-column header; the list fills the remaining space
and scrolls independently. Its empty state owns the drop guidance. The fixed
`fileSortFooterHeight` (76 px) footer separates undo from equal-width preview/sort
actions, retaining `LargeActionButton` height (60 px). There is no new clear-all API.
The informational `FileSortStepper` has four stages, mapping execution and completion
to the final stage. Existing panes slide below it without changing workflow logic.

The rule editor uses shared 16 px page insets and an unaccented surface for the menu
strip. Its compact heading reports block/connection counts and receives unsaved
state from the page; it never performs persistence itself. The inspector width is
`ruleInspectorWidth` (280 px), with a selection summary of at least
`ruleSelectionHeaderHeight` (80 px). Condition inputs have type-specific labels,
comparison/unit headings and the common `AppTextField` focus/hover treatment.
The `ruleStatusHeight` (28 px) footer reserves a single stable line for status plus
selection count; truncated status is available in a full-text tooltip. Node geometry,
palette categories, camera behavior and connection semantics are unchanged.

The underlying icon provider fetches Windows association icons using extension-based synthetic names and `SHGFI_USEFILEATTRIBUTES`, without reading real-file content. It caches at most 128 file types and falls back to a generic document icon. Core file classification and movement are not involved. Dragging files over the page shows a translucent white veil and a readable centered instruction; it does not alter the ordinary button hover style.

## Progress, recovery and history pages

Inspection/undo panels use `operationPopupWidth` / `operationPopupHeight`, are
centered in the extension content, and apply `operationVeilOpacity` to the white
`inspectionVeil` theme role. The title area remains available for window movement.
Recovery is extension page 10 with its own saved window size; its explicit action
confirmation also belongs to that extension window, not the main window.
Read-only recovery-list refresh uses an inline indicator instead of the shared
white veil. Open the page before dispatching the inspection to avoid briefly
activating the previous page's progress popup. Resume/rollback retain the popup.

History row width/height use `historyListWidth` (300 px) / `historyRowHeight` (76 px).
Whole rows use common navigation feedback; only the selected row retains a fill.
Date headings use `textBody` / `groupTitleWeight`. The controller adds `dateGroup` and
`startsDateGroup` to existing plan rows; it does not insert fake records or change
database schemas, plan IDs, selection or undo contracts. Grouping uses the last
activity timestamp converted to local time: today, yesterday, this calendar week,
last calendar week (Monday start), then older records. Invalid/future dates have
explicit fallback labels. Refresh recalculates the headings, including after undo.

History uses a full-height two-pane layout divided by the shared border color.
The right pane shows the selected local time, semantic status, three summary cards
(`historySummaryHeight`, 76 px), and one surface for file rows. Counts use explicit
`applied` / `rolledBack` flags, not translated status strings. Each file shows its
extension, name, source folder, destination folder and current status; full paths
are available on hover, and failure messages wrap without being hidden.
The bottom action area uses `historyFooterHeight` (76 px) and the shared soft undo
button, preserving the original confirmation dialog. Busy work disables refresh,
selection and undo. No inactive search or location-opening controls are shown.

History file tiles use the reusable `FileTypeBadge`, with case-insensitive extension
groups: images (orange), documents including PDF (blue), archives (purple), videos
(rose), audio (teal), and other/extensionless files (neutral). Extension letters stay
visible so color is not the only cue. `UiTheme` owns each light/dark background and
foreground pair under `fileTypes`; the neutral pair derives from raised/muted roles.
The text/background pairs are checked for at least 4.5:1 contrast. Classification is
presentation-only and reads the filename, never file contents or organizing rules.

## Deliberate exceptions

Settings use `SettingsSection`: a `textSettingsGroup` (14 px) muted heading over a
surface card with shared 16 px padding and 12 px gaps. Theme selection explicitly
chooses light/dark instead of toggling the selected option. The sorting card shows
the current watch folder/conditions and three equal-width collision policy choices.
`CollisionPolicyPicker` is reused by the legacy detail page; its minimum card height
is `collisionChoiceMinHeight` (104 px), growing for wrapped descriptions.
Recovery warnings appear only when actual cases exist and request the existing
inspection screen. Allowed external paths keep full-text tooltips and the original
revoke action. Revoke uses `AppButton.destructive`, which derives foreground,
hover/press surface and focus outline from the shared danger roles.

Watch settings use the same cards and `AppSwitch` controls. Each numeric field
retains its range, keyboard editing, and checked-condition enablement. Draft edits
remain local until Save; Check Now still uses the saved configuration. The action
row remains below the scrollable form. UI verification scrolls each numeric field
into view before sending pointer/keyboard events.

Theme choices have a fixed group width plus an expanding spacer so the row fills
the card. Tests compare their position to the actual card edge at 500/720 px widths
in both themes, rather than merely checking the button group's immediate parent.
Switches use dedicated `switchTrackOn` / `switchTrackOff` and thumb roles: on uses
the accent; off uses a neutral gray track, muted thumb and a visible neutral border.
Track state colors and thumb/track pairs are checked for at least 3:1 contrast.
Settings and Watch share one window-size key (page 8); page IDs 8/3 remain
stable, but entering Watch no longer restores the old independent page-3 size.
`SettingsWorkflow` slides their bodies using `pageSlideDuration`, keeps input locked
during motion, and supports reverse navigation. Other page sizes remain independent.

The Watch dashboard reads `WatchController.lastEvaluation`: actual eligible count,
bytes, oldest age, threshold outcome and UTC check time. Missing results show
"before checking", not manufactured zero readings. It labels the saved-condition
snapshot separately from the local draft. Save/rule changes invalidate this snapshot.
Folder browsing only fills the draft path. Recheck presets are 1/5/15 minutes plus
direct entry (still 1–1440); compact condition inputs use `watchConditionInputWidth`
and `watchUnitWidth`. Left-indicator switches use the shared `indicatorOnLeft`
option. Exclusion chips remove only through their close button; long patterns elide
with a tooltip. Save commits pending typed patterns before calling the existing save
method, while Reset restores the saved draft. Persistence/evaluation policy is unchanged.

The companion page uses the theme window background with a large preview card and
separate playback card on the left, and a scrollable surface on the right. The
settings panel groups pack selection, display, placement and desktop gestures.
`AppSwitch` uses `switchWidth` / `switchHeight` / `switchThumbSize`; `AppSlider` uses
`sliderTrackHeight` / `sliderHandleSize`. Both inherit the shared 40 px control row
and theme roles, including disabled/focus or press feedback. The pack selector is a
deliberate 64 px row (`companionPackSelectorHeight`) to accommodate a static 40 px
portrait (`companionPackPortraitSize`) and wrapped
pack/version label. It stays locked while frame edits are unsaved.

Preview background dots use `companionPreviewGridSpacing` and the shared border
color, repainting only on size/theme changes. They do not affect camera coordinates.
Zoom controls share a small floating surface. Playback controls use shared 40 px
icon buttons in one row with a `companionPlaybackBadgeHeight` status pill and time
readout; the existing timeline remains below. Apply/cancel appear during editing or
while changes remain pending. Desktop scale (10–200%, default 50%) and preview zoom
remain independent. No pack metadata, pivots, timing or import contracts change.

Not every number is a design token. Window positioning and sizes, canvas coordinates, hit testing, image crop rectangles, animation timing, and ImagePack pivots/scales are feature geometry or data, not shared control metrics. Do not mechanically replace these with unrelated spacing tokens. Character drawing proportions also remain local.

Node text/width/height presets belong to the singleton. Node type presets reuse the
same four typography sizes without adding arithmetic offsets. Custom pack-defined
bubble geometry remains authoritative; its fallback text uses shared typography.

## Verification

`nookbit_ui_metrics_contract` rejects inline font sizes, positive control padding/spacing, and raw ordinary controls outside the shared components. `nookbit_qml_smoke` compares rendered dropdown/number-field heights and arrow dimensions to the singleton and checks menu opening, rule edits, keyboard entry, theme switching, and page closing.

`nookbit_qml_rule_editor_tests` also checks primary labels through normal/hover/press
in both palettes, semantic status surfaces, accent-derived marquee color, and
connection geometry at different zoom levels. These checks complement the user's
manual light/dark visual review; desktop screenshots are not captured automatically.

After changing metrics, run both tests and visually check long labels, light/dark themes, node presets, and display scaling.

## Page window sizes

`extensionDefaults` defines initial/minimum width and height for each page. The rule editor starts wider than simple settings pages. Drag the extension window's right/bottom edge or bottom-right corner to resize; the top-left position continues to follow the main window.

File sorting uses input, preview, confirmation, execution, and result panes inside one window. A transition slides only its source and target panes, so jumping directly to execution or back to input never passes through unrelated intermediate screens. `pageSlideDuration` controls the transition (180 ms; 0 disables motion). Input is disabled while transitioning and during execution. Pending files, destination text, and input scroll state are retained. Preview/result back buttons and Escape return to input; active execution cannot be dismissed this way. Confirmation uses explicit cancellation to return to its invoking pane. Page ID 2 remains retired.

Manual sorting now includes a centered `FileSortConfirmation` pane between preparation and execution. It displays the controller's frozen source/destination pairs, not a newly recomputed plan. Confirm calls the existing `confirmSort`; cancel/back/Escape calls `cancelSort` and restores the invoking input/preview pane. Switching away while awaiting inline confirmation cancels it. Only requests armed by `FileSortPage` use this route; other explicit workflows retain their existing confirmation dialog. Pending confirmation disables main navigation while keeping inline confirmation controls usable. It never grants approval by opening the pane.

The compact home groups file sorting/rules/history under file management and settings under app controls. Buttons use shared `NavigationIcon` drawings, `textNavigation`, and `mainNavigationHeight`. `selectedPage` follows the actual external page (settings subpages map to settings), with the shared button state colors and timing. The menu scrolls at short window heights. Work/status content is centered within a bounded width instead of spreading a few labels across the entire pane; real progress remains authoritative, with no fake movement animation.

Home menu buttons are borderless. Busy state disables navigation/business content, not the entire window content item: the main title drag region and resize edges remain usable. Inspection/undo progress is a non-modal, non-focusing popup owned by the extension window. Manual execution uses its own inline status pane instead. If the extension is hidden, progress does not open it or appear over the main home. Other dialogs keep explicit busy guards. Smoke tests hold a no-file background job to verify popup ownership, inspection/file-work routing, active title drag input, and disabled navigation.

`HomeMenuButton` owns the compact home's menu layout. Its 24 px icon and 16 px DemiBold label are left aligned with `spaceGroup` between them. Group titles also use the shared 16 px role. Tests measure icon/text coordinates on all six actual menu buttons.

`PreviewController` publishes execution-started and execution-finished notifications after actual result state is applied. `FileSortPage` only responds when it armed a manual request from this page; notifications from unrelated/background work never open or navigate the window. Preparation and final approval remain existing separate steps. Execution displays real progress; the result distinguishes success/failure without manufacturing completion percentages. Successful results can invoke existing undo and return to input for its feedback. Failure results refer to the queue and history rather than offering undo of an unrelated older operation. Background character notifications are unchanged.

`BackButton` draws a shared arrow, has a standard hit area, and supplies tooltip/accessibility text instead of a long visible return label. It is used for file-flow back actions and settings subpages. The input page and its drop instruction have no nested decorative frame; content-specific list cards remain.

`UiSettingsController` stores each page's last size under a stable `windows/<page-name>/size` QSettings key, separate from rule JSON. Resizing is debounced by 300 ms; page changes, closing the extension, hiding the main window, and tray exit immediately commit pending changes. Reopening a page restores its own size, independent of the main window's height. Defaults and restored sizes are bounded by the current screen's available dimensions where compatible with the page's minimum size; docking positions are not clamped to a single monitor.

Invalid or extreme stored sizes fall back to the page default. `--verify-ui` remembers test sizes only in memory and never reads or overwrites personal window settings. Persistence tests use a temporary INI file; smoke tests check page-specific restoration, close/reopen, and independent main-window resizing.
