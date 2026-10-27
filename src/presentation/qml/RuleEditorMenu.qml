import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 전달받은 버튼 아래에 동일한 규격으로 메뉴 항목을 표시한다.
Popup {
    id: menu
    property bool dark: false
    readonly property var theme: UiTheme.colors(dark)
    property Item anchorButton
    default property alias items: menuContents.data
    parent: anchorButton
    y: anchorButton.height + UiMetrics.spaceExtraSmall
    width: UiMetrics.ruleMenuWidth
    implicitHeight: menuContents.implicitHeight + topPadding + bottomPadding
    padding: UiMetrics.buttonPadding
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
    background: Rectangle {
        radius: UiMetrics.radiusControl
        color: menu.theme.surface
        border.color: menu.theme.border
    }
    contentItem: ColumnLayout {
        id: menuContents
        spacing: UiMetrics.spaceExtraSmall
    }
}
