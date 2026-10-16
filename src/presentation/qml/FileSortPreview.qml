import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 파일 정리 안에서만 잠시 표시하는 결과 화면. 별도 창·탐색 페이지를 만들지 않는다.
Rectangle {
    id: preview
    objectName: "fileSortPreview"
    required property var controller
    property bool dark: false
    signal backRequested()
    signal sortRequested()
    readonly property var theme: UiTheme.colors(dark)
    readonly property color foreground: theme.text
    readonly property color muted: theme.muted
    readonly property color surface: theme.alternate
    color: theme.surface
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: UiMetrics.pageInset
        spacing: UiMetrics.spaceGroup
        BackButton {
            objectName: "filePreviewBackButton"
            onClicked: preview.backRequested()
        }
        Label { text: qsTr("결과 미리보기"); font.pixelSize: UiMetrics.textSectionTitle; font.bold: true; color: preview.foreground }
        Label { Layout.fillWidth: true; text: qsTr("아직 파일은 이동하지 않았어요. 결과를 확인한 뒤 정리할 수 있어요."); wrapMode: Text.Wrap; color: preview.muted }
        Flow {
            Layout.fillWidth: true
            Layout.preferredHeight: childrenRect.height
            spacing: UiMetrics.spaceControl
            Repeater {
                model: [
                    { key: "planned", label: qsTr("이동 예정"), tone: "success" },
                    { key: "skipped", label: qsTr("건너뜀"), tone: "warning" },
                    { key: "unmatched", label: qsTr("미분류"), tone: "warning" },
                    { key: "excluded", label: qsTr("제외"), tone: "info" }
                ]
                StatusBadge {
                    required property var modelData
                    dark: preview.dark
                    tone: modelData.tone
                    text: modelData.label + " " + preview.controller.outcomes.filter(function(row) { return row.statusKey === modelData.key }).length
                }
            }
        }
        Label {
            Layout.fillWidth: true
            visible: text.length > 0
            text: preview.controller.message
            wrapMode: Text.Wrap
            color: preview.foreground
        }
        ListView {
            id: results
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: UiMetrics.spaceControl
            model: preview.controller.outcomes
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                required property var modelData
                width: results.width
                height: details.implicitHeight + UiMetrics.spaceGroup * 2
                radius: UiMetrics.radiusCard
                color: preview.surface
                ColumnLayout {
                    id: details
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                    anchors.margins: UiMetrics.spaceGroup
                    spacing: UiMetrics.spaceExtraSmall
                    RowLayout {
                        Layout.fillWidth: true
                        Label { Layout.fillWidth: true; text: modelData.fileName; font.bold: true; elide: Text.ElideMiddle; color: preview.foreground }
                        StatusBadge { text: modelData.status; dark: preview.dark; tone: modelData.statusKey === "planned" ? "success" : modelData.statusKey === "excluded" ? "info" : "warning" }
                    }
                    Label { Layout.fillWidth: true; text: modelData.reason; wrapMode: Text.Wrap; color: preview.foreground }
                    Label { Layout.fillWidth: true; text: qsTr("원본: %1").arg(modelData.source); elide: Text.ElideMiddle; color: preview.muted }
                    Label { Layout.fillWidth: true; text: modelData.destination.length > 0 ? qsTr("예상 목적지: %1").arg(modelData.destination) : qsTr("이동하지 않음 · 원본 유지"); elide: Text.ElideMiddle; color: preview.muted }
                }
            }
            Label { anchors.centerIn: parent; visible: results.count === 0; text: qsTr("표시할 결과가 없어요. 안내 내용을 확인해 주세요."); color: preview.muted }
        }
        Label {
            Layout.fillWidth: true
            visible: preview.controller.warnings.length > 0
            text: preview.controller.warnings.join("\n")
            wrapMode: Text.Wrap
            color: preview.foreground
        }
        LargeActionButton {
            objectName: "filePreviewSortButton"
            Layout.fillWidth: true
            primary: true
            text: qsTr("정리하기")
            enabled: preview.controller.hasPreview && preview.controller.hasMovableSteps && !preview.controller.awaitingConfirmation
            onClicked: preview.sortRequested()
        }
    }
}
