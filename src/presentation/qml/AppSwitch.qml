import QtQuick
import QtQuick.Controls

// 왼쪽 설명과 오른쪽 토글을 같은 높이·의미색으로 표시하는 공통 스위치.
Switch {
    id: control
    property bool dark: false
    readonly property var theme: UiTheme.colors(dark)
    implicitHeight: UiMetrics.controlHeight
    leftPadding: 0
    rightPadding: UiMetrics.switchWidth + UiMetrics.spaceGroup
    hoverEnabled: true
    font.pixelSize: UiMetrics.textBody
    indicator: Rectangle {
        x: control.width - width
        y: (control.height - height) / 2
        width: UiMetrics.switchWidth
        height: UiMetrics.switchHeight
        radius: height / 2
        color: control.checked ? control.theme.accent : control.theme.muted
        opacity: control.enabled ? 1 : 0.5
        border.width: control.visualFocus || control.hovered ? UiMetrics.borderActive : 0
        border.color: control.theme.buttonFocus
        Rectangle {
            width: UiMetrics.switchThumbSize
            height: UiMetrics.switchThumbSize
            radius: width / 2
            x: control.checked ? parent.width - width - (parent.height - height) / 2 : (parent.height - height) / 2
            anchors.verticalCenter: parent.verticalCenter
            color: control.checked ? control.theme.accentText : control.theme.surface
        }
    }
    contentItem: Text {
        text: control.text
        textFormat: Text.PlainText
        font: control.font
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.Wrap
        color: control.enabled ? control.theme.text : control.theme.muted
    }
}
