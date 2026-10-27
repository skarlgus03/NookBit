import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 전달된 기록 목록만 표시하고 선택·새로고침을 요청하는 목록. 검색 결과도 같은 입력으로 받을 수 있다.
Item {
    id: timeline
    property var entries: []
    property string selectedPlanId: ""
    property bool busy: false
    property bool dark: false
    readonly property var theme: UiTheme.colors(dark)
    signal planRequested(string planId)
    signal refreshRequested()
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: UiMetrics.spaceGroup
        spacing: UiMetrics.spaceGroup
        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: qsTr("최근 %1건").arg(timeline.entries.length)
                color: timeline.theme.muted
                font.pixelSize: UiMetrics.textBody
            }
            AppButton {
                objectName: "historyRefreshButton"
                implicitWidth: UiMetrics.controlHeight
                flat: true
                dark: timeline.dark
                enabled: !timeline.busy
                Accessible.name: qsTr("작업 기록 새로고침")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("최근 작업을 최대 50건 다시 불러와요.")
                ToolTip.delay: UiMetrics.tooltipDelayMs
                contentItem: NavigationIcon { kind: "refresh"; strokeColor: timeline.theme.muted }
                onClicked: timeline.refreshRequested()
            }
        }
        ListView {
            id: historyList
            objectName: "historyList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: UiMetrics.spaceExtraSmall
            model: timeline.entries
            ScrollBar.vertical: ScrollBar {}
            Label {
                anchors.centerIn: parent
                width: parent.width - UiMetrics.spaceSection * 2
                visible: historyList.count === 0
                text: qsTr("아직 작업 기록이 없어요.")
                font.pixelSize: UiMetrics.textBody
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                color: timeline.theme.muted
            }
            delegate: Column {
                required property var modelData
                required property int index
                width: historyList.width - UiMetrics.spaceControl
                spacing: UiMetrics.spaceExtraSmall
                Label {
                    objectName: "historyDateHeading"
                    width: parent.width
                    visible: index === 0 || modelData.dateGroup !== historyList.model[index - 1].dateGroup
                    height: visible ? implicitHeight : 0
                    topPadding: UiMetrics.spaceGroup
                    bottomPadding: UiMetrics.spaceControl
                    leftPadding: UiMetrics.spaceGroup
                    text: modelData.dateGroup
                    font.pixelSize: UiMetrics.textBody
                    font.weight: UiMetrics.groupTitleWeight
                    color: timeline.theme.muted
                }
                AppButton {
                    id: row
                    objectName: "historyPlanRow"
                    width: parent.width
                    implicitHeight: UiMetrics.historyRowHeight
                    appearance: AppButton.Navigation
                    selected: modelData.planId === timeline.selectedPlanId
                    dark: timeline.dark
                    enabled: !timeline.busy
                    Accessible.name: qsTr("%1, 파일 %2개, %3").arg(modelData.time).arg(modelData.fileCount).arg(modelData.status)
                    onClicked: timeline.planRequested(modelData.planId)
                    contentItem: RowLayout {
                        spacing: UiMetrics.spaceGroup
                        Rectangle {
                            Layout.preferredWidth: UiMetrics.fileIconSize
                            Layout.preferredHeight: UiMetrics.fileIconSize
                            radius: width / 2
                            readonly property string tone: modelData.statusTone || "neutral"
                            color: timeline.theme[tone + "Surface"] || timeline.theme.alternate
                            NavigationIcon {
                                anchors.centerIn: parent
                                width: UiMetrics.iconSize
                                height: UiMetrics.iconSize
                                kind: parent.tone === "success" ? "check" : parent.tone === "neutral" ? "history" : "alert"
                                strokeColor: timeline.theme[parent.tone] || timeline.theme.muted
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: UiMetrics.spaceExtraSmall
                            Label {
                                Layout.fillWidth: true
                                text: qsTr("%1  파일 %2개").arg(modelData.shortTime || modelData.time).arg(modelData.fileCount)
                                textFormat: Text.PlainText
                                font.pixelSize: UiMetrics.textEmphasis
                                font.weight: Font.DemiBold
                                color: timeline.theme.text
                                elide: Text.ElideRight
                            }
                            Label {
                                Layout.fillWidth: true
                                text: modelData.status
                                font.pixelSize: UiMetrics.textBody
                                color: timeline.theme.muted
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }
        }
    }
}
