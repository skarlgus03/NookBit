import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 예전 규칙 폼의 선택·접힘·미저장 입력을 자체 관리하는 개발용 창.
Dialog {
    id: page
    enabled: !page.busy
    required property var controller
    property bool busy: false
    property bool dark: false
    readonly property var theme: UiTheme.colors(dark)
    readonly property bool dirty: ruleFormDirty || globalFormDirty
    property string selectedRulePath: ""
    property var selectedRuleDetail: ({})
    property var collapsedRulePaths: ({})
    property bool ruleFormDirty: false
    property bool globalFormDirty: false
    // 접힌 부모 아래의 노드를 화면에서 숨기는 함수.
    function ruleVisible(path) {
        const parts = path.split("/");
        for (let index = 1; index < parts.length; ++index) {
            if (collapsedRulePaths[parts.slice(0, index).join("/")])
                return false;
        }
        return true;
    }

    // 선택한 가지의 접힘 상태를 바꾸는 함수.
    function toggleRule(path) {
        const next = Object.assign({}, collapsedRulePaths);
        next[path] = !next[path];
        collapsedRulePaths = next;
    }

    // 입력 중인 규칙을 초안에 반영하고 유효하지 않으면 전환을 막는 함수.
    function commitRuleForm() {
        if (!ruleFormDirty || selectedRulePath.length === 0)
            return true;
        const saved = page.controller.edit(selectedRulePath, ruleIdField.text, ruleEnabledCheck.checked,
                                           ruleConditionType.currentValue, ruleConditionComparison.currentValue,
                                           ruleConditionValue.text, ruleSizeUnit.currentValue,
                                           ruleDestinationField.text);
        if (saved)
            ruleFormDirty = false;
        return saved;
    }

    // 선택한 규칙의 초안 값을 편집 입력창에 채우는 함수.
    function selectRule(path) {
        selectedRulePath = path;
        selectedRuleDetail = page.controller.detail(path);
        ruleIdField.text = selectedRuleDetail.id || "";
        ruleEnabledCheck.checked = selectedRuleDetail.enabled === true;
        ruleConditionType.currentIndex = Math.max(0, ruleConditionType.indexOfValue(selectedRuleDetail.type
                                                                                    || "extension"));
        ruleConditionComparison.currentIndex = Math.max(0, ruleConditionComparison.indexOfValue(
                                                            selectedRuleDetail.comparison || "greaterThan"));
        ruleConditionValue.text = selectedRuleDetail.value === undefined ? "" : String(selectedRuleDetail.value);
        ruleSizeUnit.currentIndex = Math.max(0, ruleSizeUnit.indexOfValue(selectedRuleDetail.sizeUnit || "MiB"));
        ruleDestinationField.text = selectedRuleDetail.destination || "";
        ruleFormDirty = false;
    }
    // 그래프 편집기가 초안을 교체하면 이전 폼의 선택과 미저장 입력만 초기화하는 함수.
    function resetDraftForm() {
        selectedRulePath = "";
        selectedRuleDetail = ({});
        ruleFormDirty = false;
        globalFormDirty = false;
        collapsedRulePaths = ({});
        globalDestinationField.text = controller.globalDestination;
    }

    anchors.centerIn: parent
    width: Math.min(parent.width - 32, 820)
    height: Math.min(parent.height - 32, 660)
    modal: true
    title: qsTr("분류 규칙 편집")
    standardButtons: Dialog.Close

    ColumnLayout {
        anchors.fill: parent
        spacing: UiMetrics.spaceControl

        Label {
            Layout.fillWidth: true
            text: qsTr("위에서부터 검사하고, 한 가지가 맞으면 그 아래로 내려갑니다. 목적지는 정리 기준 폴더 안의 상대 경로입니다.")
            wrapMode: Text.WordWrap
        }
        Label {
            Layout.fillWidth: true
            text: page.controller.message
            color: page.controller.dirty ? (page.theme.warning) : (page.theme.success)
            wrapMode: Text.WordWrap
        }
        AppButton {
            visible: page.controller.legacyRules
            text: qsTr("기존 규칙 백업 후 새 형식으로 변환")
            onClicked: page.controller.migrateLegacyRules()
        }
        RowLayout {
            Layout.fillWidth: true
            AppButton {
                text: qsTr("루트 규칙 추가")
                onClicked: {
                    if (page.commitRuleForm() && page.controller.add("")) {
                        page.collapsedRulePaths = ({});
                        page.selectRule("");
                    }
                }
            }
            AppButton {
                text: qsTr("루트 '그 외' 추가")
                onClicked: {
                    if (page.commitRuleForm() && page.controller.addOtherwise("")) {
                        page.collapsedRulePaths = ({});
                        page.selectRule("");
                    }
                }
            }
            Item {
                Layout.fillWidth: true
            }
            AppButton {
                text: qsTr("저장 전 변경 버리기")
                enabled: page.controller.dirty || page.ruleFormDirty || page.globalFormDirty
                onClicked: {
                    page.controller.reload();
                    page.selectRule("");
                    globalDestinationField.text = page.controller.globalDestination;
                    page.globalFormDirty = false;
                }
            }
            AppButton {
                text: qsTr("규칙 저장")
                enabled: page.controller.dirty || page.ruleFormDirty || page.globalFormDirty
                onClicked: {
                    if (!page.commitRuleForm())
                        return;
                    if (page.globalFormDirty)
                        page.controller.setGlobalDestination(globalDestinationField.text);
                    if (page.controller.save()) {
                        page.ruleFormDirty = false;
                        page.globalFormDirty = false;
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: qsTr("전체 예외 목적지")
            }
            AppTextField {
                id: globalDestinationField
                Layout.fillWidth: true
                text: page.controller.globalDestination
                placeholderText: qsTr("비우면 미분류")
                onTextEdited: page.globalFormDirty = true
            }
            AppButton {
                text: qsTr("적용")
                onClicked: {
                    page.controller.setGlobalDestination(globalDestinationField.text);
                    page.globalFormDirty = false;
                }
            }
        }
        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal

            ScrollView {
                SplitView.preferredWidth: 350
                SplitView.minimumWidth: 220
                clip: true
                Column {
                    width: parent.width
                    spacing: UiMetrics.spaceExtraSmall
                    Repeater {
                        model: page.controller.nodes
                        delegate: Row {
                            required property var modelData
                            width: parent.width
                            visible: page.ruleVisible(modelData.path)
                            height: visible ? 36 : 0
                            spacing: UiMetrics.spaceTiny
                            Item {
                                width: modelData.depth * 16
                                height: 1
                            }
                            AppButton {
                                width: 32
                                height: 34
                                flat: true
                                visible: modelData.childrenCount > 0
                                text: page.collapsedRulePaths[modelData.path] ? "▸" : "▾"
                                onClicked: page.toggleRule(modelData.path)
                            }
                            AppButton {
                                width: parent.width - modelData.depth * 16 - 36
                                height: 34
                                flat: true
                                highlighted: page.selectedRulePath === modelData.path
                                text: (modelData.enabled ? "" : "[꺼짐] ") + modelData.title + (modelData.exclude ? qsTr(
                                                                                                                      "  → 정리 제외") :
                                                                                                                  modelData.destination
                                                                                                                  ? "  → "
                                                                                                                    + modelData.destination :
                                                                                                                    "")
                                onClicked: {
                                    if (page.commitRuleForm())
                                        page.selectRule(modelData.path);
                                }
                            }
                        }
                    }
                }
            }

            ScrollView {
                SplitView.fillWidth: true
                clip: true
                ColumnLayout {
                    width: parent.width
                    spacing: UiMetrics.spaceControl
                    Label {
                        text: page.selectedRulePath.length > 0 ? qsTr("선택한 규칙") : qsTr("왼쪽에서 규칙을 선택하세요")
                        font.bold: true
                    }
                    Label {
                        text: qsTr("규칙 ID (영문·숫자)")
                    }
                    AppTextField {
                        id: ruleIdField
                        Layout.fillWidth: true
                        enabled: page.selectedRulePath.length > 0
                        onTextEdited: page.ruleFormDirty = true
                    }
                    CheckBox {
                        id: ruleEnabledCheck
                        text: qsTr("규칙 사용")
                        enabled: page.selectedRulePath.length > 0
                        onToggled: page.ruleFormDirty = true
                    }
                    Label {
                        text: qsTr("분류 조건")
                    }
                    AppComboBox {
                        id: ruleConditionType
                        Layout.fillWidth: true
                        enabled: page.selectedRulePath.length > 0 && !page.selectedRuleDetail.otherwise
                        textRole: "text"
                        valueRole: "value"
                        model: [
                            {
                                text: qsTr("확장자"),
                                value: "extension"
                            },
                            {
                                text: qsTr("파일 이름에 포함"),
                                value: "nameContains"
                            },
                            {
                                text: qsTr("파일 크기"),
                                value: "size"
                            },
                            {
                                text: qsTr("수정 후 지난 일수"),
                                value: "modifiedAgeDays"
                            }
                        ]
                        onCurrentValueChanged: ruleConditionComparison.currentIndex = 0
                        onActivated: page.ruleFormDirty = true
                    }
                    AppComboBox {
                        id: ruleConditionComparison
                        Layout.fillWidth: true
                        visible: ruleConditionType.currentValue === "size" || ruleConditionType.currentValue
                                 === "modifiedAgeDays"
                        textRole: "text"
                        valueRole: "value"
                        model: ruleConditionType.currentValue === "size" ? [
                                                                               {
                                                                                   text: qsTr("초과 (>)"),
                                                                                   value: "greaterThan"
                                                                               },
                                                                               {
                                                                                   text: qsTr("이상 (≥)"),
                                                                                   value: "atLeast"
                                                                               },
                                                                               {
                                                                                   text: qsTr("미만 (<)"),
                                                                                   value: "lessThan"
                                                                               },
                                                                               {
                                                                                   text: qsTr("이하 (≤)"),
                                                                                   value: "atMost"
                                                                               }
                                                                           ] : [
                                                                               {
                                                                                   text: qsTr("이상 (≥)"),
                                                                                   value: "atLeast"
                                                                               },
                                                                               {
                                                                                   text: qsTr("미만 (<)"),
                                                                                   value: "lessThan"
                                                                               }
                                                                           ]
                        onActivated: page.ruleFormDirty = true
                    }
                    Label {
                        text: ruleConditionType.currentValue === "extension" ? qsTr("확장자 (쉼표로 구분)") :
                                                                               ruleConditionType.currentValue
                                                                               === "nameContains" ? qsTr("이름에 포함할 글자") :
                                                                                                    ruleConditionType.currentValue
                                                                                                    === "size" ? qsTr(
                                                                                                                     "크기") : qsTr(
                                                                                                                     "수정 후 지난 일수")
                    }
                    AppTextField {
                        id: ruleConditionValue
                        Layout.fillWidth: true
                        enabled: page.selectedRulePath.length > 0 && !page.selectedRuleDetail.otherwise
                        inputMethodHints: ruleConditionType.currentValue === "size" || ruleConditionType.currentValue
                                          === "modifiedAgeDays" ? Qt.ImhDigitsOnly : Qt.ImhNone
                        placeholderText: ruleConditionType.currentValue === "extension" ? qsTr("예: png, jpg") : qsTr(
                                                                                              "값 입력")
                        onTextEdited: page.ruleFormDirty = true
                    }
                    AppComboBox {
                        id: ruleSizeUnit
                        Layout.fillWidth: true
                        visible: ruleConditionType.currentValue === "size"
                        textRole: "text"
                        valueRole: "value"
                        model: [
                            {
                                text: "MiB (1 MiB = 1,048,576 B)",
                                value: "MiB"
                            },
                            {
                                text: "B",
                                value: "B"
                            },
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
                        ]
                        onActivated: page.ruleFormDirty = true
                    }
                    Label {
                        text: page.selectedRuleDetail.exclude ? qsTr("정리 제외 · 이 규칙에 맞으면 이동하지 않음") : qsTr(
                                                                    "목적지 (자식이 있으면 비워 둠)")
                    }
                    AppTextField {
                        id: ruleDestinationField
                        Layout.fillWidth: true
                        enabled: page.selectedRulePath.length > 0 && !page.selectedRuleDetail.exclude
                        placeholderText: qsTr("예: Pictures/PNG")
                        onTextEdited: page.ruleFormDirty = true
                    }
                    AppButton {
                        text: qsTr("이 규칙 수정")
                        enabled: page.selectedRulePath.length > 0
                        onClicked: {
                            if (page.controller.edit(page.selectedRulePath, ruleIdField.text, ruleEnabledCheck.checked,
                                                     ruleConditionType.currentValue,
                                                     ruleConditionComparison.currentValue, ruleConditionValue.text,
                                                     ruleSizeUnit.currentValue, ruleDestinationField.text))
                                page.selectRule(page.selectedRulePath);
                        }
                    }
                    RowLayout {
                        AppButton {
                            text: qsTr("자식 추가")
                            enabled: page.selectedRulePath.length > 0
                            onClicked: {
                                if (page.commitRuleForm() && page.controller.add(page.selectedRulePath)) {
                                    page.collapsedRulePaths = ({});
                                    page.selectRule(page.selectedRulePath);
                                }
                            }
                        }
                        AppButton {
                            text: qsTr("그 아래 '그 외'")
                            enabled: page.selectedRulePath.length > 0
                            onClicked: {
                                if (page.commitRuleForm() && page.controller.addOtherwise(page.selectedRulePath)) {
                                    page.collapsedRulePaths = ({});
                                    page.selectRule(page.selectedRulePath);
                                }
                            }
                        }
                    }
                    RowLayout {
                        AppButton {
                            text: qsTr("위로")
                            enabled: page.selectedRulePath.length > 0
                            onClicked: {
                                if (page.commitRuleForm() && page.controller.move(page.selectedRulePath, -1)) {
                                    page.collapsedRulePaths = ({});
                                    page.selectRule("");
                                }
                            }
                        }
                        AppButton {
                            text: qsTr("아래로")
                            enabled: page.selectedRulePath.length > 0
                            onClicked: {
                                if (page.commitRuleForm() && page.controller.move(page.selectedRulePath, 1)) {
                                    page.collapsedRulePaths = ({});
                                    page.selectRule("");
                                }
                            }
                        }
                        AppButton {
                            text: qsTr("규칙 제거")
                            enabled: page.selectedRulePath.length > 0
                            onClicked: {
                                page.controller.remove(page.selectedRulePath);
                                page.collapsedRulePaths = ({});
                                page.selectRule("");
                            }
                        }
                    }
                }
            }
        }
    }
}
