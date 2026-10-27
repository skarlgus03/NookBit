import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 전달받은 상태를 요약하고 정리 화면 열기만 요청하는 홈 카드.
Rectangle {
    id: card
    objectName: "homeStatusCard"
    property int pendingCount: 0
    property bool watchEnabled: false
    property string watchFolder: ""
    property var companionDescriptor: ({})
    property bool dark: false
    signal sortRequested()
    readonly property var theme: UiTheme.colors(dark)
    readonly property string folderName: watchFolder.replace(/\\/g, "/").replace(/\/+$/, "").split("/").pop()
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
            spacing: UiMetrics.spaceGroup
            CompanionPortrait {
                Layout.preferredWidth: UiMetrics.homePortraitSize
                Layout.preferredHeight: UiMetrics.homePortraitSize
                descriptor: card.companionDescriptor
                dark: card.dark
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: UiMetrics.spaceExtraSmall
                Label {
                    objectName: "homePendingSummary"
                    Layout.fillWidth: true
                    text: card.pendingCount > 0 ? qsTr("정리할 파일 %1개가 모였어요").arg(card.pendingCount)
                                               : qsTr("정리할 파일이 없어요")
                    textFormat: Text.PlainText
                    font.pixelSize: UiMetrics.textEmphasis
                    font.bold: true
                    color: card.theme.text
                    wrapMode: Text.Wrap
                }
                Label {
                    objectName: "homeWatchSummary"
                    Layout.fillWidth: true
                    text: !card.watchEnabled ? qsTr("폴더 감시 꺼짐")
                        : card.folderName.length > 0 ? qsTr("%1 폴더를 지켜보는 중").arg(card.folderName)
                                                    : qsTr("감시 폴더를 확인해 주세요")
                    textFormat: Text.PlainText
                    font.pixelSize: UiMetrics.textBody
                    color: card.theme.muted
                    elide: Text.ElideMiddle
                    HoverHandler { id: watchHover }
                    ToolTip.visible: watchHover.hovered && card.watchEnabled && card.watchFolder.length > 0
                    ToolTip.delay: UiMetrics.tooltipDelayMs
                    ToolTip.text: card.watchFolder
                }
            }
        }
        AppButton {
            id: action
            objectName: "homeStatusAction"
            Layout.fillWidth: true
            implicitHeight: UiMetrics.controlHeight
            appearance: AppButton.Soft
            dark: card.dark
            text: qsTr("정리하러 가기")
            font.pixelSize: UiMetrics.textEmphasis
            font.bold: true
            onClicked: card.sortRequested()
            contentItem: Item {
                implicitHeight: Math.max(actionLabel.implicitHeight, UiMetrics.iconSize)
                implicitWidth: actionLabel.implicitWidth + UiMetrics.iconSize + UiMetrics.spaceGroup
                Text {
                    id: actionLabel
                    anchors.centerIn: parent
                    text: action.text
                    font: action.font
                    color: action.buttonForeground
                    textFormat: Text.PlainText
                }
                NavigationIcon {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: UiMetrics.iconSize
                    height: UiMetrics.iconSize
                    kind: "forward"
                    strokeColor: action.buttonForeground
                }
            }
        }
    }
}
