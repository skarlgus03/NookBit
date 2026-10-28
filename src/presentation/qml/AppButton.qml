import QtQuick
import QtQuick.Controls
import QtQuick.Window

// 일반 명령 버튼은 공통 높이·글씨·아이콘 규격을 사용한다.
Button {
    id: control
    enum Appearance { Standard, Navigation, Menu, Surface, Soft }
    property bool dark: Window.window ? Window.window.palette.window.hslLightness < 0.5 : false
    property bool primary: false
    property bool destructive: false
    property bool selected: false
    property int appearance: AppButton.Standard
    property bool accentOutline: false
    property real backgroundRadius: UiMetrics.radiusControl
    readonly property var buttonTheme: UiTheme.colors(control.dark)
    readonly property bool activeSelection: selected || checked || highlighted
    readonly property color buttonForeground: !enabled ? buttonTheme.muted : primary ? buttonTheme.accentText
        : destructive ? buttonTheme.danger
        : appearance === AppButton.Soft ? buttonTheme.accentSoftText
        : appearance === AppButton.Navigation && activeSelection ? buttonTheme.navigationActiveText : buttonTheme.text
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset, UiMetrics.buttonMinWidth,
                           Math.ceil(Math.max(implicitContentWidth, labelMetrics.advanceWidth))
                           + leftPadding + rightPadding + UiMetrics.spaceExtraSmall)
    implicitHeight: Math.max(UiMetrics.controlHeight, Math.ceil(implicitContentHeight) + topPadding + bottomPadding)
    hoverEnabled: true
    padding: UiMetrics.buttonPadding
    horizontalPadding: UiMetrics.controlPadding
    spacing: UiMetrics.spaceSmall
    font.pixelSize: UiMetrics.textBody
    icon.width: UiMetrics.iconSize
    icon.height: UiMetrics.iconSize
    TextMetrics { id: labelMetrics; font: control.font; text: control.text }
    ToolTip.visible: control.hovered && control.text.length > 0 && control.width + UiMetrics.spaceTiny < control.implicitWidth
    ToolTip.text: control.text
    ToolTip.delay: UiMetrics.tooltipDelayMs
    palette.button: buttonTheme.button
    palette.buttonText: buttonForeground
    palette.windowText: buttonForeground
    palette.brightText: buttonForeground
    palette.disabled.buttonText: buttonTheme.muted
    palette.disabled.windowText: buttonTheme.muted
    palette.disabled.brightText: buttonTheme.muted
    background: ButtonFeedback { button: control }
}
