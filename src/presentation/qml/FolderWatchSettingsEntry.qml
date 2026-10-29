import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 감시 설정의 요약과 페이지 이동 요청만 제공하는 기능 소유 설정 항목.
AppButton {
    id: entry
    required property var controller
    property Component leadingEntry
    readonly property bool hasContent: true
    readonly property var theme: UiTheme.colors(dark)
    signal routeRequested(int page)
    readonly property string watchSummary: {
        const folder = controller.folder.replace(/\\/g, "/").replace(/\/+$/, "").split("/").pop()
        const count = Number(controller.countEnabled) + Number(controller.sizeEnabled) + Number(controller.ageEnabled)
        const condition = count > 1 ? qsTr("조건 %1개 중 %2 충족 시 제안").arg(count).arg(controller.combineAll ? qsTr("모두") : qsTr("하나"))
            : controller.countEnabled ? qsTr("파일 %1개 이상이면 정리 제안").arg(controller.minimumFileCount)
            : controller.sizeEnabled ? qsTr("%1 %2 이상이면 정리 제안").arg(controller.minimumTotalSize).arg(controller.totalSizeUnit)
            : controller.ageEnabled ? qsTr("%1분 경과 시 정리 제안").arg(controller.minimumAgeMinutes) : qsTr("조건을 설정해 주세요")
        return [folder || qsTr("폴더를 지정해 주세요"), condition].join(" · ")
    }
    objectName: "settingsWatchButton"
    implicitHeight: Math.max(UiMetrics.fileRowHeight, implicitContentHeight + topPadding + bottomPadding)
    appearance: AppButton.Menu
    text: qsTr("폴더 감시")
    Accessible.name: text + ". " + entry.watchSummary
    ToolTip.visible: hovered
    ToolTip.text: entry.controller.folder + "\n" + entry.watchSummary
    onClicked: entry.routeRequested(3)
    contentItem: RowLayout {
        spacing: UiMetrics.spaceGroup
        NavigationIcon { Layout.preferredWidth: UiMetrics.navigationIconSize; Layout.preferredHeight: UiMetrics.navigationIconSize; kind: "folder"; strokeColor: entry.theme.accent }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: UiMetrics.spaceExtraSmall
            Label { text: entry.text; font.pixelSize: UiMetrics.textEmphasis; font.bold: true; color: entry.theme.text }
            Label { Layout.fillWidth: true; text: entry.watchSummary; textFormat: Text.PlainText; wrapMode: Text.Wrap; font.pixelSize: UiMetrics.textBody; color: entry.theme.muted }
        }
        Label { text: entry.controller.enabled ? qsTr("켜짐") : qsTr("꺼짐"); font.pixelSize: UiMetrics.textBody; font.bold: true; color: entry.controller.enabled ? entry.theme.success : entry.theme.muted }
        NavigationIcon { Layout.preferredWidth: UiMetrics.iconSize; Layout.preferredHeight: UiMetrics.iconSize; kind: "forward"; strokeColor: entry.theme.muted }
    }
}
