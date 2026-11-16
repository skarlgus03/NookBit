import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 파일 정리 안에서만 잠시 표시하는 결과 화면. 별도 창·탐색 페이지를 만들지 않는다.
Rectangle {
    id: preview
    objectName: "fileSortPreview"
    required property var controller
    property bool dark: false
    readonly property var theme: UiTheme.colors(dark)
    signal backRequested()
    signal sortRequested()
    readonly property color foreground: theme.text
    readonly property color muted: theme.muted
    readonly property color surface: theme.alternate
    readonly property bool noMove: controller.hasPreview && !controller.hasMovableSteps
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
        Label { Layout.fillWidth: true; visible: !preview.noMove; text: qsTr("아직 파일은 이동하지 않았어요. 결과를 확인한 뒤 정리할 수 있어요."); wrapMode: Text.Wrap; color: preview.muted }
        Rectangle {
            objectName: "filePreviewNoMoveNotice"
            Layout.fillWidth: true
            visible: preview.noMove
            implicitHeight: noticeContent.implicitHeight + 2 * UiMetrics.spaceGroup
            radius: UiMetrics.radiusCard
            color: preview.surface
            border.color: preview.theme.border
            ColumnLayout {
                id: noticeContent
                anchors.fill: parent
                anchors.margins: UiMetrics.spaceGroup
                spacing: UiMetrics.spaceSmall
                Label {
                    Layout.fillWidth: true
                    text: qsTr("정리할 파일이 없어요")
                    color: preview.foreground
                    font.bold: true
                    wrapMode: Text.Wrap
                }
                Label {
                    Layout.fillWidth: true
                    text: preview.controller.message
                    textFormat: Text.PlainText
                    color: preview.muted
                    wrapMode: Text.Wrap
                }
            }
        }
        Label {
            Layout.fillWidth: true
            visible: !preview.noMove && text.length > 0
            text: preview.controller.message
            wrapMode: Text.Wrap
            color: preview.foreground
        }
        AppButton {
            objectName: "documentRecommendationButton"
            dark: preview.dark
            text: qsTr("문서 폴더 추천")
            enabled: preview.controller.hasPreview && !preview.controller.recommendationBusy
            onClicked: preview.controller.recommendDocumentFolders()
        }
        Label {
            Layout.fillWidth: true
            visible: text.length > 0
            text: preview.controller.recommendationStatus || ""
            wrapMode: Text.Wrap
            color: preview.muted
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
                        Label { text: modelData.status; font.bold: true; color: preview.foreground }
                    }
                    Label { Layout.fillWidth: true; text: modelData.reason; wrapMode: Text.Wrap; color: preview.foreground }
                    Label { Layout.fillWidth: true; text: qsTr("원본: %1").arg(modelData.source); elide: Text.ElideMiddle; color: preview.muted }
                    Label { Layout.fillWidth: true; text: modelData.destination.length > 0 ? qsTr("예상 목적지: %1").arg(modelData.destination) : qsTr("이동하지 않음 · 원본 유지"); elide: Text.ElideMiddle; color: preview.muted }
                    Label {
                        Layout.fillWidth: true
                        visible: text.length > 0
                        text: modelData.recommendedFolder
                              ? qsTr("문서 추천: %1").arg(modelData.recommendedFolder)
                              : modelData.recommendationNote || ""
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        color: preview.foreground
                    }
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
            text: preview.noMove ? qsTr("대기 목록으로") : qsTr("정리하기")
            enabled: preview.controller.hasPreview && !preview.controller.awaitingConfirmation
                     && !preview.controller.recommendationBusy
            onClicked: preview.noMove ? preview.backRequested() : preview.sortRequested()
        }
    }
}
