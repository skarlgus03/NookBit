import QtQuick

// 버튼 종류와 무관하게 같은 호버·누름·클릭 확인 반응을 제공하는 배경.
Rectangle {
    id: feedback
    objectName: "buttonFeedbackSurface"
    required property var button
    property bool primary: false
    property bool selected: false
    property color normalColor: button.palette.button
    property color selectedColor: theme.buttonChecked
    property color hoverColor: theme.buttonHover
    property bool showBorder: true
    property color idleBorder: theme.buttonBorder
    property bool recentClick: false
    readonly property var theme: UiTheme.colors(button.dark)
    readonly property bool pushed: (button.enabled && button.down) || recentClick
    readonly property bool chosen: selected || button.checked || button.highlighted
    readonly property bool emphasized: button.enabled && (button.hovered || button.visualFocus || chosen)
    implicitWidth: UiMetrics.buttonDefaultWidth
    implicitHeight: UiMetrics.controlHeight
    radius: UiMetrics.radiusControl
    color: !button.enabled && !recentClick ? (button.flat ? "transparent" : normalColor)
        : pushed ? (primary ? theme.primaryPressed : theme.buttonPressed)
        : primary ? (button.hovered ? theme.primaryHover : theme.accent)
        : chosen ? selectedColor : button.hovered || button.visualFocus ? hoverColor : button.flat ? "transparent" : normalColor
    opacity: button.enabled || recentClick ? 1 : 0.5
    border.color: !showBorder ? "transparent" : pushed || emphasized ? theme.buttonFocus : button.flat ? "transparent" : idleBorder
    border.width: !showBorder ? 0 : pushed || emphasized ? UiMetrics.borderActive : UiMetrics.borderNormal
    scale: pushed ? UiMetrics.buttonPressedScale : 1
    transform: Translate {
        y: feedback.pushed ? UiMetrics.buttonPressedOffset : 0
        Behavior on y { NumberAnimation { duration: feedback.pushed ? 0 : UiMetrics.buttonReleaseMs } }
    }
    Behavior on color { ColorAnimation { duration: feedback.pushed ? 0 : UiMetrics.buttonTransitionMs } }
    Behavior on scale { NumberAnimation { duration: feedback.pushed ? 0 : UiMetrics.buttonReleaseMs } }
    Rectangle {
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        anchors.margins: UiMetrics.spaceTiny
        height: UiMetrics.borderActive
        color: feedback.theme.buttonFocus
        opacity: feedback.pushed ? 0.5 : 0
        radius: parent.radius
    }
    Connections {
        target: feedback.button
        // 짧은 클릭도 인지할 수 있도록 입력 확인을 잠깐 유지하는 함수.
        function onClicked() { feedback.recentClick = true; clickPulse.restart() }
    }
    Timer {
        id: clickPulse
        interval: UiMetrics.buttonClickPulseMs
        onTriggered: feedback.recentClick = false
    }
}
