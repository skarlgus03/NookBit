import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 현재 설정을 카드로 요약하고 저장·세부 화면 이동을 기존 창구에 전달한다.
ScrollView {
    id: page
    required property var settings
    property var watchContribution: ({})
    property var organizerContribution: ({})
    property bool dark: false
    property var startupMessages: []
    property string applicationVersion: ""
    readonly property var theme: UiTheme.colors(dark)
    signal routeRequested(int page)
    objectName: "settingsPage"
    contentWidth: availableWidth
    clip: true
    padding: UiMetrics.spaceSection
    background: Rectangle { color: page.theme.window }
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    ColumnLayout {
        width: page.availableWidth
        spacing: UiMetrics.spaceSection
        Repeater {
            model: page.startupMessages
            Label {
                required property string modelData
                objectName: "featureStartupMessage"
                Layout.fillWidth: true
                text: modelData
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                color: page.theme.warning
                font.pixelSize: UiMetrics.textBody
            }
        }
        RegisteredSettingsSection {
            Layout.fillWidth: true
            contribution: page.organizerContribution.notice || ({})
            dark: page.dark
            onRouteRequested: function(route) { page.routeRequested(route) }
        }
        SettingsSection {
            Layout.fillWidth: true
            title: qsTr("화면")
            dark: page.dark
            RowLayout {
                Layout.fillWidth: true
                spacing: UiMetrics.spaceGroup
                ColumnLayout {
                    Layout.minimumWidth: 0
                    spacing: UiMetrics.spaceExtraSmall
                    Label { text: qsTr("테마"); font.pixelSize: UiMetrics.textEmphasis; font.bold: true; color: page.theme.text }
                    Label { text: qsTr("선택은 바로 저장돼요."); font.pixelSize: UiMetrics.textBody; color: page.theme.muted }
                }
                Item { Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredWidth: 0 }
                Rectangle {
                    objectName: "themeChoiceGroup"
                    Layout.alignment: Qt.AlignRight
                    Layout.minimumWidth: implicitWidth
                    Layout.maximumWidth: implicitWidth
                    implicitWidth: themeButtons.implicitWidth + UiMetrics.spaceExtraSmall * 2
                    implicitHeight: UiMetrics.controlHeight + UiMetrics.spaceExtraSmall * 2
                    color: page.theme.alternate
                    radius: UiMetrics.radiusControl
                    RowLayout {
                        id: themeButtons
                        anchors.centerIn: parent
                        spacing: 0
                        AppButton {
                            objectName: "globalLightModeButton"
                            text: qsTr("라이트")
                            dark: page.dark
                            appearance: AppButton.Menu
                            selected: !page.settings.darkMode
                            onClicked: page.settings.darkMode = false
                        }
                        AppButton {
                            objectName: "globalDarkModeSwitch"
                            text: qsTr("다크")
                            dark: page.dark
                            appearance: AppButton.Menu
                            selected: page.settings.darkMode
                            onClicked: page.settings.darkMode = true
                        }
                    }
                }
            }
        }
        RegisteredSettingsSection {
            Layout.fillWidth: true
            contribution: page.organizerContribution
            leadingEntry: page.watchContribution.source ? watchEntry : null
            dark: page.dark
            onRouteRequested: function(route) { page.routeRequested(route) }
        }
        SettingsSection {
            Layout.fillWidth: true
            title: qsTr("정보")
            dark: page.dark
            Label {
                objectName: "applicationVersionLabel"
                Layout.fillWidth: true
                text: "NookBit " + page.applicationVersion
                textFormat: Text.PlainText
                font.pixelSize: UiMetrics.textEmphasis
                font.bold: true
                color: page.theme.text
                wrapMode: Text.Wrap
            }
        }
    }
    Component {
        id: watchEntry
        RegisteredSettingsSection {
            contribution: page.watchContribution
            dark: page.dark
            onRouteRequested: function(route) { page.routeRequested(route) }
        }
    }
}
