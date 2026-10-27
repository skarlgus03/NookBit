import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 개발용 정리 진단과 테스트 요청을 일반 화면에서 분리한 페이지.
ScrollView {
    id: page
    required property var controller
    required property var pending
    required property var recovery
    property bool dark: false
    readonly property var theme: UiTheme.colors(dark)
    signal bubbleTestRequested
    signal legacyEditorRequested
    signal rulesRequested
    signal recoveryRequested

    clip: true
    contentWidth: availableWidth
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

    ColumnLayout {
        width: page.availableWidth
        spacing: UiMetrics.spaceSection

        ColumnLayout {
            objectName: "developerTools"
            Layout.fillWidth: true
            visible: page.visible
            Label {
                text: qsTr("개발용 기존 정리 화면 · Ctrl+Shift+D로 접기")
                font.bold: true
            }
            AppButton {
                text: qsTr("말풍선 배치 테스트")
                onClicked: {
                    page.bubbleTestRequested();
                }
            }
            AppButton {
                text: qsTr("기존 규칙 폼 열기")
                onClicked: page.legacyEditorRequested()
            }

            Label {
                text: qsTr("파일 정리")
                font.pixelSize: UiMetrics.textPageTitle
                font.bold: true
                color: page.theme.text
            }

            Label {
                text: qsTr("정리할 파일을 먼저 대기 목록에 담아 주세요.")
                font.pixelSize: UiMetrics.textEmphasis
                color: page.theme.muted
            }

            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: qsTr("현재 적용 중인 규칙: ") + page.controller.activeRuleLabel
                    color: page.theme.success
                    elide: Text.ElideMiddle
                }
                AppButton {
                    text: qsTr("규칙 편집기로")
                    onClicked: page.rulesRequested()
                }
            }

            Label {
                Layout.fillWidth: true
                visible: page.recovery.recoveryNotice.length > 0
                text: page.recovery.recoveryNotice
                color: page.theme.warning
                wrapMode: Text.WordWrap
            }

            AppButton {
                visible: page.recovery.recoveryCases.length > 0
                text: qsTr("중단 작업 확인")
                onClicked: {
                    page.recoveryRequested();
                }
            }

            Rectangle {
                id: dropZone
                property bool dragging: false
                Layout.fillWidth: true
                Layout.preferredHeight: 120
                radius: 18
                color: dragging ? (page.theme.menuSelected) : page.theme.surface
                border.width: dragging ? 2 : 1
                border.color: dragging ? page.theme.accent : page.theme.border

                Column {
                    anchors.centerIn: parent
                    spacing: UiMetrics.spaceControl

                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: qsTr("파일을 여기로 끌어놓으세요")
                        font.pixelSize: UiMetrics.textSectionTitle
                        font.bold: true
                        color: page.theme.text
                    }
                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: qsTr("원본 파일은 이동하거나 복사하지 않습니다.")
                        color: page.theme.muted
                    }
                }

                DropArea {
                    anchors.fill: parent
                    onEntered: drag => {
                        const canRegister = drag.hasUrls && (drag.supportedActions & Qt.CopyAction);
                        dropZone.dragging = canRegister;
                        if (canRegister)
                            drag.accept(Qt.CopyAction);
                        else
                            drag.accepted = false;
                    }
                    onExited: dropZone.dragging = false
                    onDropped: drop => {
                        dropZone.dragging = false;
                        if (drop.hasUrls && (drop.supportedActions & Qt.CopyAction)) {
                            for (const url of drop.urls)
                                page.pending.addUrl(url);
                            drop.accept(Qt.CopyAction);
                        } else {
                            drop.accepted = false;
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true

                Label {
                    text: qsTr("1. 대기 목록")
                    font.pixelSize: UiMetrics.textSectionTitle
                    font.bold: true
                    color: page.theme.text
                }
                Item {
                    Layout.fillWidth: true
                }
                Label {
                    text: qsTr("%1개 대기 중").arg(page.pending.count)
                    color: page.theme.muted
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 170
                radius: 16
                color: page.theme.surface
                border.color: page.theme.border

                Label {
                    anchors.centerIn: parent
                    visible: page.pending.count === 0
                    text: qsTr("아직 대기 중인 파일이 없어요.")
                    color: page.theme.muted
                }

                ListView {
                    id: pendingList
                    anchors.fill: parent
                    anchors.margins: UiMetrics.spaceGroup
                    clip: true
                    spacing: UiMetrics.spaceExtraSmall
                    visible: page.pending.count > 0
                    model: page.pending

                    delegate: Rectangle {
                        required property int index
                        required property string fileName
                        required property string fullPath
                        required property bool available
                        width: pendingList.width
                        height: 70
                        radius: UiMetrics.radiusPanel
                        color: !available ? (page.theme.dangerSurface) : index % 2 === 0 ? (page.theme.alternate) :
                                                                                           page.theme.surface

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 16
                            anchors.rightMargin: 12
                            spacing: UiMetrics.spaceGroup

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: UiMetrics.spaceExtraSmall
                                Label {
                                    text: available ? fileName : qsTr("[파일 없음] %1").arg(fileName)
                                    font.bold: true
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }
                                Label {
                                    text: fullPath
                                    color: page.theme.muted
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }
                            }

                            AppButton {
                                text: qsTr("목록에서 빼기")
                                onClicked: page.pending.removeAt(index)
                            }
                        }
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                visible: page.pending.unavailableCount > 0
                text: qsTr("찾을 수 없는 파일 %1개가 있어요. 해당 항목을 목록에서 빼거나 파일을 원래 위치에 돌려놓아 주세요.").arg(
                          page.pending.unavailableCount)
                color: page.theme.danger
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                visible: page.pending.storageNotice.length > 0
                text: page.pending.storageNotice
                color: page.theme.danger
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: UiMetrics.spaceGroup
                Label {
                    text: qsTr("같은 이름의 파일이 있으면")
                    color: page.theme.text
                }
                AppComboBox {
                    textRole: "label"
                    valueRole: "key"
                    model: [
                        {
                            label: qsTr("이름 변경"),
                            key: "rename"
                        },
                        {
                            label: qsTr("건너뛰기"),
                            key: "skip"
                        },
                        {
                            label: qsTr("작업 중단"),
                            key: "abort"
                        }
                    ]
                    currentIndex: page.controller.collisionPolicy === "skip" ? 1 : page.controller.collisionPolicy
                                                                               === "abort" ? 2 : 0
                    onActivated: page.controller.collisionPolicy = currentValue
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("규칙에 별도 처리가 지정되어 있으면 그 설정을 우선해요.")
                    color: page.theme.muted
                    wrapMode: Text.WordWrap
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: UiMetrics.spaceGroup

                Label {
                    text: qsTr("정리할 위치")
                    color: page.theme.text
                }
                AppTextField {
                    id: destinationField
                    Layout.fillWidth: true
                    text: page.controller.defaultDestinationRoot
                    placeholderText: qsTr("정리할 폴더의 전체 경로")
                    onTextEdited: page.controller.clear()
                }
                AppButton {
                    text: qsTr("미리보기 만들기")
                    enabled: page.pending.count > 0
                    onClicked: page.controller.createPreview(destinationField.text)
                }
                AppButton {
                    text: qsTr("정리하기")
                    enabled: page.controller.hasPreview && page.controller.hasMovableSteps &&
                             !page.controller.awaitingConfirmation
                    onClicked: {
                        page.controller.prepareSort();
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: UiMetrics.spaceGroup

                AppButton {
                    text: qsTr("마지막 이동 되돌리기")
                    enabled: page.controller.canUndo
                    onClicked: page.controller.undoLastMove()
                }
                Item {
                    Layout.fillWidth: true
                }
                Label {
                    text: qsTr("중간에 실패하면 되돌리기를 시도하고, 불확실하면 확인을 요청해요.")
                    color: page.theme.muted
                }
            }

            Label {
                Layout.fillWidth: true
                text: page.controller.message.length > 0 ? page.controller.message : qsTr(
                                                               "저장된 규칙으로 미리보기를 만듭니다. 편집 중인 초안은 전체 저장 후 적용돼요.")
                color: page.theme.success
                wrapMode: Text.WordWrap
            }
        }
        Label {
            text: qsTr("분류 미리보기")
            font.pixelSize: UiMetrics.textSectionTitle
            font.bold: true
            color: page.theme.text
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 300
            radius: 16
            color: page.theme.surface
            border.color: page.theme.border

            Label {
                anchors.centerIn: parent
                visible: !page.controller.hasPreview
                text: qsTr("미리보기를 만들면 예상 목적지가 여기에 표시됩니다.")
                color: page.theme.muted
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: UiMetrics.spaceGroup
                visible: page.controller.hasPreview
                spacing: UiMetrics.spaceSmall

                ListView {
                    id: previewList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: UiMetrics.spaceControl
                    model: page.controller.outcomes
                    delegate: Rectangle {
                        required property var modelData
                        width: previewList.width
                        height: previewRow.implicitHeight + 20
                        radius: UiMetrics.radiusPanel
                        color: modelData.statusKey === "planned" ? page.theme.successSurface : page.theme.warningSurface
                        border.color: modelData.statusKey === "planned" ? (page.theme.successBorder) : page.theme.border
                        ColumnLayout {
                            id: previewRow
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: UiMetrics.spaceCompact
                            spacing: UiMetrics.spaceExtraSmall
                            RowLayout {
                                Layout.fillWidth: true
                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.fileName
                                    font.bold: true
                                    elide: Text.ElideMiddle
                                }
                                Label {
                                    text: modelData.status
                                    font.bold: true
                                    color: modelData.statusKey === "planned" ? (page.theme.success) : (
                                                                                   page.theme.warning)

                                }
                            }
                            Label {
                                Layout.fillWidth: true
                                text: modelData.reason
                                color: page.theme.text
                                elide: Text.ElideRight
                            }
                            Label {
                                Layout.fillWidth: true
                                text: qsTr("원본: ") + modelData.source
                                color: page.theme.muted
                                elide: Text.ElideMiddle
                            }
                            Label {
                                Layout.fillWidth: true
                                text: modelData.destination.length > 0 ? qsTr("예상 목적지: ") + modelData.destination : qsTr(
                                                                             "이동하지 않음 · 원본 유지")
                                color: page.theme.muted
                                elide: Text.ElideMiddle
                            }
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    visible: page.controller.outcomes.length === 0
                    text: qsTr("표시할 파일별 검사 결과가 없어요. 위의 경고를 확인해 주세요.")
                    color: page.theme.muted
                }
                Label {
                    Layout.fillWidth: true
                    visible: page.controller.warnings.length > 0
                    text: qsTr("추가 경고: ") + page.controller.warnings.join("\n")
                    color: page.theme.warning
                    wrapMode: Text.WordWrap
                }
            }
        }

        Label {
            Layout.fillWidth: true
            text: page.pending.lastMessage.length > 0 ? page.pending.lastMessage : qsTr("승인 전에는 파일을 이동하지 않습니다.")
            color: page.theme.success
            wrapMode: Text.WordWrap
        }
    }
}
