import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 설정 항목을 작은 그룹 제목과 공통 카드로 구분하는 섹션.
ColumnLayout {
    id: section
    property string title: ""
    property bool dark: false
    default property alias contentData: body.data
    spacing: UiMetrics.spaceGroup
    data: [
        Label {
            objectName: "settingsSectionTitle"
            Layout.fillWidth: true
            text: section.title
            font.pixelSize: UiMetrics.textSettingsGroup
            font.weight: UiMetrics.groupTitleWeight
            color: UiTheme.colors(section.dark).muted
        },
        Rectangle {
            objectName: "settingsSectionCard"
            Layout.fillWidth: true
            implicitHeight: body.implicitHeight + UiMetrics.spaceSection * 2
            radius: UiMetrics.radiusCard
            color: UiTheme.colors(section.dark).surface
            ColumnLayout {
                id: body
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: UiMetrics.spaceSection
                spacing: UiMetrics.spaceGroup
            }
        }
    ]
}
