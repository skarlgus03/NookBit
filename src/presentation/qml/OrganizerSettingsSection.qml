import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 충돌 처리와 외부 목적지 설정만 소유하고 다른 기능의 앞부분은 표시 슬롯으로 받는다.
ColumnLayout {
    id: section
    objectName: "organizerSettingsSection"
    required property var sorting
    required property var destinations
    property bool dark: false
    property Component leadingEntry
    readonly property bool hasContent: true
    readonly property var theme: UiTheme.colors(dark)
    signal routeRequested(int page)
    spacing: UiMetrics.spaceSection
    SettingsSection {
        Layout.fillWidth: true
        title: qsTr("파일 정리")
        dark: section.dark
        Loader {
            Layout.fillWidth: true
            active: !!section.leadingEntry
            visible: active
            sourceComponent: section.leadingEntry
        }
        Rectangle { Layout.fillWidth: true; visible: !!section.leadingEntry; implicitHeight: UiMetrics.borderNormal; color: section.theme.border }
        Label { text: qsTr("같은 이름의 파일이 있으면"); font.pixelSize: UiMetrics.textEmphasis; font.bold: true; color: section.theme.text }
        Label { Layout.fillWidth: true; text: qsTr("목적지에 이미 같은 이름의 파일이 있을 때"); font.pixelSize: UiMetrics.textBody; color: section.theme.muted; wrapMode: Text.Wrap }
        CollisionPolicyPicker {
            Layout.fillWidth: true
            policy: section.sorting.collisionPolicy
            dark: section.dark
            onPolicyRequested: policy => section.sorting.collisionPolicy = policy
        }
        Label { Layout.fillWidth: true; text: qsTr("규칙에 별도 처리가 지정되어 있으면 그 설정이 우선해요."); wrapMode: Text.Wrap; font.pixelSize: UiMetrics.textBody; color: section.theme.muted }
    }
    SettingsSection {
        Layout.fillWidth: true
        title: qsTr("허용한 외부 목적지")
        dark: section.dark
        Label { Layout.fillWidth: true; text: qsTr("기본 정리 폴더 밖으로 옮길 수 있는 곳"); wrapMode: Text.Wrap; color: section.theme.muted; font.pixelSize: UiMetrics.textBody }
        Label { Layout.fillWidth: true; visible: section.destinations.allowedRoots.length === 0; text: qsTr("허용한 외부 폴더가 없어요."); wrapMode: Text.Wrap; color: section.theme.muted; font.pixelSize: UiMetrics.textBody }
        Repeater {
            model: section.destinations.allowedRoots
            RowLayout {
                required property string modelData
                Layout.fillWidth: true
                spacing: UiMetrics.spaceGroup
                NavigationIcon { Layout.preferredWidth: UiMetrics.iconSize; Layout.preferredHeight: UiMetrics.iconSize; kind: "folder"; strokeColor: section.theme.muted }
                Label {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: modelData
                    textFormat: Text.PlainText
                    elide: Text.ElideMiddle
                    color: section.theme.text
                    font.pixelSize: UiMetrics.textBody
                    HoverHandler { id: pathHover }
                    ToolTip.visible: pathHover.hovered
                    ToolTip.text: modelData
                    ToolTip.delay: UiMetrics.tooltipDelayMs
                }
                AppButton { text: qsTr("허용 해제"); flat: true; destructive: true; dark: section.dark; onClicked: section.destinations.revokeFolder(modelData) }
            }
        }
        Label { Layout.fillWidth: true; text: qsTr("새 외부 폴더는 분류 규칙에서 목적지를 고를 때 허용할 수 있어요."); wrapMode: Text.Wrap; color: section.theme.muted; font.pixelSize: UiMetrics.textBody }
    }
}
