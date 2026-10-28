import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 공통 선택 카드로 기존 이름 충돌 설정을 표시하는 페이지.
PageSurface {
    id: page
    required property var controller
    background: Rectangle { color: page.theme.window }
    ScrollView {
        id: scroll
        anchors.fill: parent
        padding: UiMetrics.spaceSection
        contentWidth: availableWidth
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        SettingsSection {
            width: scroll.availableWidth
            title: qsTr("같은 이름의 파일이 있으면")
            dark: page.dark
            CollisionPolicyPicker {
                Layout.fillWidth: true
                policy: page.controller.collisionPolicy
                dark: page.dark
                onPolicyRequested: policy => page.controller.collisionPolicy = policy
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("규칙에 별도 처리가 지정되어 있으면 그 설정이 우선해요.")
                font.pixelSize: UiMetrics.textBody
                wrapMode: Text.Wrap
                color: page.theme.muted
            }
        }
    }
}
