import QtQuick
import QtQuick.Controls

// 메인 메뉴의 아이콘·정중앙 글씨·선택 강조를 공통 규격으로 표시하는 버튼.
AppButton {
    id: control
    property bool dark: false
    property bool selected: false
    property string iconKind: "folder"
    readonly property color foreground: dark ? "#edf2f8" : "#273344"
    implicitHeight: UiMetrics.mainNavigationHeight
    leftPadding: UiMetrics.controlPadding
    rightPadding: UiMetrics.controlPadding
    font.pixelSize: UiMetrics.textNavigation
    font.weight: UiMetrics.navigationTextWeight
    Accessible.name: text
    Accessible.description: selected ? qsTr("현재 페이지") : ""
    contentItem: Item {
        opacity: control.enabled ? 1 : 0.55
        NavigationIcon {
            objectName: "homeMenuIcon"
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: UiMetrics.mainMenuIconSize
            height: UiMetrics.mainMenuIconSize
            kind: control.iconKind
            strokeColor: control.foreground
        }
        Text {
            objectName: "homeMenuLabel"
            anchors.fill: parent
            anchors.leftMargin: UiMetrics.mainMenuIconSize + UiMetrics.spaceGroup
            anchors.rightMargin: UiMetrics.mainMenuIconSize + UiMetrics.spaceGroup
            text: control.text
            font: control.font
            color: control.foreground
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }
    background: Rectangle {
        radius: UiMetrics.radiusControl
        color: control.selected ? (control.dark ? "#2d3d55" : "#e6eef7") : "transparent"
        border.width: UiMetrics.borderNormal
        border.color: control.hovered || control.visualFocus ? "#7898bb" : control.dark ? "#3d4757" : "#d7dfe9"
        Behavior on color { ColorAnimation { duration: UiMetrics.selectionFadeDuration } }
    }
}
