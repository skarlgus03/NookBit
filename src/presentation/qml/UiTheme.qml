pragma Singleton
import QtQuick

// 공통 기본색에서 역할별 색을 파생한다. 화면은 색상값 대신 이 역할을 사용한다.
QtObject {
    readonly property color white: "#ffffff"
    readonly property var lightPalette: ({
        window: "#f5f7fa", surface: white, raised: "#eef2f7",
        text: "#243247", muted: "#64748b", border: "#d8e0ea", grid: "#e3e9f1",
        accent: "#426fb0", hover: "#365e99", pressed: "#2b4d80",
        accentSoft: "#e8effa", focus: "#6f9bd3", pressedSoft: "#cddcf2",
        success: "#28764e", successSoft: "#edf7f0",
        warning: "#946020", warningSoft: "#fff4df",
        danger: "#ac423a", dangerSoft: "#fff0ee"
    })
    readonly property var darkPalette: ({
        window: "#161b24", surface: "#202938", raised: "#283448",
        text: "#edf2f8", muted: "#a6b4c8", border: "#3c4b62", grid: "#29364a",
        accent: "#426fb0", hover: "#527fbe", pressed: "#33598f",
        accentSoft: "#263a57", focus: "#91b8ed", pressedSoft: "#314c70",
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
    readonly property color characterPivotGuide: lightPalette.accent
    readonly property color characterBubbleGuide: lightPalette.warning
    readonly property var light: buildTheme(lightPalette, false)
    readonly property var dark: buildTheme(darkPalette, true)

    // 기본 팔레트를 화면·상태·버튼·캔버스가 공유할 의미별 색상표로 변환하는 함수.
    function buildTheme(p, isDark) {
        return {
            window: p.window, surface: p.surface, alternate: p.raised, home: p.window,
            inspectionVeil: white, text: p.text, contentText: p.text, muted: p.muted, homeMuted: p.muted,
            border: p.border, sectionBorder: p.border, button: p.raised,
            navigationText: p.text, navigationButton: p.raised, navigationSelected: p.accentSoft,
            navigationBorder: p.border, navigationFocus: p.focus,
            navigationActiveText: isDark ? p.focus : p.accent,
            menuSelected: p.accentSoft, menuBorder: p.border, menuFocus: p.focus,
            accent: p.accent, accentText: white, primaryFocus: p.focus,
            buttonHover: p.accentSoft, buttonPressed: p.pressedSoft, buttonChecked: p.accentSoft,
            buttonBorder: p.border, buttonFocus: p.focus, primaryHover: p.hover, primaryPressed: p.pressed,
            editorAccent: isDark ? p.focus : p.accent, editorTint: p.accentSoft,
            editorCanvas: p.window, editorGrid: p.grid,
            success: p.success, successSurface: p.successSoft, successBorder: p.success,
            warning: p.warning, warningSurface: p.warningSoft, danger: p.danger, dangerSurface: p.dangerSoft,
            bubbleSurface: p.surface, bubbleBorder: p.border, bubbleAction: p.accent,
            nodeSurface: p.surface, nodeKinds: nodePalette, onNode: white,
            edge: isDark ? p.focus : p.accent, edgePending: p.muted,
            edgeDisconnect: p.warning, edgeTrace: p.success, snap: p.success,
            nodeInputSurface: p.successSoft, nodeInputBorder: p.success, nodeInputIcon: nodePalette.start,
            nodeInputText: p.text, nodeInputMuted: p.muted,
            marquee: Qt.rgba(0.26, 0.44, 0.69, 0.2)
        }
    }
    // 지정한 밝기의 역할별 색상 규격을 반환하는 함수.
    function colors(darkMode) { return darkMode ? dark : light }
}
