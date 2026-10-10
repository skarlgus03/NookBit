# Shared UI metrics

The source of truth is `src/presentation/qml/UiMetrics.qml`, a presentation-only QML singleton. Core services and use cases must not depend on it. The native application font also reads `textBody` from this singleton.

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
| Home navigation | `navigationHeight` | 48 px |
| Window button | `windowButtonSize` | 32 px |

These are initial application decisions, not universal UX standards. Choose a semantic role rather than a token that happens to have the desired number. Body and caption may currently share a size but can be tuned independently later.

Use `AppButton`, `AppComboBox`, `AppTextField`, and `AppSpinBox` for ordinary controls. Specialized themed buttons must also read the shared size tokens. Use `controlPadding`, `buttonPadding`, spacing, radius, and border tokens instead of inline common metrics. Icons use `iconSize`; dropdown indicators use their own shared width and height.

Pending file rows use `fileIconSize` (32 px) for file-type icons. The presentation-only image provider fetches Windows association icons using extension-based synthetic names and `SHGFI_USEFILEATTRIBUTES`, not real-file content or thumbnails. It loads asynchronously, caches at most 128 file types, and falls back to a generic document icon. Core file classification and movement are not involved.

## Deliberate exceptions

Not every number is a design token. Window positioning and sizes, canvas coordinates, hit testing, image crop rectangles, animation timing, and ImagePack pivots/scales are feature geometry or data, not shared control metrics. Do not mechanically replace these with unrelated spacing tokens. Character drawing proportions also remain local.

Node text/width/height presets belong to the singleton but remain independent of ordinary form controls. Custom pack-defined bubble geometry remains authoritative; its fallback text uses the shared typography.

## Verification

`nookbit_ui_metrics_contract` rejects inline font sizes, positive control padding/spacing, and raw ordinary controls outside the shared components. `nookbit_qml_smoke` compares rendered dropdown/number-field heights and arrow dimensions to the singleton and checks menu opening, rule edits, keyboard entry, theme switching, and page closing.

After changing metrics, run both tests and visually check long labels, light/dark themes, node presets, and display scaling.

## Page window sizes

`extensionDefaults` defines initial/minimum width and height for each page. The rule editor starts wider than simple settings pages. Drag the extension window's right/bottom edge or bottom-right corner to resize; the top-left position continues to follow the main window.

`UiSettingsController` stores each page's last size under a stable `windows/<page-name>/size` QSettings key, separate from rule JSON. Resizing is debounced by 300 ms; page changes, closing the extension, hiding the main window, and tray exit immediately commit pending changes. Reopening a page restores its own size, independent of the main window's height. Defaults and restored sizes are bounded by the current screen's available dimensions where compatible with the page's minimum size; docking positions are not clamped to a single monitor.

Invalid or extreme stored sizes fall back to the page default. `--verify-ui` remembers test sizes only in memory and never reads or overwrites personal window settings. Persistence tests use a temporary INI file; smoke tests check page-specific restoration, close/reopen, and independent main-window resizing.
