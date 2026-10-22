pragma Singleton
import QtQuick

// 공통 화면·설정·버튼과 네이티브 메뉴가 공유하는 역할별 테마 색상표.
QtObject {
    readonly property color characterPivotGuide: "#3985df"
    readonly property color characterBubbleGuide: "#da8c32"
    readonly property var light: ({
        window: "#f5f7fa", surface: "#ffffff", alternate: "#f5f7fa", inspectionVeil: "#ffffff",
        text: "#273344", contentText: "#27352b", muted: "#687586", homeMuted: "#748091",
        border: "#e1e6ed", sectionBorder: "#d9dfd8", button: "#edf1f6", home: "#f5f7fa",
        navigationText: "#253428", navigationButton: "#f3f5f1", navigationSelected: "#dce9db",
        navigationBorder: "#d9dfd4", navigationFocus: "#7da58a",
        menuSelected: "#e6eef7", menuBorder: "#d7dfe9", menuFocus: "#7898bb",
        accent: "#405e80", accentText: "#ffffff", primaryFocus: "#88a8c9"
    })
    readonly property var dark: ({
        window: "#161a22", surface: "#202633", alternate: "#242d3b", inspectionVeil: "#ffffff",
        text: "#edf2f8", contentText: "#edf2f8", muted: "#aab6c7", homeMuted: "#aab6c7",
        border: "#3d4757", sectionBorder: "#3d4757", button: "#2b3546", home: "#1b2230",
        navigationText: "#edf2f8", navigationButton: "#2b3546", navigationSelected: "#3e5270",
        navigationBorder: "#3d4757", navigationFocus: "#86aad6",
        menuSelected: "#2d3d55", menuBorder: "#3d4757", menuFocus: "#7898bb",
        accent: "#405e80", accentText: "#ffffff", primaryFocus: "#88a8c9"
    })
    // 지정한 밝기의 역할별 색상 규격을 반환하는 함수.
    function colors(darkMode) { return darkMode ? dark : light }
}
