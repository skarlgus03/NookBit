import QtQuick

// 버튼 상태 색을 한곳에서 결정하고 불투명한 반응 면만 페이드하는 공통 배경.
Rectangle {
    id: feedback
    objectName: "buttonFeedbackSurface"
    required property var button
    readonly property var theme: button.buttonTheme
    readonly property bool navigation: button.appearance === AppButton.Navigation
    readonly property bool menu: button.appearance === AppButton.Menu
    readonly property bool soft: button.appearance === AppButton.Soft
    readonly property bool borderless: navigation || menu || button.flat
    readonly property bool pushed: button.enabled && button.down
    readonly property bool chosen: button.activeSelection
    readonly property bool interactionActive: button.enabled && (pushed || chosen || button.hovered || button.visualFocus)
    readonly property bool outlineActive: button.enabled && (button.visualFocus || (!navigation && (pushed || chosen || button.hovered)))
    readonly property color interactionColor: pushed ? (button.primary ? theme.primaryPressed
        : navigation ? theme.navigationPressed : soft ? theme.softButtonPressed : theme.buttonPressed)
        : button.primary ? theme.primaryHover
        : chosen ? (navigation ? theme.navigationSelected : theme.buttonChecked)
        : navigation ? theme.navigationHover : soft ? theme.softButtonHover : theme.buttonHover
    implicitWidth: UiMetrics.buttonDefaultWidth
    implicitHeight: UiMetrics.controlHeight
    radius: button.backgroundRadius
    color: button.primary && button.enabled ? theme.accent : borderless ? "transparent"
        : soft ? theme.softButton : button.appearance === AppButton.Surface ? theme.surface : theme.button
    opacity: button.enabled ? 1 : 0.5
    border.color: outlineActive ? theme.buttonFocus : borderless ? "transparent"
        : button.accentOutline ? theme.editorAccent : theme.buttonBorder
    border.width: outlineActive ? UiMetrics.borderActive : borderless ? 0 : UiMetrics.borderNormal
    Rectangle {
        objectName: "buttonFeedbackLayer"
        anchors.fill: parent
        anchors.margins: feedback.border.width
        radius: Math.max(0, feedback.radius - feedback.border.width)
        color: feedback.interactionColor
        opacity: feedback.interactionActive ? 1 : 0
        // 색의 RGB는 고정하고 투명도만 바꿔 밝은 화면의 검은 중간색 깜박임을 막는다.
        Behavior on opacity {
            NumberAnimation {
                duration: feedback.pushed || !feedback.button.enabled ? 0
                    : feedback.interactionActive ? UiMetrics.buttonTransitionMs : UiMetrics.buttonReleaseMs
            }
        }
    }
}
