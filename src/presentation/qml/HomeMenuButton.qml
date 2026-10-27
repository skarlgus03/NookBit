import QtQuick
import QtQuick.Controls

// 아이콘과 글씨를 왼쪽에 정렬하고 현재 페이지에만 선택 배경을 표시하는 버튼.
AppButton {
    id: control
    property string iconKind: "folder"
    property string badgeText: ""
    appearance: AppButton.Navigation
    readonly property color foreground: buttonForeground
    implicitHeight: UiMetrics.mainNavigationHeight
    leftPadding: UiMetrics.controlPadding
    rightPadding: UiMetrics.controlPadding
    font.pixelSize: UiMetrics.textNavigation
    font.weight: UiMetrics.navigationTextWeight
    Accessible.name: text
    Accessible.description: [selected ? qsTr("현재 페이지") : "",
        badgeText.length > 0 ? qsTr("%1개 대기 중").arg(badgeText) : ""].filter(part => part.length > 0).join(" · ")
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
            anchors.rightMargin: badge.visible ? badge.width + UiMetrics.spaceGroup : 0
            text: control.text
            font: control.font
            color: control.foreground
            horizontalAlignment: Text.AlignLeft
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        Rectangle {
            id: badge
            objectName: "homeNavigationBadge"
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            visible: control.badgeText.length > 0
            width: Math.max(height, badgeLabel.implicitWidth + UiMetrics.spaceControl * 2)
            height: UiMetrics.homeBadgeHeight
            radius: height / 2
            color: control.buttonTheme.accent
            Accessible.ignored: true
            Text {
                id: badgeLabel
                anchors.centerIn: parent
                text: control.badgeText
                textFormat: Text.PlainText
                font.pixelSize: UiMetrics.textBody
                font.bold: true
                color: control.buttonTheme.accentText
            }
        }
    }
}
