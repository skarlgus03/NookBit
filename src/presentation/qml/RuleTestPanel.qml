import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

// 실제·가상 시험 입력과 시험용 파일 선택창을 소유하는 떠 있는 패널.
Popup {
    id: panel
    required property var editor
    property bool dark: false
    readonly property var theme: UiTheme.colors(dark)
    readonly property color surfaceColor: theme.surface
    readonly property color canvasColor: theme.editorCanvas
    readonly property color borderColor: theme.border
    readonly property color primaryText: theme.text
    readonly property color mutedText: theme.muted

    required property var controller
    objectName: "ruleTestPanel"
    x: 18
    y: 18
    width: Math.max(1, Math.min(panel.parent.width - 20, 420))
    padding: UiMetrics.spaceSection
    modal: false
    dim: false
    focus: false
    closePolicy: Popup.CloseOnEscape
    onOpened: panel.clampToCanvas()
    onHeightChanged: {
        if (opened)
            panel.clampToCanvas();
    }
    background: Rectangle {
        radius: UiMetrics.radiusPanel
        color: panel.surfaceColor
        border.color: panel.borderColor
    }
    contentItem: ColumnLayout {
        spacing: UiMetrics.spaceGroup
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 38
            color: "transparent"
            MouseArea {
                id: testPanelHandle
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                cursorShape: Qt.SizeAllCursor
                property real pressX: 0
                property real pressY: 0
                onPressed: mouse => {
                    pressX = mouse.x;
                    pressY = mouse.y;
                }
                onPositionChanged: mouse => {
                    if (!pressed)
                        return;
                    const point = mapToItem(panel.parent, mouse.x, mouse.y);
                    panel.x = Math.max(0, Math.min(point.x - pressX, panel.parent.width - panel.width));
                    panel.y = Math.max(0, Math.min(point.y - pressY, panel.parent.height - panel.height));
                }
            }
            RowLayout {
                anchors.fill: parent
                Label {
                    Layout.fillWidth: true
                    text: qsTr("규칙 테스트")
                    font.bold: true
                    color: panel.primaryText
                }
                ThemeButton {
                    text: "×"
                    implicitWidth: 36
                    onClicked: panel.close()
                }
            }
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("파일은 이동하지 않아요 · 제목을 잡고 창을 옮길 수 있어요")
            color: panel.mutedText
            wrapMode: Text.WordWrap
        }
        RowLayout {
            ThemeButton {
                text: qsTr("실제 파일")
                font.bold: panel.editor.testMode === 0
                onClicked: panel.editor.testMode = 0
            }
            ThemeButton {
                text: qsTr("가상 파일")
                font.bold: panel.editor.testMode === 1
                onClicked: panel.editor.testMode = 1
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 100
            visible: panel.editor.testMode === 0
            radius: UiMetrics.radiusControl
            color: panel.canvasColor
            border.color: panel.borderColor
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: UiMetrics.spaceCompact
                Label {
                    Layout.fillWidth: true
                    text: panel.editor.testFileUrl.toString() === "" ? qsTr("시험할 파일이나 폴더 하나를 놓으세요") :
                                                                       panel.editor.testFileUrl.toString()
                    color: panel.primaryText
                    elide: Text.ElideMiddle
                }
                ThemeButton {
                    text: qsTr("파일 선택")
                    onClicked: ruleTestFileDialog.open()
                }
            }
            DropArea {
                anchors.fill: parent
                onDropped: drop => {
                    if (drop.urls.length !== 1) {
                        panel.editor.testError = qsTr("파일이나 폴더 하나만 놓아 주세요.");
                        return;
                    }
                    panel.editor.testFileUrl = drop.urls[0];
                    panel.editor.testError = "";
                    panel.editor.testResult = ({});
                    panel.editor.testTraceBlockIds = [];
                }
            }
        }
        GridLayout {
            visible: panel.editor.testMode === 1
            Layout.fillWidth: true
            columns: 2
            Label {
                text: qsTr("대상 종류")
                color: panel.primaryText
            }
            AppComboBox {
                id: testItemType
                objectName: "ruleTestItemTypeCombo"
                Layout.fillWidth: true
                model: [qsTr("파일"), qsTr("폴더")]
                onActivated: {
                    panel.editor.testResult = ({});
                    panel.editor.testTraceBlockIds = [];
                }
            }
            Label {
                text: qsTr("이름")
                color: panel.primaryText
            }
            AppTextField {
                id: testNameField
                Layout.fillWidth: true
                text: "example.png"
                placeholderText: qsTr("확장자를 포함한 이름")
                onTextEdited: {
                    panel.editor.testResult = ({});
                    panel.editor.testTraceBlockIds = [];
                }
            }
            Label {
                text: qsTr("파일 크기 (B)")
                enabled: testItemType.currentIndex === 0
                color: panel.primaryText
            }
            AppTextField {
                id: testSizeField
                enabled: testItemType.currentIndex === 0
                Layout.fillWidth: true
                text: "1048576"
                inputMethodHints: Qt.ImhDigitsOnly
                onTextEdited: {
                    panel.editor.testResult = ({});
                    panel.editor.testTraceBlockIds = [];
                }
            }
            Label {
                text: qsTr("수정 후 경과일")
                color: panel.primaryText
            }
            AppTextField {
                id: testAgeField
                Layout.fillWidth: true
                text: "0"
                inputMethodHints: Qt.ImhDigitsOnly
                onTextEdited: {
                    panel.editor.testResult = ({});
                    panel.editor.testTraceBlockIds = [];
                }
            }
        }
        ThemeButton {
            Layout.fillWidth: true
            text: qsTr("이 규칙으로 시험")
            onClicked: panel.runTest()
        }
        Label {
            Layout.fillWidth: true
            visible: panel.editor.testError !== ""
            text: panel.editor.testError
            color: panel.theme.danger
            wrapMode: Text.WordWrap
        }
        ColumnLayout {
            visible: panel.editor.testResult.ok === true
            Layout.fillWidth: true
            spacing: UiMetrics.spaceSmall
            Label {
                Layout.fillWidth: true
                font.bold: true
                color: panel.primaryText
                text: panel.editor.testResult.outcome === "destination" ? qsTr("목적지: %1").arg(
                                                                              panel.editor.testResult.destination) :
                                                                          panel.editor.testResult.outcome
                                                                          === "excluded" ? qsTr("결과: 정리 제외") : qsTr(
                                                                                               "결과: 미분류")
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                color: panel.mutedText
                text: panel.editor.testResult.matchedRuleId === "global-otherwise" ? qsTr("선택 경로: 파일 입력 → 전체 예외") :
                                                                                     panel.editor.testResult.path
                                                                                     && panel.editor.testResult.path.length
                                                                                     > 0 ? qsTr("선택 경로: 파일 입력 → %1").arg(
                                                                                               panel.editor.testResult.path.join(
                                                                                                   " → ")) : qsTr(
                                                                                               "선택 경로: 파일 입력 → 일치하는 규칙 없음")
                wrapMode: Text.WordWrap
            }
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("현재 화면의 미저장 그래프를 시험합니다. 파일을 변경하거나 규칙을 저장하지 않습니다.")
            color: panel.mutedText
            wrapMode: Text.WordWrap
        }
    }
    // 패널 전체가 캔버스 안에 보이도록 위치를 보정하는 함수.
    function clampToCanvas() {
        x = Math.max(0, Math.min(x, parent.width - width));
        y = Math.max(0, Math.min(y, parent.height - height));
    }
    // 입력 파일 선택창까지 포함해 페이지 소유 보조 창을 닫는 함수.
    function dismissTransientUi() {
        close();
        ruleTestFileDialog.close();
    }
    // 현재 그래프와 시험 입력을 Controller에 전달하고 결과만 편집 상태에 반영하는 함수.
    function runTest() {
        if (panel.editor.testMode === 0 && panel.editor.testFileUrl.toString() === "") {
            panel.editor.testError = qsTr("먼저 시험할 파일 하나를 선택하거나 놓아 주세요.");
            panel.editor.testResult = ({});
            panel.editor.testTraceBlockIds = [];
            return;
        }
        const result = panel.controller.testGraph(panel.editor.blocks, panel.editor.edges, panel.editor.testMode === 0
                                                  ? panel.editor.testFileUrl : "", testNameField.text,
                                                  testItemType.currentIndex === 1 ? "0" : testSizeField.text, testAgeField.text,
                                                  testItemType.currentIndex === 1);
        panel.editor.applyTestResult(result);
    }
    Connections {
        target: panel.parent
        // 캔버스 크기가 바뀌면 열린 패널의 위치를 보정하는 함수.
        function onWidthChanged() {
            if (panel.opened)
                panel.clampToCanvas();
        }
        // 캔버스 높이가 바뀌면 열린 패널의 위치를 보정하는 함수.
        function onHeightChanged() {
            if (panel.opened)
                panel.clampToCanvas();
        }
    }
    component ThemeButton: RuleEditorButton {
        dark: panel.dark
    }
    FileDialog {
        id: ruleTestFileDialog
        title: qsTr("시험할 파일 선택")
        fileMode: FileDialog.OpenFile
        onAccepted: {
            panel.editor.testFileUrl = selectedFile;
            panel.editor.testError = "";
            panel.editor.testResult = ({});
            panel.editor.testTraceBlockIds = [];
        }
    }
}
