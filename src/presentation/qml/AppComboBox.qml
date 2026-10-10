import QtQuick
import QtQuick.Controls

// 모든 페이지의 높이·화살표·여백·선택 목록 표현을 통일한 드롭다운.
ComboBox {
    id: control
    implicitHeight: 40
    implicitWidth: Math.max(120, contentItem.implicitWidth + leftPadding + rightPadding)
    font.pixelSize: 14
    leftPadding: 12
    rightPadding: 36
    topPadding: 8
    bottomPadding: 8
    contentItem: Text {
        text: control.displayText
        font: control.font
        color: control.enabled ? control.palette.text : control.palette.disabled.text
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: DropdownIndicator {
        objectName: "appDropdownIndicator"
        x: control.width - width - 12
        y: (control.height - height) / 2
        strokeColor: control.enabled ? control.palette.text : control.palette.disabled.text
    }
    background: Rectangle {
        radius: 8
        color: control.palette.base
        border.color: control.hovered || control.visualFocus || control.pressed ? control.palette.highlight : control.palette.mid
        border.width: control.hovered || control.visualFocus || control.pressed ? 2 : 1
    }
    delegate: ItemDelegate {
        id: choice
        required property int index
        width: control.width - 2
        height: 40
        text: control.textAt(index)
        font: control.font
        highlighted: control.highlightedIndex === index
        contentItem: Text {
            text: choice.text
            font: choice.font
            color: control.palette.text
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: 6
            color: choice.hovered || choice.highlighted ? control.palette.button : control.palette.base
        }
    }
    popup: Popup {
        y: control.height + 4
        width: control.width
        padding: 1
        implicitHeight: Math.min(322, control.count * 40 + 2)
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollBar.vertical: ScrollBar {}
        }
        background: Rectangle { radius: 8; color: control.palette.base; border.color: control.palette.mid }
    }
}
