import QtQuick
import QtQuick.Controls
import QtQuick.Window

// 모든 페이지의 높이·화살표·여백·선택 목록 표현을 통일한 드롭다운.
ComboBox {
    id: control
    implicitHeight: UiMetrics.controlHeight
    readonly property real widestTextWidth: measureChoiceWidth(model, count, labelMetrics.font)
    // 모델과 글꼴이 바뀔 때 가장 긴 선택지의 실제 글자 폭을 다시 구하는 함수.
    function measureChoiceWidth(source, total, metricFont) {
        if (source === null || total <= 0 || (metricFont.pixelSize <= 0 && metricFont.pointSize <= 0)) return 0
        let widest = 0
        for (let index = 0; index < total; ++index)
            widest = Math.max(widest, labelMetrics.advanceWidth(control.textAt(index)))
        return Math.ceil(widest)
    }
    FontMetrics { id: labelMetrics; font: control.font }
    implicitWidth: Math.max(UiMetrics.dropdownMinWidth, widestTextWidth + leftPadding + rightPadding + UiMetrics.spaceExtraSmall)
    font.pixelSize: UiMetrics.textBody
    leftPadding: UiMetrics.controlPadding
    rightPadding: UiMetrics.dropdownTrailingPadding
    topPadding: UiMetrics.spaceControl
    bottomPadding: UiMetrics.spaceControl
    ToolTip.visible: control.hovered && control.contentItem.truncated
    ToolTip.text: control.displayText
    ToolTip.delay: UiMetrics.tooltipDelayMs
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
        width: Math.max(1, control.popup.width - control.popup.leftPadding - control.popup.rightPadding)
        height: UiMetrics.controlHeight
        text: control.textAt(index)
        font: control.font
        highlighted: control.highlightedIndex === index
        ToolTip.visible: choice.hovered && choice.contentItem.truncated
        ToolTip.text: choice.text
        ToolTip.delay: UiMetrics.tooltipDelayMs
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
        width: Math.min(Math.max(control.width, control.widestTextWidth + UiMetrics.controlPadding * 2),
                        Math.max(control.width, control.Window.window ? control.Window.window.width - UiMetrics.pageInset * 2 : control.width))
        margins: UiMetrics.pageInset
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
