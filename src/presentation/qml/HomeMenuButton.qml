import QtQuick
import QtQuick.Controls

// 메인 메뉴의 아이콘·정중앙 글씨·선택 강조를 공통 규격으로 표시하는 버튼.
AppButton {
    id: control
    property bool selected: false
    property string iconKind: "folder"
    readonly property var theme: UiTheme.colors(control.dark)
    readonly property color foreground: theme.text
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
    background: ButtonFeedback {
        button: control
        selected: control.selected
        normalColor: "transparent"
        selectedColor: control.theme.menuSelected
        idleBorder: control.theme.menuBorder
    }
}
