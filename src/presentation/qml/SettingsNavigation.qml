import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T

// 설명과 이동 방향을 함께 표시하는 설정 진입 버튼.
T.Button {
    id: control
    property string description: ""
    property bool dark: false
    readonly property var theme: UiTheme.colors(dark)
    implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
    padding: UiMetrics.spaceGroup
    focusPolicy: Qt.StrongFocus
    Accessible.name: text
    Accessible.description: description
    contentItem: RowLayout {
        spacing: UiMetrics.spaceGroup
        ColumnLayout {
            Layout.fillWidth: true
            spacing: UiMetrics.spaceExtraSmall
            Label { Layout.fillWidth: true; text: control.text; color: control.theme.text; font.pixelSize: UiMetrics.textEmphasis; font.weight: UiMetrics.settingsLabelWeight }
            Label { Layout.fillWidth: true; text: control.description; visible: text.length > 0; color: control.theme.muted; wrapMode: Text.Wrap }
        }
        Label { text: "›"; color: control.theme.muted; font.pixelSize: UiMetrics.textSectionTitle }
    }
    background: Rectangle {
        color: control.hovered ? control.theme.alternate : control.theme.surface
        radius: UiMetrics.radiusCard
        border.color: control.visualFocus ? control.theme.menuFocus : control.theme.border
        border.width: control.visualFocus ? UiMetrics.borderActive : UiMetrics.borderNormal
    }
}
