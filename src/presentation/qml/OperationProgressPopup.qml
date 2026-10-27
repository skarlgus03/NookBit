import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 작업 진행 정보만 표시하고 표시 위치와 노출 여부는 화면 연결부가 정한다.
Popup {
    id: popup
    required property var progress
    property bool dark: false
    readonly property var theme: UiTheme.colors(dark)
    objectName: "operationProgressPopup"
    modal: false
    dim: false
    focus: false
    closePolicy: Popup.NoAutoClose
    width: Math.min(UiMetrics.operationPopupWidth, parent.width - UiMetrics.pageInset * 2)
    height: UiMetrics.operationPopupHeight
    padding: UiMetrics.spaceCompact
    background: Rectangle {
        radius: UiMetrics.radiusCard
        color: popup.theme.surface
        border.color: popup.theme.border
    }
    contentItem: ColumnLayout {
        spacing: UiMetrics.spaceSmall
        Label {
            text: popup.progress.stageLabel
            color: popup.theme.text
            font.bold: true
            Layout.fillWidth: true
            elide: Text.ElideRight
        }
        Label {
            text: popup.progress.currentFileName
            visible: text.length > 0
            color: popup.theme.text
            Layout.fillWidth: true
            elide: Text.ElideMiddle
        }
        Label {
            text: popup.progress.filePosition
            visible: text.length > 0
            color: popup.theme.muted
            Layout.fillWidth: true
        }
        ProgressBar {
            Layout.fillWidth: true
            from: 0
            to: 1
            value: popup.progress.byteFraction
            indeterminate: !popup.progress.byteProgressKnown
            Accessible.name: popup.progress.stageLabel
        }
        Label {
            text: popup.progress.byteProgressText
            visible: popup.progress.byteProgressKnown
            color: popup.theme.text
            Layout.fillWidth: true
        }
        Label {
            text: "복사 후 검증·확정까지 마쳐야 완료돼요."
            wrapMode: Text.WordWrap
            color: popup.theme.muted
            font.pixelSize: UiMetrics.textBody
            Layout.fillWidth: true
        }
    }
}
