import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 등록된 알림 종류를 표시하며 기능 자체의 활성화는 다루지 않는 설정 화면.
ScrollView {
    id: page
    objectName: "notificationSettingsPage"
    property var types: []
    property string status: ""
    property bool dark: false
    signal enabledRequested(string typeId, bool value)
    readonly property var theme: UiTheme.colors(dark)
    contentWidth: availableWidth
    clip: true
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    ColumnLayout {
        width: page.availableWidth
        spacing: UiMetrics.spaceSection
        Label {
            Layout.fillWidth: true
            text: qsTr("받을 알림을 선택하세요")
            font.pixelSize: UiMetrics.textSectionTitle
            font.bold: true
            color: page.theme.text
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("알림 표시만 바뀌어요. 기능 실행과 작업 기록은 그대로 유지돼요.")
            wrapMode: Text.Wrap
            color: page.theme.muted
        }
        Repeater {
            model: page.types
            delegate: Rectangle {
                id: notificationItem
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: itemContent.implicitHeight + UiMetrics.pageInset * 2
                color: page.theme.surface
                radius: UiMetrics.radiusCard
                border.width: UiMetrics.borderNormal
                border.color: page.theme.border
                RowLayout {
                    id: itemContent
                    anchors.fill: parent
                    anchors.margins: UiMetrics.pageInset
                    spacing: UiMetrics.spaceSection
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: UiMetrics.spaceSmall
                        Label {
                            Layout.fillWidth: true
                            text: notificationItem.modelData.name
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            font.pixelSize: UiMetrics.textEmphasis
                            font.weight: UiMetrics.groupTitleWeight
                            color: page.theme.text
                        }
                        Label {
                            Layout.fillWidth: true
                            text: notificationItem.modelData.description
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            color: page.theme.muted
                        }
                        Label {
                            Layout.fillWidth: true
                            text: notificationItem.modelData.sourceName
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            font.pixelSize: UiMetrics.textCaption
                            color: page.theme.muted
                        }
                    }
                    Switch {
                        objectName: "notificationToggle_" + notificationItem.modelData.id
                        Layout.alignment: Qt.AlignVCenter
                        implicitHeight: UiMetrics.controlHeight
                        checked: notificationItem.modelData.enabled
                        Accessible.name: notificationItem.modelData.name
                        onToggled: page.enabledRequested(notificationItem.modelData.id, checked)
                    }
                }
            }
        }
        Label {
            Layout.fillWidth: true
            visible: page.types.length === 0
            text: qsTr("등록된 알림이 없어요.")
            color: page.theme.muted
        }
        Label {
            Layout.fillWidth: true
            visible: page.status.length > 0
            text: page.status
            wrapMode: Text.Wrap
            color: page.theme.text
        }
    }
}
