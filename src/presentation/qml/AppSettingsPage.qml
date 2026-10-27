import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 앱 설정을 표시하고 다른 설정 화면으로의 이동을 요청하는 페이지.
ScrollView {
    id: page
    required property var settings
    required property var destinations
    property bool recoveryAvailable: false
    property bool dark: false
    readonly property var theme: UiTheme.colors(dark)
    signal watchSettingsRequested
    signal collisionSettingsRequested
    signal recoveryRequested

    objectName: "settingsPage"
    contentWidth: availableWidth
    clip: true
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    ColumnLayout {
        width: page.availableWidth
        spacing: UiMetrics.settingsSectionSpacing
        SettingsSection {
            Layout.fillWidth: true
            title: qsTr("화면")
            dark: page.dark
            Switch {
                objectName: "globalDarkModeSwitch"
                text: qsTr("앱 전체 다크모드")
                font.weight: UiMetrics.settingsLabelWeight
                checked: page.settings.darkMode
                onToggled: page.settings.darkMode = checked
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("테마는 자동 저장돼요.")
                wrapMode: Text.Wrap
                color: page.theme.muted
            }
        }
        SettingsSection {
            Layout.fillWidth: true
            title: qsTr("기능 설정")
            dark: page.dark
            NavigationButton {
                Layout.fillWidth: true
                font.weight: UiMetrics.settingsLabelWeight
                text: qsTr("폴더 감시 설정  ›")
                onClicked: page.watchSettingsRequested()
            }
            NavigationButton {
                Layout.fillWidth: true
                font.weight: UiMetrics.settingsLabelWeight
                text: qsTr("파일 이름 충돌 처리  ›")
                onClicked: page.collisionSettingsRequested()
            }
            NavigationButton {
                Layout.fillWidth: true
                font.weight: UiMetrics.settingsLabelWeight
                text: qsTr("중단 작업 확인")
                visible: page.recoveryAvailable
                onClicked: {
                    page.recoveryRequested();
                }
            }
        }
        SettingsSection {
            Layout.fillWidth: true
            title: qsTr("허용한 외부 목적지")
            dark: page.dark
            Label {
                Layout.fillWidth: true
                visible: page.destinations.allowedRoots.length === 0
                text: qsTr("없음 · 규칙 편집기의 목적지 설정에서 폴더를 허용할 수 있어요.")
                color: page.theme.muted
                wrapMode: Text.Wrap
            }
            Repeater {
                model: page.destinations.allowedRoots
                RowLayout {
                    required property string modelData
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        font.weight: UiMetrics.settingsLabelWeight
                        text: modelData
                        elide: Text.ElideMiddle
                        color: page.theme.text
                    }
                    AppButton {
                        font.weight: UiMetrics.settingsLabelWeight
                        text: qsTr("허용 해제")
                        onClicked: page.destinations.revokeFolder(modelData)
                    }
                }
            }
        }
    }
}
