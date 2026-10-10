import QtQuick
import QtQuick.Controls

// 입력 상자의 높이·여백·테두리를 드롭다운과 맞춘 텍스트 입력.
TextField {
    id: control
    implicitHeight: 40
    font.pixelSize: 14
    leftPadding: 12
    rightPadding: 12
    background: Rectangle {
        radius: 8
        color: control.palette.base
        border.color: control.activeFocus || control.hovered ? control.palette.highlight : control.palette.mid
        border.width: control.activeFocus || control.hovered ? 2 : 1
    }
}
