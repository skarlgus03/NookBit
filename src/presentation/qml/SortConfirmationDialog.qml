import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 개발용 정리 요청의 명시적 승인을 받는 기존 확인창.
Dialog {
    id: page
    enabled: !page.busy
    required property var controller
    property bool busy: false
    property bool inlineConfirmation: false

    objectName: "legacySortConfirmation"
    anchors.centerIn: parent
    width: Math.min(parent.width - UiMetrics.spaceSection * 2, UiMetrics.sortConfirmationWidth)
    height: Math.min(parent.height - UiMetrics.spaceSection * 2, UiMetrics.sortConfirmationHeight)
    modal: true
    title: qsTr("파일 이동 최종 확인")
    onClosed: if (!page.inlineConfirmation)
                  page.controller.cancelSort()

    ColumnLayout {
        anchors.fill: parent
        spacing: UiMetrics.spaceGroup
        Label {
            Layout.fillWidth: true
            text: qsTr("아래 %1개 파일을 이동할까요? 미분류·제외·건너뜀 파일은 그대로 둡니다.").arg(page.controller.confirmationSteps.length)
            wrapMode: Text.WordWrap
        }
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: UiMetrics.spaceCompact
            model: page.controller.confirmationSteps
            ScrollBar.vertical: ScrollBar {}
            delegate: Column {
                required property var modelData
                width: ListView.view.width
                spacing: UiMetrics.spaceExtraSmall
                Label {
                    width: parent.width
                    text: modelData.source
                    wrapMode: Text.WrapAnywhere
                }
                Label {
                    width: parent.width
                    text: qsTr("→ %1").arg(modelData.destination)
                    wrapMode: Text.WrapAnywhere
                }
            }
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("확인 후에도 파일과 규칙을 다시 검사해요. 변경이 있으면 새 미리보기가 필요해요.")
            wrapMode: Text.WordWrap
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            AppButton {
                text: qsTr("취소")
                onClicked: page.close()
            }
            AppButton {
                text: qsTr("확인하고 이동")
                enabled: page.controller.awaitingConfirmation
                onClicked: page.controller.confirmSort()
            }
        }
    }
}
