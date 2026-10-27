import QtQuick
import QtQuick.Controls

// 아이콘과 글씨를 왼쪽에 정렬하고 현재 페이지에만 선택 배경을 표시하는 버튼.
AppButton {
    id: control
    property string iconKind: "folder"
    appearance: AppButton.Navigation
    readonly property color foreground: buttonForeground
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
}
