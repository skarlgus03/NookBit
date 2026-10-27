import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

// 선택 블록의 설정과 목적지 선택창만 소유하는 패널.
Frame {
    id: inspector
    objectName: "ruleInspector"
    required property var editor
    property bool dark: false
    readonly property var theme: UiTheme.colors(dark)
    readonly property color surfaceColor: theme.surface
    readonly property color borderColor: theme.border
    readonly property color primaryText: theme.text
    readonly property color mutedText: theme.muted
    readonly property color nodeColor: theme.nodeSurface

    required property var destinations
    required property Item overlayHost
    property int folderTargetId: -1
    property string externalFolder: ""
    readonly property string selectedType: editor.selectedBlock ? editor.selectedBlock.type : ""
    readonly property string selectionKind: selectedType === "start" ? qsTr("시작점")
        : ["destination", "exclude"].includes(selectedType) ? qsTr("종착점")
        : selectedType === "otherwise" ? qsTr("예외 분기") : qsTr("조건 검사")
    readonly property string valueLabel: selectedType === "destination" ? qsTr("목적지 경로")
        : selectedType === "extension" ? qsTr("확장자") : selectedType === "name" ? qsTr("이름에 포함할 글자")
        : selectedType === "size" ? qsTr("파일 크기") : selectedType === "age" ? qsTr("수정 후 경과일") : qsTr("표시 값")
    padding: UiMetrics.spaceSection
    background: Rectangle {
        radius: UiMetrics.radiusPanel
        color: inspector.surfaceColor
        border.color: inspector.borderColor
    }
    ScrollView {
        id: inspectorScroll
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth
        contentHeight: inspectorContent.implicitHeight
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ColumnLayout {
            id: inspectorContent
            width: inspectorScroll.availableWidth
            spacing: UiMetrics.spaceCompact
            Label {
                Layout.fillWidth: true
                text: qsTr("블록 설정")
                font.pixelSize: UiMetrics.textSectionTitle
                font.bold: true
                color: inspector.primaryText
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: UiMetrics.borderNormal
                color: inspector.borderColor
            }
            Rectangle {
                objectName: "ruleSelectionSummary"
                Layout.fillWidth: true
                implicitHeight: Math.max(UiMetrics.ruleSelectionHeaderHeight, selectionText.implicitHeight + UiMetrics.spaceGroup * 2)
                color: inspector.theme.window
                radius: UiMetrics.radiusControl
                ColumnLayout {
                    id: selectionText
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: UiMetrics.spaceGroup
                    spacing: UiMetrics.spaceExtraSmall
                    Label {
                        Layout.fillWidth: true
                        text: inspector.editor.selectedIds.length > 1 ? qsTr("%1개 블록 선택됨").arg(inspector.editor.selectedIds.length)
                            : inspector.editor.selectedBlock ? inspector.editor.titleFor(inspector.selectedType) : qsTr("선택한 블록이 없어요")
                        wrapMode: Text.WordWrap
                        font.pixelSize: UiMetrics.textEmphasis
                        font.bold: true
                        color: inspector.primaryText
                    }
                    Label {
                        Layout.fillWidth: true
                        text: inspector.editor.selectedIds.length > 1 ? qsTr("함께 이동하거나 세로로 정렬할 수 있어요.")
                            : inspector.editor.selectedBlock ? inspector.selectionKind : qsTr("캔버스에서 블록을 선택해 주세요.")
                        wrapMode: Text.WordWrap
                        font.pixelSize: UiMetrics.textBody
                        color: inspector.mutedText
                    }
                }
            }
            Label {
                Layout.fillWidth: true
                visible: inspector.editor.selectedBlock !== null && inspector.editor.selectedBlock.type === "start"
                text: qsTr("파일 입력은 하나만 존재하는 시작점이에요. 위치를 옮기거나 오른쪽 연결점에서 조건을 이어 갈 수 있어요.")
                wrapMode: Text.WordWrap
                color: inspector.mutedText
            }
            Label {
                Layout.fillWidth: true
                text: inspector.valueLabel
                font.pixelSize: UiMetrics.textBody
                font.bold: true
                color: inspector.primaryText
                visible: inspector.editor.selectedBlock !== null && inspector.editor.selectedBlock.type !== "start"
                         && inspector.editor.selectedBlock.type !== "otherwise" && inspector.editor.selectedBlock.type
                         !== "exclude"
            }
            AppTextField {
                id: inspectorValue
                objectName: "ruleInspectorValue"
                Layout.fillWidth: true
                visible: inspector.editor.selectedBlock !== null && inspector.editor.selectedBlock.type !== "start"
                         && inspector.editor.selectedBlock.type !== "otherwise" && inspector.editor.selectedBlock.type
                         !== "exclude"
                enabled: visible
                placeholderText: inspector.editor.selectedBlock && inspector.editor.selectedBlock.type
                                 === "destination" ? (inspector.editor.selectedBlock.homeRelative !== false ? qsTr(
                                                                                                                  "사진/여행") :
                                                                                                              qsTr("D:/중요자료")) :
                                                     inspector.selectedType === "extension" ? qsTr("png, jpg, webp")
                    : inspector.selectedType === "name" ? qsTr("포함할 글자") : qsTr("숫자 입력")
                color: inspector.primaryText
                placeholderTextColor: inspector.mutedText
                onEditingFinished: {
                    if (inspector.editor.selectedBlock)
                        inspector.editor.updateValue(inspector.editor.selectedBlock.id, text);
                }
            }
            CheckBox {
                objectName: "destinationHomeCheck"
                Layout.fillWidth: true
                visible: inspector.editor.selectedBlock !== null && inspector.editor.selectedBlock.type
                         === "destination"
                text: qsTr("기본 정리 폴더 기준")
                checked: inspector.editor.selectedBlock !== null && inspector.editor.selectedBlock.homeRelative
                         !== false
                onClicked: {
                    const block = inspector.editor.selectedBlock;
                    const relative = inspector.destinations.relativeToHome(block.value);
                    inspector.editor.setDestination(block.id, checked ? (relative || block.value) :
                                                                        inspector.destinations.resolvedPath(block.value,
                                                                                                            true), checked);
                }
            }
            ThemeButton {
                objectName: "destinationFolderButton"
                Layout.fillWidth: true
                visible: inspector.editor.selectedBlock !== null && inspector.editor.selectedBlock.type
                         === "destination"
                text: qsTr("폴더 선택")
                onClicked: {
                    inspector.folderTargetId = inspector.editor.selectedBlock.id;
                    destinationFolderDialog.open();
                }
            }
            Label {
                Layout.fillWidth: true
                visible: inspector.editor.selectedBlock !== null && inspector.editor.selectedBlock.type
                         === "destination"
                text: visible ? qsTr("실제 경로: %1").arg(inspector.destinations.resolvedPath(
                                                          inspector.editor.selectedBlock.value,
                                                          inspector.editor.selectedBlock.homeRelative !== false)) : ""
                color: inspector.mutedText
                wrapMode: Text.WrapAnywhere
            }
            ThemeButton {
                Layout.fillWidth: true
                visible: inspector.editor.selectedBlock !== null && inspector.editor.selectedBlock.type
                         === "destination" && inspector.editor.selectedBlock.homeRelative === false
                text: qsTr("이 외부 폴더 허용")
                onClicked: {
                    inspector.editor.updateValue(inspector.editor.selectedBlock.id, inspectorValue.text);
                    inspector.folderTargetId = inspector.editor.selectedBlock.id;
                    inspector.externalFolder = inspector.editor.selectedBlock.value.trim();
                    externalDestinationConfirm.open();
                }
            }
            Label {
                Layout.fillWidth: true
                visible: ["size", "age"].includes(inspector.selectedType)
                text: qsTr("비교 방법")
                font.pixelSize: UiMetrics.textBody
                font.bold: true
                color: inspector.primaryText
            }
            AppComboBox {
                objectName: "sizeComparisonCombo"
                Layout.fillWidth: true
                visible: inspector.editor.selectedBlock !== null && (inspector.editor.selectedBlock.type === "size"
                                                                     || inspector.editor.selectedBlock.type === "age")
                textRole: "text"
                valueRole: "value"
                model: inspector.editor.selectedBlock && inspector.editor.selectedBlock.type === "age" ? [
                                                                                                             {
                                                                                                                 text: qsTr(
                                                                                                                           "이상 (≥)"),
                                                                                                                 value: "atLeast"
                                                                                                             },
                                                                                                             {
                                                                                                                 text: qsTr(
                                                                                                                           "미만 (<)"),
                                                                                                                 value: "lessThan"
                                                                                                             }
                                                                                                         ] : [
                                                                                                             {
                                                                                                                 text: qsTr(
                                                                                                                           "초과 (>)"),
                                                                                                                 value: "greaterThan"
                                                                                                             },
                                                                                                             {
                                                                                                                 text: qsTr(
                                                                                                                           "이상 (≥)"),
                                                                                                                 value: "atLeast"
                                                                                                             },
                                                                                                             {
                                                                                                                 text: qsTr(
                                                                                                                           "미만 (<)"),
                                                                                                                 value: "lessThan"
                                                                                                             },
                                                                                                             {
                                                                                                                 text: qsTr(
                                                                                                                           "이하 (≤)"),
                                                                                                                 value: "atMost"
                                                                                                             }
                                                                                                         ]
                currentIndex: inspector.editor.selectedBlock ? model.findIndex(entry => entry.value
                                                                                        === inspector.editor.selectedBlock.comparison) :
                                                               -1
                onActivated: inspector.editor.setComparison(inspector.editor.selectedId, currentValue)
            }
            Label {
                Layout.fillWidth: true
                visible: inspector.selectedType === "size"
                text: qsTr("단위")
                font.pixelSize: UiMetrics.textBody
                font.bold: true
                color: inspector.primaryText
            }
            AppComboBox {
                objectName: "sizeUnitCombo"
                Layout.fillWidth: true
                visible: inspector.editor.selectedBlock !== null && inspector.editor.selectedBlock.type === "size"
                textRole: "text"
                valueRole: "value"
                model: {
                    const choices = [
                              {
                                  text: "KB",
                                  value: "KB"
                              },
                              {
                                  text: "MB",
                                  value: "MB"
                              },
                              {
                                  text: "GB",
                                  value: "GB"
                              }
                          ];
                    const legacy = inspector.editor.selectedBlock ? inspector.editor.selectedBlock.unit : "";
                    if (legacy === "B")
                        choices.push({
                                         text: qsTr("B (바이트 · 기존 규칙)"),
                                         value: "B"
                                     });
                    if (legacy === "MiB")
                        choices.push({
                                         text: qsTr("MiB (1024 기준 · 기존 규칙)"),
                                         value: "MiB"
                                     });
                    return choices;
                }
                currentIndex: inspector.editor.selectedBlock ? model.findIndex(entry => entry.value === (
                                                                                            inspector.editor.selectedBlock.unit
                                                                                            || "MB")) : -1
                onActivated: inspector.editor.setSizeUnit(inspector.editor.selectedId, currentValue)
            }
            ThemeButton {
                Layout.fillWidth: true
                visible: inspector.editor.selectedBlock !== null && inspector.editor.selectedBlock.type !== "start"
                         && inspector.editor.selectedBlock.type !== "destination"
                         && inspector.editor.selectedBlock.type !== "exclude"
                text: inspector.editor.selectedBlock && inspector.editor.selectedBlock.enabled === false ? qsTr(
                                                                                                               "규칙 사용하기") :
                                                                                                           qsTr("규칙 끄기")
                onClicked: inspector.editor.toggleEnabled(inspector.editor.selectedId)
            }
            Label {
                Layout.fillWidth: true
                visible: inspector.selectedType === "otherwise"
                text: qsTr("같은 부모의 다른 조건이 모두 맞지 않을 때 이 가지로 진행해요.")
                wrapMode: Text.WordWrap
                font.pixelSize: UiMetrics.textBody
                color: inspector.mutedText
            }
            Label {
                Layout.fillWidth: true
                visible: inspector.editor.selectedBlock !== null && inspector.editor.selectedBlock.type === "exclude"
                text: qsTr("이 가지에 도달한 파일은 이동하지 않고, 전체 예외도 적용하지 않아요.")
                wrapMode: Text.WordWrap
                color: inspector.mutedText
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: UiMetrics.spaceControl
                Layout.preferredHeight: UiMetrics.borderNormal
                visible: inspector.editor.orderFor(inspector.editor.selectedId) > 0
                color: inspector.borderColor
            }
            Label {
                Layout.fillWidth: true
                visible: inspector.editor.orderFor(inspector.editor.selectedId) > 0
                text: qsTr("같은 부모에서 검사 순서: %1").arg(inspector.editor.orderFor(inspector.editor.selectedId))
                color: inspector.mutedText
            }
            RowLayout {
                Layout.fillWidth: true
                visible: inspector.editor.orderFor(inspector.editor.selectedId) > 0
                ThemeButton {
                    Layout.fillWidth: true
                    text: qsTr("순서 앞으로")
                    onClicked: inspector.editor.moveSelectedOrder(-1)
                }
                ThemeButton {
                    Layout.fillWidth: true
                    text: qsTr("순서 뒤로")
                    onClicked: inspector.editor.moveSelectedOrder(1)
                }
            }
        }
    }
    Connections {
        target: inspector.editor
        // 선택한 블록의 값을 입력 필드에 복사하는 함수.
        function onSelectedIdChanged() {
            const block = inspector.editor.blockForId(inspector.editor.selectedId);
            inspectorValue.text = block ? block.value : "";
        }
        // 폴더 선택으로 바뀐 경로만 현재 입력 필드에 반영하는 함수.
        function onDestinationUpdated(blockId, value) {
            if (blockId === inspector.editor.selectedId)
                inspectorValue.text = value;
        }
    }
    // 페이지를 떠나면 목적지 선택·외부 폴더 승인창을 닫는 함수.
    function dismissTransientUi() {
        destinationFolderDialog.close();
        externalDestinationConfirm.close();
    }
    component ThemeButton: RuleEditorButton {
        dark: inspector.dark
    }
    // 선택한 폴더를 홈 상대 경로로 표시하거나 외부 사용 확인을 요청하는 함수.
    function chooseDestinationFolder(path) {
        const target = editor.blockForId(folderTargetId);
        if (!target)
            return;
        const relative = inspector.destinations.relativeToHome(path);
        if (target.homeRelative !== false && relative !== "") {
            editor.setDestination(folderTargetId, relative, true);
        } else {
            externalFolder = path;
            externalDestinationConfirm.open();
        }
    }
    FolderDialog {
        id: destinationFolderDialog
        title: qsTr("목적지 폴더 선택")
        onAccepted: inspector.chooseDestinationFolder(inspector.destinations.folderPath(selectedFolder))
    }
    Dialog {
        id: externalDestinationConfirm
        parent: inspector.overlayHost
        anchors.centerIn: parent
        modal: true
        title: qsTr("별도 목적지 사용")
        width: Math.min(inspector.overlayHost.width - UiMetrics.spaceSection * 2, UiMetrics.operationPopupWidth)
        contentItem: ColumnLayout {
            spacing: UiMetrics.spaceGroup
            Label {
                Layout.fillWidth: true
                text: qsTr("이 폴더와 하위 폴더를 정리 목적지로 허용할까요? 규칙을 불러오는 것만으로는 허용되지 않아요.\n%1").arg(inspector.externalFolder)
                wrapMode: Text.WrapAnywhere
            }
            RowLayout {
                ThemeButton {
                    text: qsTr("취소")
                    onClicked: externalDestinationConfirm.close()
                }
                ThemeButton {
                    text: qsTr("허용하고 지정")
                    onClicked: {
                        if (inspector.destinations.allowFolder(inspector.externalFolder)) {
                            inspector.editor.setDestination(inspector.folderTargetId, inspector.externalFolder, false);
                            externalDestinationConfirm.close();
                        } else {
                            inspector.editor.statusText = qsTr("드라이브 루트나 변수가 아닌 전체 폴더 경로를 지정해 주세요.");
                        }
                    }
                }
            }
        }
    }
}
