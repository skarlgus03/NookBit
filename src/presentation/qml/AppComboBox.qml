import QtQuick
import QtQuick.Controls

// 모든 페이지의 높이·화살표·여백·선택 목록 표현을 통일한 드롭다운.
ComboBox {
    id: control
    implicitHeight: UiMetrics.controlHeight
    implicitWidth: Math.max(UiMetrics.dropdownMinWidth, contentItem.implicitWidth + leftPadding + rightPadding)
    font.pixelSize: UiMetrics.textBody
    leftPadding: UiMetrics.controlPadding
    rightPadding: UiMetrics.dropdownTrailingPadding
    topPadding: UiMetrics.spaceControl
    bottomPadding: UiMetrics.spaceControl
    contentItem: Text {
        text: control.displayText
        font: control.font
        color: control.enabled ? control.palette.text : control.palette.disabled.text
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: DropdownIndicator {
        objectName: "appDropdownIndicator"
        x: control.width - width - UiMetrics.controlPadding
        y: (control.height - height) / 2
        strokeColor: control.enabled ? control.palette.text : control.palette.disabled.text
    }
    background: Rectangle {
        radius: UiMetrics.radiusControl
        color: control.palette.base
        border.color: control.hovered || control.visualFocus || control.pressed ? control.palette.highlight : control.palette.mid
        border.width: control.hovered || control.visualFocus || control.pressed ? UiMetrics.borderActive : UiMetrics.borderNormal
    }
    delegate: ItemDelegate {
        id: choice
        required property int index
        width: control.width - UiMetrics.borderNormal * 2
        height: UiMetrics.controlHeight
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
            radius: UiMetrics.radiusInner
            color: choice.hovered || choice.highlighted ? control.palette.button : control.palette.base
        }
    }
    popup: Popup {
        y: control.height + UiMetrics.spaceExtraSmall
        width: control.width
        padding: UiMetrics.borderNormal
        implicitHeight: Math.min(UiMetrics.dropdownVisibleRows, control.count) * UiMetrics.controlHeight + UiMetrics.borderNormal * 2
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollBar.vertical: ScrollBar {}
        }
        background: Rectangle { radius: UiMetrics.radiusControl; color: control.palette.base; border.color: control.palette.mid }
    }
}
