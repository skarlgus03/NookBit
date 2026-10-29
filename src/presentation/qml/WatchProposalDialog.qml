import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 감시 제안을 표시하고 기존 Controller에 수락·미루기를 전달하는 창.
Dialog {
    id: page
    enabled: !page.busy
    required property var controller
    property bool busy: false
    signal pendingAccepted

    anchors.centerIn: parent
    width: Math.min(parent.width - 32, 700)
    height: Math.min(parent.height - 32, 500)
    modal: true
    title: qsTr("정리 제안")
    standardButtons: Dialog.Close

    ColumnLayout {
        anchors.fill: parent
        Label {
            Layout.fillWidth: true
            text: page.controller.proposalText
            wrapMode: Text.WordWrap
        }
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: page.controller.proposalFiles
            delegate: Label {
                required property string modelData
                width: parent.width
                text: modelData
                wrapMode: Text.WrapAnywhere
                padding: UiMetrics.spaceSmall
            }
        }
        RowLayout {
            AppButton {
                objectName: "watchAcceptProposalButton"
                text: qsTr("대기 목록에 담기")
                enabled: page.controller.proposalVisible
                onClicked: {
                    page.controller.addProposalToPending();
                    page.close();
                    page.pendingAccepted();
                }
            }
            AppButton {
                objectName: "watchSnoozeProposalButton"
                text: qsTr("30분 뒤 다시 알림")
                onClicked: {
                    page.controller.snoozeProposal();
                    page.close();
                }
            }
        }
    }
}
