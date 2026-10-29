import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 복구 사실만 표시하고 확인 화면 이동을 요청하는 기능 소유 안내.
Rectangle {
    id: notice
    objectName: "organizerRecoveryNotice"
    required property var controller
    property bool dark: false
    property Component leadingEntry
    readonly property bool hasContent: controller.recoveryCases.length > 0
    readonly property var theme: UiTheme.colors(dark)
    signal routeRequested(int page)
    implicitHeight: recoveryRow.implicitHeight + UiMetrics.spaceSection * 2
    color: notice.theme.warningSurface
    radius: UiMetrics.radiusCard
    RowLayout {
        id: recoveryRow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: UiMetrics.spaceSection
        spacing: UiMetrics.spaceGroup
        NavigationIcon { Layout.preferredWidth: UiMetrics.iconSize; Layout.preferredHeight: UiMetrics.iconSize; kind: "alert"; strokeColor: notice.theme.warning }
        Label {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: qsTr("확인이 필요한 중단 작업이 있어요.\n파일을 옮기기 전에 작업 상태를 확인해 주세요.")
            font.pixelSize: UiMetrics.textBody
            wrapMode: Text.Wrap
            color: notice.theme.warning
        }
        AppButton { text: qsTr("확인하기"); appearance: AppButton.Surface; dark: notice.dark; onClicked: notice.routeRequested(10) }
    }
}
