import QtQuick
import QtQuick.Controls
import QtQuick.Window

// 일반 명령 버튼은 공통 높이·글씨·아이콘 규격을 사용한다.
Button {
    id: control
    property bool dark: Window.window ? Window.window.palette.window.hslLightness < 0.5 : false
    property bool primary: false
    readonly property var buttonTheme: UiTheme.colors(control.dark)
    readonly property color buttonForeground: !enabled ? buttonTheme.muted : primary ? buttonTheme.accentText : buttonTheme.text
    implicitHeight: UiMetrics.controlHeight
    hoverEnabled: true
    padding: UiMetrics.buttonPadding
    horizontalPadding: UiMetrics.buttonPadding
    spacing: UiMetrics.spaceSmall
    font.pixelSize: UiMetrics.textBody
    icon.width: UiMetrics.iconSize
    icon.height: UiMetrics.iconSize
    palette.button: buttonTheme.button
    palette.buttonText: buttonForeground
    palette.windowText: buttonForeground
    palette.brightText: buttonForeground
    palette.disabled.buttonText: buttonTheme.muted
    palette.disabled.windowText: buttonTheme.muted
    palette.disabled.brightText: buttonTheme.muted
    background: ButtonFeedback { button: control; primary: control.primary }
}
