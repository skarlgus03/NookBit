import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 저장된 조건으로 수행한 실제 최근 검사만 요약하는 상태 카드.
Rectangle {
    id: card
    required property var controller
    property bool dark: false
    readonly property var theme: UiTheme.colors(dark)
    readonly property var snapshot: controller.lastEvaluation || ({})
    readonly property bool checked: !!snapshot.checkedAt
    signal checkRequested()
    implicitHeight: contents.implicitHeight + UiMetrics.spaceSection * 2
    color: theme.surface
    radius: UiMetrics.radiusCard
    ColumnLayout {
        id: contents
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: UiMetrics.spaceSection
        spacing: UiMetrics.spaceGroup
        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: !card.controller.enabled ? qsTr("자동 감시 꺼짐") : !card.checked ? qsTr("검사 대기 중")
                    : card.snapshot.shouldOffer ? qsTr("지켜보는 중 · 조건 충족") : qsTr("지켜보는 중 · 아직 조건 전")
                font.pixelSize: UiMetrics.textEmphasis
                font.bold: true
                wrapMode: Text.Wrap
                color: card.controller.enabled ? card.theme.success : card.theme.muted
            }
            AppButton { text: qsTr("지금 검사"); appearance: AppButton.Soft; dark: card.dark; onClicked: card.checkRequested(); ToolTip.visible: hovered; ToolTip.text: qsTr("현재 저장된 폴더와 조건을 검사해요.") }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: UiMetrics.spaceControl
            Repeater {
                model: [
                    {title: qsTr("분류 가능한 파일"), enabled: card.controller.countEnabled, value: card.snapshot.fileCount || 0, limit: card.controller.minimumFileCount, unit: qsTr("개")},
                    {title: qsTr("전체 크기"), enabled: card.controller.sizeEnabled, value: Math.round((card.snapshot.totalBytes || 0) / ({KB: 1000, MB: 1000000, GB: 1000000000, MiB: 1048576}[card.controller.totalSizeUnit] || 1) * 10) / 10, limit: card.controller.minimumTotalSize, unit: card.controller.totalSizeUnit},
                    {title: qsTr("가장 오래된 파일"), enabled: card.controller.ageEnabled, value: card.snapshot.oldestAgeMinutes || 0, limit: card.controller.minimumAgeMinutes, unit: qsTr("분")}
                ]
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 1
                    implicitHeight: metrics.implicitHeight + UiMetrics.spaceGroup * 2
                    radius: UiMetrics.radiusControl
                    color: card.theme.window
                    opacity: modelData.enabled ? 1 : 0.5
                    ColumnLayout {
                        id: metrics
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: UiMetrics.spaceGroup
                        spacing: UiMetrics.spaceControl
                        Label { Layout.fillWidth: true; text: modelData.title; wrapMode: Text.Wrap; font.pixelSize: UiMetrics.textBody; color: card.theme.muted }
                        Label { Layout.fillWidth: true; text: card.checked ? qsTr("%1 / %2 %3").arg(modelData.value).arg(modelData.limit).arg(modelData.unit) : qsTr("검사 전"); wrapMode: Text.Wrap; font.pixelSize: UiMetrics.textBody; font.bold: true; color: card.theme.text }
                        ProgressBar { Layout.fillWidth: true; from: 0; to: Math.max(1, modelData.limit); value: card.checked ? Math.min(modelData.value, modelData.limit) : 0; Accessible.name: modelData.title }
                    }
                }
            }
        }
        Label {
            Layout.fillWidth: true
            text: card.checked ? qsTr("저장된 조건의 최근 검사 · %1").arg(Qt.formatTime(new Date(card.snapshot.checkedAt), "hh:mm")) : qsTr("저장된 조건으로 검사하면 현재 수치를 확인할 수 있어요.")
            wrapMode: Text.Wrap
            font.pixelSize: UiMetrics.textBody
            color: card.theme.muted
        }
    }
}
