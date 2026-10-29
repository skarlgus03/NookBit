import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

// 규칙 저장·불러오기·내보내기의 화면 흐름과 관련 확인창만 소유하는 연결부.
Item {
    id: actions
    required property var editor
    required property var controller
    required property var guideService
    property bool oldEditorFormDirty: false
    property string importErrorLocation: ""
    property string importErrorReason: ""
    property string exportErrorReason: ""
    readonly property alias guideStatus: rulesGuideDownload.status
    signal editorDraftReplaced
    property bool dark: false
    readonly property var theme: UiTheme.colors(dark)
    readonly property color surfaceColor: theme.surface
    readonly property color borderColor: theme.border
    readonly property color primaryText: theme.text
    readonly property color mutedText: theme.muted

    // 기존 폼의 미저장 초안이 있으면 교체 확인 후 저장하는 함수.
    function requestSave() {
        if (controller.dirty || oldEditorFormDirty)
            confirmSave.open();
        else
            saveAll();
    }
    // 파일을 선택하기 전에 현재 미저장 초안의 교체 여부를 확인하는 함수.
    function requestImport() {
        if (editor.graphDirty || controller.dirty || oldEditorFormDirty)
            confirmImportReplace.open();
        else
            importFileDialog.open();
    }
    // 현재 그래프를 저장본 변경 없이 내보낼 위치를 요청하는 함수.
    function requestExport() {
        exportFileDialog.open();
    }
    // 저장본을 다시 읽기 전에 초안 폐기 확인을 요청하는 함수.
    function requestReload() {
        confirmReload.open();
    }
    // 작성 가이드의 저장 위치를 요청하는 함수.
    function saveGuide() {
        rulesGuideDownload.saveDocument();
    }
    // 파일 선택과 문서 소유 확인창만 닫는 함수.
    function dismissTransientUi() {
        for (const popup of [confirmSave, confirmReload, importFileDialog, exportFileDialog, exportErrorDialog,
                             confirmImportReplace, importErrorDialog])
            popup.close();
        rulesGuideDownload.closeDialog();
    }
    component ThemeButton: RuleEditorButton {
        dark: actions.dark
    }
    // Controller에서 읽은 초안만 세션에 전달하고 기본 시점을 맞추는 함수.
    function loadGraphFromDraft() {
        editor.restoreGraph(controller.graph());
        Qt.callLater(function () {
            editor.resetDefaultView();
        });
    }
    // 그래프를 검증한 뒤 규칙 초안과 노드 위치를 한 번에 저장하는 함수.
    function saveAll() {
        if (!actions.controller.importGraph(editor.blocks, editor.edges)) {
            editor.statusText = actions.controller.message;
            return;
        }
        actions.editorDraftReplaced();
        editor.stagedGraph = true;
        if (!actions.controller.save()) {
            editor.stagedGraph = false;
            editor.statusText = actions.controller.message;
        }
    }
    Connections {
        target: actions.controller
        // 규칙 저장 후에는 확정한 그래프의 배치를 남기고 새 저장본을 다시 펼치는 함수.
        function onRulesSaved() {
            if (actions.editor.stagedGraph) {
                const layoutSaved = actions.controller.saveGraphLayout(actions.editor.blocks, actions.editor.edges);
                actions.loadGraphFromDraft();
                actions.editor.statusText = layoutSaved ? qsTr("규칙과 노드 위치를 모두 저장했어요.") : qsTr(
                                                              "규칙은 저장됐지만 노드 위치는 저장하지 못했어요.");
            } else if (actions.editor.graphDirty) {
                actions.editor.statusText = qsTr("규칙 저장본이 바뀌었어요. 화면의 미저장 그래프는 유지했으니 필요하면 다시 불러오세요.");
            } else {
                actions.loadGraphFromDraft();
            }
        }
    }
    GuideDownload {
        id: rulesGuideDownload
        objectName: "rulesGuideDownload"
        parent: actions
        guideKey: "rules"
        service: actions.guideService
    }

    Dialog {
        id: confirmSave
        modal: true
        anchors.centerIn: parent
        width: Math.min(actions.width - UiMetrics.dialogInset * 2, UiMetrics.dialogCompactWidth)
        title: qsTr("기존 편집기의 초안을 교체할까요?")
        standardButtons: Dialog.NoButton
        palette.window: actions.surfaceColor
        palette.windowText: actions.primaryText
        background: Rectangle {
            radius: UiMetrics.radiusPanel
            color: actions.surfaceColor
            border.color: actions.borderColor
        }
        contentItem: ColumnLayout {
            spacing: UiMetrics.spaceSection
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: actions.primaryText
                text: qsTr("기존 편집기에서 저장하지 않은 초안을 현재 그래프로 교체하고, 규칙과 노드 위치를 함께 저장합니다.")
            }
            RowLayout {
                Layout.fillWidth: true
                Item {
                    Layout.fillWidth: true
                }
                ThemeButton {
                    text: qsTr("취소")
                    onClicked: confirmSave.close()
                }
                ThemeButton {
                    text: qsTr("전체 저장")
                    onClicked: {
                        confirmSave.close();
                        actions.saveAll();
                    }
                }
            }
        }
    }

    Dialog {
        id: confirmReload
        modal: true
        anchors.centerIn: parent
        width: Math.min(actions.width - UiMetrics.dialogInset * 2, UiMetrics.dialogCompactWidth)
        title: qsTr("저장된 규칙 다시 열기")
        standardButtons: Dialog.NoButton
        palette.window: actions.surfaceColor
        palette.windowText: actions.primaryText
        background: Rectangle {
            radius: UiMetrics.radiusPanel
            color: actions.surfaceColor
            border.color: actions.borderColor
        }
        contentItem: ColumnLayout {
            spacing: UiMetrics.spaceSection
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: actions.primaryText
                text: qsTr("저장하지 않은 그래프와 기존 편집기의 규칙 초안을 버리고, 마지막 저장본을 다시 불러옵니다.")
            }
            RowLayout {
                Layout.fillWidth: true
                Item {
                    Layout.fillWidth: true
                }
                ThemeButton {
                    text: qsTr("취소")
                    onClicked: confirmReload.close()
                }
                ThemeButton {
                    text: qsTr("다시 열기")
                    onClicked: {
                        confirmReload.close();
                        actions.controller.reload();
                        actions.loadGraphFromDraft();
                        actions.editorDraftReplaced();
                    }
                }
            }
        }
    }

    FileDialog {
        id: importFileDialog
        title: qsTr("분류 규칙 JSON 불러오기")
        nameFilters: [qsTr("JSON 파일 (*.json)")]
        fileMode: FileDialog.OpenFile
        onAccepted: {
            const result = actions.controller.importJsonFile(selectedFile);
            if (!result.ok) {
                actions.importErrorLocation = result.location;
                actions.importErrorReason = result.reason;
                importErrorDialog.open();
                return;
            }
            actions.editorDraftReplaced();
            actions.loadGraphFromDraft();
            actions.editor.statusText = qsTr("검증을 통과한 JSON을 편집 초안으로 불러왔어요. 전체 저장 전까지 현재 규칙은 바뀌지 않아요.");
        }
    }

    FileDialog {
        id: exportFileDialog
        title: qsTr("현재 그래프를 JSON으로 내보내기")
        nameFilters: [qsTr("JSON 파일 (*.json)")]
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        onAccepted: {
            const result = actions.controller.exportGraph(actions.editor.blocks, actions.editor.edges, selectedFile);
            if (!result.ok) {
                actions.exportErrorReason = result.error;
                exportErrorDialog.open();
                return;
            }
            actions.editor.statusText = qsTr("현재 그래프를 JSON으로 내보냈어요. 앱에 적용된 규칙과 편집 초안은 바뀌지 않았어요.");
        }
    }

    Dialog {
        id: exportErrorDialog
        modal: true
        anchors.centerIn: parent
        width: Math.min(actions.width - UiMetrics.dialogInset * 2, UiMetrics.dialogStandardWidth)
        title: qsTr("JSON 내보내기 실패")
        standardButtons: Dialog.Close
        palette.window: actions.surfaceColor
        palette.windowText: actions.primaryText
        background: Rectangle {
            radius: UiMetrics.radiusPanel
            color: actions.surfaceColor
            border.color: actions.borderColor
        }
        contentItem: Label {
            width: parent.width
            text: actions.exportErrorReason
            color: actions.primaryText
            wrapMode: Text.WordWrap
        }
    }

    Dialog {
        id: confirmImportReplace
        modal: true
        anchors.centerIn: parent
        width: Math.min(actions.width - UiMetrics.dialogInset * 2, UiMetrics.dialogCompactWidth)
        title: qsTr("편집 중인 초안을 교체할까요?")
        standardButtons: Dialog.NoButton
        palette.window: actions.surfaceColor
        palette.windowText: actions.primaryText
        background: Rectangle {
            radius: UiMetrics.radiusPanel
            color: actions.surfaceColor
            border.color: actions.borderColor
        }
        contentItem: ColumnLayout {
            spacing: UiMetrics.spaceSection
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: actions.primaryText
                text: qsTr("새 JSON이 검증에 성공하면 현재 미저장 초안을 교체합니다. 검증 실패나 파일 선택 취소 시에는 그대로 유지해요.")
            }
            RowLayout {
                Layout.fillWidth: true
                Item {
                    Layout.fillWidth: true
                }
                ThemeButton {
                    text: qsTr("취소")
                    onClicked: confirmImportReplace.close()
                }
                ThemeButton {
                    text: qsTr("파일 선택")
                    onClicked: {
                        confirmImportReplace.close();
                        importFileDialog.open();
                    }
                }
            }
        }
    }

    Dialog {
        id: importErrorDialog
        modal: true
        anchors.centerIn: parent
        width: Math.min(actions.width - UiMetrics.dialogInset * 2, UiMetrics.dialogWideWidth)
        title: qsTr("규칙 파일 검증 실패")
        standardButtons: Dialog.Close
        palette.window: actions.surfaceColor
        palette.windowText: actions.primaryText
        background: Rectangle {
            radius: UiMetrics.radiusPanel
            color: actions.surfaceColor
            border.color: actions.borderColor
        }
        contentItem: ColumnLayout {
            spacing: UiMetrics.spaceCompact
            Label {
                Layout.fillWidth: true
                text: qsTr("문제 위치: %1").arg(actions.importErrorLocation)
                color: actions.primaryText
                wrapMode: Text.WrapAnywhere
                font.bold: true
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("원인: %1").arg(actions.importErrorReason)
                color: actions.primaryText
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("기존 규칙과 편집 초안은 변경하지 않았어요.")
                color: actions.mutedText
                wrapMode: Text.WordWrap
            }
        }
    }
}
