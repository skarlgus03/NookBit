import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 설정 항목을 큰 제목·구분선·본문 여백으로 나누는 공통 섹션.
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
            font.pixelSize: UiMetrics.textSectionTitle
            font.weight: UiMetrics.groupTitleWeight
            color: section.dark ? "#edf2f8" : "#27352b"
        },
        Rectangle {
            objectName: "settingsSectionDivider"
            Layout.fillWidth: true
            implicitHeight: UiMetrics.borderNormal
            color: section.dark ? "#3d4757" : "#d9dfd8"
        },
        ColumnLayout {
            id: body
            Layout.fillWidth: true
            spacing: UiMetrics.spaceGroup
        }
    ]
}
