import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 기존 충돌 정책을 선택 카드로 표시하고 저장은 상위 화면에 요청하는 부품.
RowLayout {
    id: picker
    property string policy: "rename"
    property bool dark: false
    readonly property var theme: UiTheme.colors(dark)
    signal policyRequested(string policy)
    spacing: UiMetrics.spaceControl
    Repeater {
        model: [
            {key: "rename", title: qsTr("이름 바꾸기"), description: qsTr("파일 (1).png처럼 번호를 붙여요.")},
            {key: "skip", title: qsTr("건너뛰기"), description: qsTr("겹친 파일은 두고 나머지만 옮겨요.")},
            {key: "abort", title: qsTr("작업 멈추기"), description: qsTr("이름이 겹치면 정리 준비를 중단해요.")}
        ]
        delegate: AppButton {
            required property var modelData
            objectName: "collisionPolicy_" + modelData.key
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 1
            Layout.minimumWidth: 0
            implicitHeight: Math.max(UiMetrics.collisionChoiceMinHeight, implicitContentHeight + topPadding + bottomPadding)
            appearance: AppButton.Surface
            selected: picker.policy === modelData.key
            dark: picker.dark
            text: modelData.title
            Accessible.name: modelData.title + ". " + modelData.description
            Accessible.description: selected ? qsTr("선택됨") : ""
            onClicked: picker.policyRequested(modelData.key)
            contentItem: ColumnLayout {
                spacing: UiMetrics.spaceExtraSmall
                Label { Layout.fillWidth: true; text: modelData.title; font.pixelSize: UiMetrics.textBody; font.bold: true; wrapMode: Text.Wrap; color: picker.theme.text }
                Label { Layout.fillWidth: true; text: modelData.description; font.pixelSize: UiMetrics.textBody; wrapMode: Text.Wrap; color: picker.theme.muted }
            }
        }
    }
}
