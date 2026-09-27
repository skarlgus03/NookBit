import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    width: 920
    height: 760
    minimumWidth: 680
    minimumHeight: 620
    visible: true
    title: qsTr("NookBit")
    color: "#f5f5f2"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 32
        spacing: 14

        Label {
            text: qsTr("NookBit")
            font.pixelSize: 30
            font.bold: true
            color: "#232323"
        }

        Label {
            text: qsTr("정리할 파일을 먼저 대기 목록에 담아 주세요.")
            font.pixelSize: 16
            color: "#5a5a56"
        }

        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: watchController.enabled ? qsTr("폴더 감시 중 · ") + watchController.folder
                                              : qsTr("폴더 감시 꺼짐")
                color: "#426349"
                elide: Text.ElideMiddle
            }
            Button {
                text: qsTr("폴더 감시 설정")
                onClicked: {
                    watchFolderField.text = watchController.folder
                    watchEnabledCheck.checked = watchController.enabled
                    watchRecursiveCheck.checked = watchController.recursive
                    watchIntervalSpin.value = watchController.rescanIntervalMinutes
                    watchCombination.currentIndex = watchController.combineAll ? 1 : 0
                    watchCountCheck.checked = watchController.countEnabled
                    watchCountSpin.value = watchController.minimumFileCount
                    watchSizeCheck.checked = watchController.sizeEnabled
                    watchSizeSpin.value = watchController.minimumTotalMiB
                    watchAgeCheck.checked = watchController.ageEnabled
                    watchAgeSpin.value = watchController.minimumAgeMinutes
                    watchExcludeField.text = watchController.excludePatterns
                    watchDialog.open()
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: watchController.proposalVisible ? 92 : 0
            visible: watchController.proposalVisible
            radius: 12
            color: "#e8f4e5"
            border.color: "#8ab793"
            RowLayout {
                anchors.fill: parent
                anchors.margins: 12
                Label {
                    Layout.fillWidth: true
                    text: watchController.proposalText
                    wrapMode: Text.WordWrap
                    color: "#24382a"
                }
                Button {
                    text: qsTr("제안 확인")
                    onClicked: watchProposalDialog.open()
                }
            }
        }

        Label {
            Layout.fillWidth: true
            visible: previewController.recoveryNotice.length > 0
            text: previewController.recoveryNotice
            color: "#9b5c23"
            wrapMode: Text.WordWrap
        }

        Button {
            visible: previewController.recoveryCases.length > 0
            text: qsTr("중단 작업 확인")
            onClicked: {
                previewController.refreshRecoveryCases()
                recoveryDialog.open()
            }
        }

        Rectangle {
            id: dropZone
            property bool dragging: false
            Layout.fillWidth: true
            Layout.preferredHeight: 120
            radius: 18
            color: dragging ? "#e8f4e5" : "#ffffff"
            border.width: dragging ? 2 : 1
            border.color: dragging ? "#4d8f57" : "#d8dcd5"

            Column {
                anchors.centerIn: parent
                spacing: 8

                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("파일을 여기로 끌어놓으세요")
                    font.pixelSize: 19
                    font.bold: true
                    color: "#24382a"
                }
                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("원본 파일은 이동하거나 복사하지 않습니다.")
                    color: "#657065"
                }
            }

            DropArea {
                anchors.fill: parent
                onEntered: (drag) => {
                    const canRegister = drag.hasUrls && (drag.supportedActions & Qt.CopyAction)
                    dropZone.dragging = canRegister
                    if (canRegister)
                        drag.accept(Qt.CopyAction)
                    else
                        drag.accepted = false
                }
                onExited: dropZone.dragging = false
                onDropped: (drop) => {
                    dropZone.dragging = false
                    if (drop.hasUrls && (drop.supportedActions & Qt.CopyAction)) {
                        for (const url of drop.urls)
                            pendingFiles.addUrl(url)
                        drop.accept(Qt.CopyAction)
                    } else {
                        drop.accepted = false
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true

            Label {
                text: qsTr("파일정리대기")
                font.pixelSize: 19
                font.bold: true
                color: "#232323"
            }
            Item { Layout.fillWidth: true }
            Label {
                text: qsTr("%1개 대기 중").arg(pendingFiles.count)
                color: "#5a5a56"
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 170
            radius: 16
            color: "#ffffff"
            border.color: "#deded8"

            Label {
                anchors.centerIn: parent
                visible: pendingFiles.count === 0
                text: qsTr("아직 대기 중인 파일이 없어요.")
                color: "#7b817a"
            }

            ListView {
                id: pendingList
                anchors.fill: parent
                anchors.margins: 12
                clip: true
                spacing: 4
                visible: pendingFiles.count > 0
                model: pendingFiles

                delegate: Rectangle {
                    required property int index
                    required property string fileName
                    required property string fullPath
                    width: pendingList.width
                    height: 70
                    radius: 10
                    color: index % 2 === 0 ? "#f7f9f6" : "#ffffff"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 12
                        spacing: 12

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            Label {
                                text: fileName
                                font.bold: true
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }
                            Label {
                                text: fullPath
                                color: "#67716a"
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }
                        }

                        Button {
                            text: qsTr("목록에서 빼기")
                            onClicked: pendingFiles.removeAt(index)
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Label {
                text: qsTr("정리할 위치")
                color: "#232323"
            }
            TextField {
                id: destinationField
                Layout.fillWidth: true
                text: previewController.defaultDestinationRoot
                placeholderText: qsTr("정리할 폴더의 전체 경로")
                onTextEdited: previewController.clear()
            }
            Button {
                text: qsTr("미리보기 만들기")
                enabled: pendingFiles.count > 0
                onClicked: previewController.createPreview(destinationField.text)
            }
            Button {
                text: previewController.hasFrozenPlan ? qsTr("저장됨") : qsTr("계획 고정·저장")
                enabled: previewController.hasPreview && !previewController.hasFrozenPlan
                         && previewController.steps.length > 0
                onClicked: previewController.freezePreview()
            }
            Button {
                text: qsTr("저장 계획 검사")
                enabled: previewController.hasFrozenPlan && !previewController.hasApprovedPlan
                onClicked: previewController.verifySavedPlan()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Button {
                text: qsTr("파일 이동 승인")
                enabled: previewController.hasFrozenPlan && !previewController.hasApprovedPlan
                         && previewController.hasMovableSteps
                onClicked: previewController.approvePlan()
            }
            Button {
                text: qsTr("승인한 파일들 이동")
                enabled: previewController.hasApprovedPlan && pendingFiles.count > 0
                onClicked: previewController.moveApprovedFile()
            }
            Button {
                text: qsTr("마지막 이동 되돌리기")
                enabled: previewController.canUndo
                onClicked: previewController.undoLastMove()
            }
            Button {
                text: qsTr("작업 기록")
                onClicked: {
                    previewController.refreshHistory()
                    historyDialog.open()
                }
            }
            Item { Layout.fillWidth: true }
            Label {
                text: qsTr("중간에 실패하면 되돌리기를 시도하고, 불확실하면 확인을 요청해요.")
                color: "#67716a"
            }
        }

        Label {
            Layout.fillWidth: true
            text: previewController.message.length > 0
                  ? previewController.message
                  : qsTr("기본 규칙: 사진 → Pictures/연도/월, PDF → Documents/PDF")
            color: "#426349"
            wrapMode: Text.WordWrap
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 16
            color: "#ffffff"
            border.color: "#deded8"

            Label {
                anchors.centerIn: parent
                visible: !previewController.hasPreview
                text: qsTr("미리보기를 만들면 예상 목적지가 여기에 표시됩니다.")
                color: "#7b817a"
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                visible: previewController.hasPreview
                spacing: 6

                ListView {
                    id: previewList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: previewController.steps
                    delegate: ColumnLayout {
                        required property var modelData
                        width: previewList.width
                        spacing: 2
                        Label {
                            Layout.fillWidth: true
                            text: modelData.fileName + "  ·  " + modelData.status + "  ·  " + modelData.rule
                            font.bold: true
                            elide: Text.ElideMiddle
                        }
                        Label {
                            Layout.fillWidth: true
                            text: modelData.destination
                            color: "#67716a"
                            elide: Text.ElideMiddle
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    visible: previewController.steps.length === 0
                    text: qsTr("이동 대상으로 분류된 파일이 없어요.")
                    color: "#7b817a"
                }
                Label {
                    Layout.fillWidth: true
                    visible: previewController.warnings.length > 0
                    text: qsTr("경고: ") + previewController.warnings.join("\n")
                    color: "#9b5c23"
                    wrapMode: Text.WordWrap
                }
            }
        }

        Label {
            Layout.fillWidth: true
            text: pendingFiles.lastMessage.length > 0
                  ? pendingFiles.lastMessage
                  : qsTr("승인 전에는 파일을 이동하지 않습니다.")
            color: "#426349"
            wrapMode: Text.WordWrap
        }
    }

    Dialog {
        id: watchDialog
        anchors.centerIn: parent
        width: Math.min(root.width - 32, 720)
        height: Math.min(root.height - 32, 640)
        modal: true
        title: qsTr("폴더 감시 조건")
        standardButtons: Dialog.Close

        ColumnLayout {
            anchors.fill: parent
            spacing: 10

            Label {
                Layout.fillWidth: true
                text: qsTr("조건을 만족하면 정리를 제안합니다. 파일은 자동으로 이동하지 않습니다.")
                wrapMode: Text.WordWrap
            }
            ScrollView {
                id: watchSettingsScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                ColumnLayout {
                    width: watchSettingsScroll.availableWidth
                    spacing: 12
                    Label { text: qsTr("감시할 폴더") }
                    TextField {
                        id: watchFolderField
                        Layout.fillWidth: true
                        placeholderText: qsTr("예: C:/Users/이름/Downloads")
                    }
                    RowLayout {
                        CheckBox {
                            id: watchEnabledCheck
                            text: qsTr("자동 감시 켜기")
                        }
                        CheckBox {
                            id: watchRecursiveCheck
                            text: qsTr("하위 폴더 포함")
                        }
                    }
                    RowLayout {
                        Label { text: qsTr("정기 재검사 간격") }
                        SpinBox {
                            id: watchIntervalSpin
                            from: 1
                            to: 1440
                            value: 30
                        }
                        Label { text: qsTr("분") }
                    }
                    RowLayout {
                        Label { text: qsTr("조건 조합") }
                        ComboBox {
                            id: watchCombination
                            model: [qsTr("하나만 충족"), qsTr("모두 충족")]
                        }
                    }
                    RowLayout {
                        CheckBox {
                            id: watchCountCheck
                            text: qsTr("분류 가능한 파일이")
                        }
                        SpinBox {
                            id: watchCountSpin
                            from: 1
                            to: 1000000
                            value: 20
                            enabled: watchCountCheck.checked
                        }
                        Label { text: qsTr("개 이상") }
                    }
                    RowLayout {
                        CheckBox {
                            id: watchSizeCheck
                            text: qsTr("파일 전체 크기가")
                        }
                        SpinBox {
                            id: watchSizeSpin
                            from: 1
                            to: 1000000
                            value: 100
                            enabled: watchSizeCheck.checked
                        }
                        Label { text: qsTr("MiB 이상") }
                    }
                    RowLayout {
                        CheckBox {
                            id: watchAgeCheck
                            text: qsTr("가장 오래된 파일이")
                        }
                        SpinBox {
                            id: watchAgeSpin
                            from: 1
                            to: 1000000
                            value: 60
                            enabled: watchAgeCheck.checked
                        }
                        Label { text: qsTr("분 이상") }
                    }
                    Label { text: qsTr("제외할 파일명 패턴 · 쉼표로 구분") }
                    TextField {
                        id: watchExcludeField
                        Layout.fillWidth: true
                        placeholderText: qsTr("*.partial, *.crdownload, ~$*")
                    }
                }
            }
            Label {
                Layout.fillWidth: true
                text: watchController.statusMessage
                wrapMode: Text.WordWrap
                color: "#426349"
            }
            RowLayout {
                Button {
                    text: qsTr("조건 저장")
                    onClicked: watchController.saveSettings(
                        watchFolderField.text, watchEnabledCheck.checked,
                        watchRecursiveCheck.checked, watchIntervalSpin.value,
                        watchCombination.currentIndex === 1,
                        watchCountCheck.checked, watchCountSpin.value,
                        watchSizeCheck.checked, watchSizeSpin.value,
                        watchAgeCheck.checked, watchAgeSpin.value,
                        watchExcludeField.text)
                }
                Button {
                    text: qsTr("저장된 조건 지금 검사")
                    onClicked: watchController.checkNow()
                }
            }
        }
    }

    Dialog {
        id: watchProposalDialog
        anchors.centerIn: parent
        width: Math.min(root.width - 32, 700)
        height: Math.min(root.height - 32, 500)
        modal: true
        title: qsTr("정리 제안")
        standardButtons: Dialog.Close

        ColumnLayout {
            anchors.fill: parent
            Label {
                Layout.fillWidth: true
                text: watchController.proposalText
                wrapMode: Text.WordWrap
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: watchController.proposalFiles
                delegate: Label {
                    required property string modelData
                    width: parent.width
                    text: modelData
                    wrapMode: Text.WrapAnywhere
                    padding: 5
                }
            }
            RowLayout {
                Button {
                    text: qsTr("대기 목록에 담기")
                    enabled: watchController.proposalVisible
                    onClicked: {
                        watchController.addProposalToPending()
                        watchProposalDialog.close()
                    }
                }
                Button {
                    text: qsTr("이번 제안 닫기")
                    onClicked: {
                        watchController.dismissProposal()
                        watchProposalDialog.close()
                    }
                }
            }
        }
    }

    Dialog {
        id: historyDialog
        anchors.centerIn: parent
        width: Math.min(root.width - 32, 840)
        height: Math.min(root.height - 32, 600)
        modal: true
        title: qsTr("지난 작업 기록")
        standardButtons: Dialog.Close

        ColumnLayout {
            anchors.fill: parent
            spacing: 10

            Label {
                Layout.fillWidth: true
                text: previewController.historyMessage
                wrapMode: Text.WordWrap
                color: "#426349"
            }
            Button {
                text: qsTr("목록 새로고침")
                onClicked: previewController.refreshHistory()
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 12

                ListView {
                    id: historyList
                    Layout.preferredWidth: 280
                    Layout.fillHeight: true
                    clip: true
                    spacing: 6
                    model: previewController.historyEntries
                    delegate: Rectangle {
                        required property var modelData
                        width: historyList.width
                        height: 100
                        radius: 9
                        color: modelData.planId === previewController.selectedHistoryPlanId
                               ? "#e8f4e5" : "#f7f9f6"
                        border.color: "#d8dcd5"

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 9
                            spacing: 2
                            Label {
                                Layout.fillWidth: true
                                text: modelData.time + " · " + modelData.status
                                font.bold: true
                                elide: Text.ElideRight
                            }
                            Label {
                                text: qsTr("대상 %1개 · 현재 이동됨 %2개")
                                      .arg(modelData.fileCount).arg(modelData.appliedCount)
                                color: "#67716a"
                            }
                            Button {
                                text: qsTr("파일 보기")
                                onClicked: previewController.selectHistoryPlan(modelData.planId)
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 8

                    Label {
                        Layout.fillWidth: true
                        text: previewController.selectedHistoryPlanId.length > 0
                              ? qsTr("선택한 작업의 파일") : qsTr("왼쪽에서 작업을 선택해 주세요.")
                        font.bold: true
                    }
                    ScrollView {
                        id: historyStepScroll
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        Column {
                            width: historyStepScroll.availableWidth
                            spacing: 6

                            Repeater {
                                model: previewController.historySteps
                                delegate: Frame {
                                    required property var modelData
                                    width: parent.width

                                    ColumnLayout {
                                        width: parent.width
                                        Label {
                                            Layout.fillWidth: true
                                            text: modelData.fileName + " · " + modelData.status
                                            font.bold: true
                                            wrapMode: Text.WrapAnywhere
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            text: qsTr("원본: ") + modelData.source
                                            wrapMode: Text.WrapAnywhere
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            text: qsTr("목적지: ") + modelData.destination
                                            wrapMode: Text.WrapAnywhere
                                        }
                                    }
                                }
                            }
                        }
                    }
                    Button {
                        text: qsTr("이 작업 되돌리기")
                        enabled: previewController.canUndoSelectedHistory
                        onClicked: undoHistoryDialog.open()
                    }
                }
            }
        }
    }

    Dialog {
        id: undoHistoryDialog
        anchors.centerIn: parent
        width: Math.min(root.width - 80, 480)
        modal: true
        title: qsTr("선택한 작업 되돌리기")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: previewController.undoSelectedHistory()

        Label {
            width: parent.width
            text: qsTr("이 작업에서 이동된 파일 %1개를 원래 위치로 되돌릴까요?\n"
                       + "파일이 바뀌었거나 원본 경로가 사용 중이면 중단합니다.")
                  .arg(previewController.selectedHistoryFileCount)
            wrapMode: Text.WordWrap
        }
    }

    Dialog {
        id: recoveryDialog
        anchors.centerIn: parent
        width: Math.min(root.width - 32, 760)
        height: Math.min(root.height - 32, 560)
        modal: true
        title: qsTr("중단 작업 확인")
        standardButtons: Dialog.Close

        ColumnLayout {
            anchors.fill: parent
            spacing: 10

            Label {
                Layout.fillWidth: true
                text: qsTr("파일을 자동으로 옮기거나 삭제하지 않습니다. 원본과 목적지를 직접 확인해 주세요.")
                wrapMode: Text.WordWrap
            }
            Button {
                text: qsTr("파일 상태 다시 검사")
                onClicked: previewController.refreshRecoveryCases()
            }
            Label {
                Layout.fillWidth: true
                text: previewController.recoveryMessage
                wrapMode: Text.WordWrap
                color: "#9b5c23"
            }
            ScrollView {
                id: recoveryScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                Column {
                    width: recoveryScroll.availableWidth
                    spacing: 8

                    Repeater {
                        model: previewController.recoveryCases
                        delegate: Frame {
                            required property var modelData
                            width: parent.width

                            ColumnLayout {
                                width: parent.width
                                Label {
                                    Layout.fillWidth: true
                                    text: qsTr("작업 기록: ") + modelData.planId +
                                          qsTr(" · 파일 ") + modelData.partIndex + "/" + modelData.partCount
                                    font.bold: true
                                    elide: Text.ElideMiddle
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: qsTr("원본 (") + modelData.sourceState + "): " + modelData.source
                                    wrapMode: Text.WrapAnywhere
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: qsTr("목적지 (") + modelData.destinationState + "): " + modelData.destination
                                    wrapMode: Text.WrapAnywhere
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.detail
                                    wrapMode: Text.WordWrap
                                    color: "#9b5c23"
                                }
                                Button {
                                    text: qsTr("현재 상태로 기록 확정")
                                    visible: modelData.partIndex === 1
                                    enabled: modelData.canResolve
                                    onClicked: previewController.resolveRecoveryCase(modelData.planId)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
