import QtQuick
import QtQuick.Controls

// 아이콘과 글씨를 왼쪽에 정렬하고 현재 페이지에만 선택 배경을 표시하는 버튼.
AppButton {
    id: control
    property bool selected: false
    property string iconKind: "folder"
    readonly property var theme: UiTheme.colors(control.dark)
    readonly property color foreground: !enabled ? theme.muted : selected ? theme.navigationActiveText : theme.text
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
            anchors.rightMargin: 0
            text: control.text
            font: control.font
            color: control.foreground
            horizontalAlignment: Text.AlignLeft
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }
    background: ButtonFeedback {
        button: control
        selected: control.selected
        showBorder: false
        normalColor: "transparent"
        hoverColor: control.theme.alternate
        selectedColor: control.theme.menuSelected
    }
}
