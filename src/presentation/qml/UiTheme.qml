pragma Singleton
import QtQuick

// 공통 기본색에서 역할별 색을 파생한다. 화면은 색상값 대신 이 역할을 사용한다.
QtObject {
    readonly property color white: "#ffffff"
    readonly property var lightPalette: ({
        window: "#f3f3f7", surface: white, raised: "#ecebf3",
        text: "#24233a", muted: "#615f78", border: "#e0dfea", grid: "#e6e5ee",
        accent: "#3e3a6e", hover: "#4a4683", pressed: "#2f2c57", onAccent: white,
        accentSoft: "#e6e5f4", focus: "#7a74c4", pressedSoft: "#d3d1ec",
        navigationHover: "#dedce9", navigationPressed: "#c9c5df",
        success: "#28764e", successSoft: "#edf7f0",
        warning: "#946020", warningSoft: "#fff4df",
        danger: "#ac423a", dangerSoft: "#fff0ee"
    })
    readonly property var darkPalette: ({
        window: "#16161f", surface: "#20202c", raised: "#2a2a3a",
        text: "#ecebf4", muted: "#a6a4bc", border: "#33334a", grid: "#262636",
        accent: "#a9b0f0", hover: "#bcc2f4", pressed: "#8f97e0", onAccent: "#16163a",
        accentSoft: "#2a2a48", focus: "#c3c8f7", pressedSoft: "#34345a",
        success: "#8eccaa", successSoft: "#203d31",
        warning: "#e8bf7c", warningSoft: "#463720",
        danger: "#f1a79c", dangerSoft: "#432b32"
    })
    // 노드의 종류는 기능 구분에 필요한 색만 사용하며 두 테마에서 공유한다.
    readonly property var nodePalette: ({
        start: "#24624d", extension: "#286c9c", size: "#986126", name: "#7750a2",
        age: "#237c83", otherwise: "#596575", destination: "#39734c", exclude: "#944b68",
        fallback: "#53677e"
    })
    readonly property color lightAccentColor: lightPalette.accent
    readonly property color darkAccentColor: darkPalette.accent
    readonly property var light: buildTheme(lightPalette, false)
    readonly property var dark: buildTheme(darkPalette, true)

    // 기본 팔레트를 화면·상태·버튼·캔버스가 공유할 의미별 색상표로 변환하는 함수.
    function buildTheme(p, isDark) {
        const accentColor = isDark ? darkAccentColor : lightAccentColor
        return {
            window: p.window, surface: p.surface, alternate: p.raised, home: p.window,
            inspectionVeil: white, text: p.text, contentText: p.text, muted: p.muted, homeMuted: p.muted,
            border: p.border, sectionBorder: p.border, button: p.raised,
            navigationText: p.text, navigationButton: p.raised, navigationSelected: p.accentSoft,
            navigationBorder: p.border, navigationFocus: p.focus,
            navigationActiveText: p.accent,
            navigationHover: p.navigationHover || p.raised,
            navigationPressed: p.navigationPressed || p.pressedSoft,
            menuSelected: p.accentSoft, menuBorder: p.border, menuFocus: p.focus,
            accent: p.accent, accentText: p.onAccent, accentSoftText: p.accent, primaryFocus: p.focus,
            buttonHover: p.accentSoft, buttonPressed: p.pressedSoft, buttonChecked: p.accentSoft,
            buttonBorder: p.border, buttonFocus: p.focus, primaryHover: p.hover, primaryPressed: p.pressed,
            editorAccent: p.accent, editorTint: p.accentSoft,
            editorCanvas: p.window, editorGrid: p.grid,
            success: p.success, successSurface: p.successSoft, successBorder: p.success,
            warning: p.warning, warningSurface: p.warningSoft, danger: p.danger, dangerSurface: p.dangerSoft,
            bubbleSurface: p.surface, bubbleBorder: p.border, bubbleAction: p.accent,
            nodeSurface: p.surface, nodeKinds: nodePalette, onNode: white,
            edge: p.accent, edgePending: p.muted,
            edgeDisconnect: p.warning, edgeTrace: p.success, snap: p.success,
            nodeInputSurface: p.successSoft, nodeInputBorder: p.success, nodeInputIcon: nodePalette.start,
            nodeInputText: p.text, nodeInputMuted: p.muted,
            pivotGuide: p.accent, bubbleGuide: p.warning,
            marquee: Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.2)
        }
    }
    // 지정한 밝기의 역할별 색상 규격을 반환하는 함수.
    function colors(darkMode) { return darkMode ? dark : light }
}
