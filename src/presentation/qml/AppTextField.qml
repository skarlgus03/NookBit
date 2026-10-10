import QtQuick
import QtQuick.Controls

// 입력 상자의 높이·여백·테두리를 드롭다운과 맞춘 텍스트 입력.
TextField {
    id: control
    implicitHeight: UiMetrics.controlHeight
    font.pixelSize: UiMetrics.textBody
    leftPadding: UiMetrics.controlPadding
    rightPadding: UiMetrics.controlPadding
    background: Rectangle {
        radius: UiMetrics.radiusControl
        color: control.palette.base
        border.color: control.activeFocus || control.hovered ? control.palette.highlight : control.palette.mid
        border.width: control.activeFocus || control.hovered ? UiMetrics.borderActive : UiMetrics.borderNormal
    }
}
