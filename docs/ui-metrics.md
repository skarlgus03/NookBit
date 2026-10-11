# Shared UI metrics

The source of truth is `src/presentation/qml/UiMetrics.qml`, a presentation-only QML singleton. Core services and use cases must not depend on it. The native application font also reads `textBody` from this singleton.

`fontFamily` prefers installed Noto Sans KR; `fontFallback` is Malgun Gothic, followed by the platform's original default. No font files are bundled or downloaded. The application font is inherited by ordinary QML text and native menus/dialogs. `--verify-ui` reports the resolved family so a missing font is not mistaken for a successful substitution.

## Roles

| Role | Token | Initial value |
| --- | --- | --- |
| Body text / standard button | `textBody` | 14 px |
| Supporting text | `textCaption` | 14 px |
| Primary action label | `textAction` | 15 px |
| Emphasized content | `textEmphasis` | 16 px |
| Window title | `textWindowTitle` | 17 px |
| Section title | `textSectionTitle` | 18 px |
| Application title | `textAppTitle` | 25 px |
| Page title | `textPageTitle` | 28 px |
| Standard field / command | `controlHeight` | 40 px |
| Primary action | `primaryActionHeight` | 46 px |
| Paired file actions | `largeActionHeight` | 60 px |
| Home navigation | `navigationHeight` | 48 px |
| Window button | `windowButtonSize` | 32 px |

These are initial application decisions, not universal UX standards. Choose a semantic role rather than a token that happens to have the desired number. Body and caption may currently share a size but can be tuned independently later.

Use `AppButton`, `AppComboBox`, `AppTextField`, and `AppSpinBox` for ordinary controls. Specialized themed buttons must also read the shared size tokens. Use `controlPadding`, `buttonPadding`, spacing, radius, and border tokens instead of inline common metrics. Icons use `iconSize`; dropdown indicators use their own shared width and height.

Pending rows use `filePreviewSize` (48 px), with a `fileRowHeight` of 64 px. The presentation-only preview provider asynchronously decodes a small first-frame thumbnail for PNG/JPEG/WebP/BMP/GIF, honors image orientation, and preserves aspect ratio. To bound work, originals larger than 32 MiB or 16 megapixels are not decoded. Corrupt, missing, unsupported, or oversized images fall back to file-type icons. Thumbnails are not persisted to disk.

The underlying icon provider fetches Windows association icons using extension-based synthetic names and `SHGFI_USEFILEATTRIBUTES`, without reading real-file content. It caches at most 128 file types and falls back to a generic document icon. Core file classification and movement are not involved. Dragging files over the page shows a translucent white veil and a readable centered instruction; it does not alter the ordinary button hover style.

## Deliberate exceptions

Not every number is a design token. Window positioning and sizes, canvas coordinates, hit testing, image crop rectangles, animation timing, and ImagePack pivots/scales are feature geometry or data, not shared control metrics. Do not mechanically replace these with unrelated spacing tokens. Character drawing proportions also remain local.

Node text/width/height presets belong to the singleton but remain independent of ordinary form controls. Custom pack-defined bubble geometry remains authoritative; its fallback text uses the shared typography.

## Verification

`nookbit_ui_metrics_contract` rejects inline font sizes, positive control padding/spacing, and raw ordinary controls outside the shared components. `nookbit_qml_smoke` compares rendered dropdown/number-field heights and arrow dimensions to the singleton and checks menu opening, rule edits, keyboard entry, theme switching, and page closing.

After changing metrics, run both tests and visually check long labels, light/dark themes, node presets, and display scaling.

## Page window sizes

`extensionDefaults` defines initial/minimum width and height for each page. The rule editor starts wider than simple settings pages. Drag the extension window's right/bottom edge or bottom-right corner to resize; the top-left position continues to follow the main window.

File sorting uses input, preview, confirmation, execution, and result panes inside one window. A transition slides only its source and target panes, so jumping directly to execution or back to input never passes through unrelated intermediate screens. `pageSlideDuration` controls the transition (180 ms; 0 disables motion). Input is disabled while transitioning and during execution. Pending files, destination text, and input scroll state are retained. Preview/result back buttons and Escape return to input; active execution cannot be dismissed this way. Confirmation uses explicit cancellation to return to its invoking pane. Page ID 2 remains retired.

Manual sorting now includes a centered `FileSortConfirmation` pane between preparation and execution. It displays the controller's frozen source/destination pairs, not a newly recomputed plan. Confirm calls the existing `confirmSort`; cancel/back/Escape calls `cancelSort` and restores the invoking input/preview pane. Switching away while awaiting inline confirmation cancels it. Only requests armed by `FileSortPage` use this route; other explicit workflows retain their existing confirmation dialog. Pending confirmation disables main navigation while keeping inline confirmation controls usable. It never grants approval by opening the pane.

The compact home groups file sorting/rules/history under file management and settings under app controls. Buttons use shared `NavigationIcon` drawings, `textNavigation`, and `mainNavigationHeight`. `selectedPage` follows the actual external page (settings subpages map to settings) and its background fades with `selectionFadeDuration`. The menu scrolls at short window heights. Work/status content is centered within a bounded width instead of spreading a few labels across the entire pane; real progress remains authoritative, with no fake movement animation.

`PreviewController` publishes execution-started and execution-finished notifications after actual result state is applied. `FileSortPage` only responds when it armed a manual request from this page; notifications from unrelated/background work never open or navigate the window. Preparation and final approval remain existing separate steps. Execution displays real progress; the result distinguishes success/failure without manufacturing completion percentages. Successful results can invoke existing undo and return to input for its feedback. Failure results refer to the queue and history rather than offering undo of an unrelated older operation. Background character notifications are unchanged.

`BackButton` draws a shared arrow, has a standard hit area, and supplies tooltip/accessibility text instead of a long visible return label. It is used for file-flow back actions and settings subpages. The input page and its drop instruction have no nested decorative frame; content-specific list cards remain.

`UiSettingsController` stores each page's last size under a stable `windows/<page-name>/size` QSettings key, separate from rule JSON. Resizing is debounced by 300 ms; page changes, closing the extension, hiding the main window, and tray exit immediately commit pending changes. Reopening a page restores its own size, independent of the main window's height. Defaults and restored sizes are bounded by the current screen's available dimensions where compatible with the page's minimum size; docking positions are not clamped to a single monitor.

Invalid or extreme stored sizes fall back to the page default. `--verify-ui` remembers test sizes only in memory and never reads or overwrites personal window settings. Persistence tests use a temporary INI file; smoke tests check page-specific restoration, close/reopen, and independent main-window resizing.
