import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 앱 설정의 안내와 이동만 담당하며 창 배치는 상위 화면에 맡긴다.
ScrollView {
    id: page
    objectName: "settingsPage"
    property bool dark: false
    readonly property var theme: UiTheme.colors(dark)
    signal watchRequested()
    signal characterRequested()
    signal collisionRequested()
    signal recoveryRequested()
    contentWidth: availableWidth
    clip: true
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    ScrollBar.vertical.policy: ScrollBar.AsNeeded
    ColumnLayout {
        width: page.availableWidth
        spacing: UiMetrics.settingsSectionSpacing
        ColumnLayout {
            Layout.fillWidth: true
            spacing: UiMetrics.spaceSmall
            Label { text: qsTr("설정"); font.pixelSize: UiMetrics.textPageTitle; font.bold: true; color: page.theme.text }
            Label { Layout.fillWidth: true; text: qsTr("화면과 파일 정리 방식을 내게 맞게 조정하세요."); wrapMode: Text.Wrap; color: page.theme.muted }
        }
        SettingsSection {
            Layout.fillWidth: true
            title: qsTr("화면")
            dark: page.dark
            Switch {
                objectName: "globalDarkModeSwitch"
                text: qsTr("앱 전체 다크모드")
                font.weight: UiMetrics.settingsLabelWeight
                checked: uiSettings.darkMode
                onToggled: uiSettings.darkMode = checked
            }
            Label { Layout.fillWidth: true; text: qsTr("선택한 테마는 자동으로 저장돼요."); wrapMode: Text.Wrap; color: page.theme.muted }
        }
        SettingsSection {
            Layout.fillWidth: true
            title: qsTr("기능 설정")
            dark: page.dark
            SettingsNavigation { Layout.fillWidth: true; dark: page.dark; text: qsTr("폴더 감시"); description: qsTr("조건에 맞는 파일을 발견하면 정리를 제안해요."); onClicked: page.watchRequested() }
            SettingsNavigation { Layout.fillWidth: true; dark: page.dark; text: qsTr("캐릭터"); description: qsTr("캐릭터팩, 표시 크기와 위치를 설정해요."); onClicked: page.characterRequested() }
            SettingsNavigation { Layout.fillWidth: true; dark: page.dark; text: qsTr("같은 이름의 파일 처리"); description: qsTr("이름 변경, 건너뛰기, 작업 중단 중 선택해요."); onClicked: page.collisionRequested() }
            SettingsNavigation { Layout.fillWidth: true; dark: page.dark; text: qsTr("중단 작업 확인"); description: qsTr("파일 위치와 기록을 확인하고 가능한 복구 방법을 안내해요."); visible: recoveryController.recoveryCases.length > 0; onClicked: page.recoveryRequested() }
        }
        SettingsSection {
            Layout.fillWidth: true
            title: qsTr("허용한 외부 목적지")
            dark: page.dark
            Label {
                Layout.fillWidth: true
                visible: destinationSettings.allowedRoots.length === 0
                text: qsTr("아직 허용한 폴더가 없어요. 규칙 편집기에서 기본 정리 위치 밖의 폴더를 허용할 수 있어요.")
                color: page.theme.muted
                wrapMode: Text.Wrap
            }
            Repeater {
                model: destinationSettings.allowedRoots
                RowLayout {
                    required property string modelData
                    Layout.fillWidth: true
                    Label { Layout.fillWidth: true; text: modelData; elide: Text.ElideMiddle; color: page.theme.text }
                    AppButton { text: qsTr("허용 해제"); onClicked: destinationSettings.revokeFolder(modelData) }
                }
            }
        }
    }
}
