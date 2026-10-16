import QtQuick
import QtQuick.Controls

// 상태를 색과 글자로 함께 전달하는 공통 배지.
Rectangle {
    id: badge
    property string text: ""
    property string tone: "info"
    property bool dark: false
    readonly property var theme: UiTheme.colors(dark)
    implicitWidth: label.implicitWidth + UiMetrics.spaceGroup * 2
    implicitHeight: label.implicitHeight + UiMetrics.spaceSmall * 2
    radius: UiMetrics.radiusControl
    color: theme[tone + "Surface"] || theme.alternate
    Accessible.role: Accessible.StaticText
    Accessible.name: text
    Label {
        id: label
        anchors.centerIn: parent
        text: badge.text
        font.pixelSize: UiMetrics.textCaption
        font.weight: Font.DemiBold
        color: badge.theme[badge.tone] || badge.theme.muted
    }
}
