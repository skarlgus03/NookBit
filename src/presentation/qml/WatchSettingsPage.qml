import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs as NativeDialogs

// 감시 조건의 입력 초안과 저장 요청을 소유하는 페이지.
PageSurface {
    id: page
    required property var controller
    property bool initialized: false
    property bool saving: false
    property bool customInterval: false
    property var loadedValues: []
    readonly property var draft: initialized ? draftValues() : []
    readonly property int changeCount: loadedValues.length ? draft.reduce((count, value, index) => count + (value !== loadedValues[index] ? 1 : 0), 0) + (watchExcludeField.pending ? 1 : 0) : 0
    // 화면 입력만 비교하고 저장값은 바꾸지 않는 함수.
    function draftValues() {
        return [watchFolderField.text, watchEnabledCheck.checked, watchRecursiveCheck.checked,
            watchIntervalSpin.value, watchCombination.currentIndex, watchCountCheck.checked,
            watchCountSpin.value, watchSizeCheck.checked, watchSizeSpin.value, watchAgeCheck.checked,
            watchAgeSpin.value, watchExcludeField.text, watchSizeUnit.currentValue]
    }
    // 입력 중인 패턴까지 확정한 뒤 기존 저장 계약으로 전달하는 함수.
    function save() {
        watchExcludeField.commitPending()
        saving = true
        controller.saveSettings(watchFolderField.text, watchEnabledCheck.checked,
            watchRecursiveCheck.checked, watchIntervalSpin.value, watchCombination.currentIndex === 1,
            watchCountCheck.checked, watchCountSpin.value, watchSizeCheck.checked, watchSizeSpin.value,
            watchAgeCheck.checked, watchAgeSpin.value, watchExcludeField.text, watchSizeUnit.currentValue)
        saving = false
    }
    // 저장된 감시 조건을 이 페이지의 입력 초안에 복사하는 함수.
    function reload() {
        watchFolderField.text = page.controller.folder;
        watchEnabledCheck.checked = page.controller.enabled;
        watchRecursiveCheck.checked = page.controller.recursive;
        watchIntervalSpin.value = page.controller.rescanIntervalMinutes;
        watchCombination.currentIndex = page.controller.combineAll ? 1 : 0;
        watchCountCheck.checked = page.controller.countEnabled;
        watchCountSpin.value = page.controller.minimumFileCount;
        watchSizeCheck.checked = page.controller.sizeEnabled;
        watchSizeSpin.value = page.controller.minimumTotalSize;
        watchSizeUnit.currentIndex = watchSizeUnit.indexOfValue(page.controller.totalSizeUnit);
        watchAgeCheck.checked = page.controller.ageEnabled;
        watchAgeSpin.value = page.controller.minimumAgeMinutes;
        watchExcludeField.text = page.controller.excludePatterns;
        watchExcludeField.clearPending();
        customInterval = ![1, 5, 15].includes(watchIntervalSpin.value);
        loadedValues = draftValues();
    }
    Component.onCompleted: {
        initialized = true;
        if (visible)
            reload();
    }
    onVisibleChanged: if (initialized) {
        if (visible) reload()
        else folderDialog.close()
    }
    Connections {
        target: page.controller
        // 현재 화면이 요청한 저장 성공에서만 초안을 새 저장값으로 교체하는 함수.
        function onSettingsChanged() { if (page.saving) page.reload() }
    }

    objectName: "watchSettingsDialog"

    background: Rectangle { color: page.theme.window }
    NativeDialogs.FolderDialog {
        id: folderDialog
        title: qsTr("감시할 폴더 선택")
        onAccepted: watchFolderField.text = page.controller.folderPath(selectedFolder)
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: UiMetrics.spaceSection
        spacing: UiMetrics.spaceGroup
        AppButton {
            objectName: "watchOpenProposalButton"
            visible: page.controller.proposalVisible
            text: qsTr("정리 제안 확인")
            dark: page.dark
            onClicked: page.dialogRequested("watch.proposal")
        }
        ScrollView {
            id: watchSettingsScroll
            objectName: "watchSettingsScroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                width: watchSettingsScroll.availableWidth
                spacing: UiMetrics.spaceSection
                WatchStatusCard { Layout.fillWidth: true; controller: page.controller; dark: page.dark; onCheckRequested: page.controller.checkNow() }
                SettingsSection {
                    Layout.fillWidth: true
                    title: qsTr("어디를")
                    dark: page.dark
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: UiMetrics.spaceControl
                        NavigationIcon { Layout.preferredWidth: UiMetrics.iconSize; Layout.preferredHeight: UiMetrics.iconSize; kind: "folder"; strokeColor: page.theme.accent }
                        AppTextField { id: watchFolderField; Layout.fillWidth: true; Layout.minimumWidth: 0; placeholderText: qsTr("감시할 폴더 경로"); Accessible.name: qsTr("감시할 폴더") }
                        AppButton { text: qsTr("폴더 바꾸기"); appearance: AppButton.Soft; dark: page.dark; onClicked: folderDialog.open() }
                    }
                    AppSwitch { id: watchEnabledCheck; Layout.fillWidth: true; text: qsTr("자동 감시 켜기"); dark: page.dark }
                    Rectangle { Layout.fillWidth: true; implicitHeight: UiMetrics.borderNormal; color: page.theme.border }
                    AppSwitch { id: watchRecursiveCheck; Layout.fillWidth: true; text: qsTr("하위 폴더도 포함"); dark: page.dark }
                    Rectangle { Layout.fillWidth: true; implicitHeight: UiMetrics.borderNormal; color: page.theme.border }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: UiMetrics.spaceControl
                        Label { Layout.fillWidth: true; Layout.minimumWidth: 0; text: qsTr("다시 확인하는 간격"); wrapMode: Text.Wrap; color: page.theme.text; font.pixelSize: UiMetrics.textBody }
                        Repeater {
                            model: [1, 5, 15]
                            delegate: AppButton {
                                required property int modelData
                                text: qsTr("%1분").arg(modelData)
                                implicitWidth: UiMetrics.controlHeight + UiMetrics.spaceSection
                                selected: !page.customInterval && watchIntervalSpin.value === modelData
                                appearance: AppButton.Menu
                                dark: page.dark
                                onClicked: { page.customInterval = false; watchIntervalSpin.value = modelData }
                            }
                        }
                        AppButton { objectName: "watchIntervalCustomButton"; text: qsTr("직접"); implicitWidth: UiMetrics.controlHeight + UiMetrics.spaceSection; selected: page.customInterval; appearance: AppButton.Menu; dark: page.dark; onClicked: page.customInterval = true }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: page.customInterval
                        Item { Layout.fillWidth: true }
                        AppSpinBox { id: watchIntervalSpin; objectName: "watchIntervalSpin"; editable: true; Layout.preferredWidth: UiMetrics.watchConditionInputWidth; from: 1; to: 1440; value: 30 }
                        Label { text: qsTr("분"); color: page.theme.muted; font.pixelSize: UiMetrics.textBody }
                    }
                }
                SettingsSection {
                    Layout.fillWidth: true
                    title: qsTr("언제")
                    dark: page.dark
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: qsTr("아래 조건 중"); color: page.theme.muted; font.pixelSize: UiMetrics.textBody }
                        AppComboBox { id: watchCombination; model: [qsTr("하나라도"), qsTr("모두")]; Layout.fillWidth: true }
                        Label { text: qsTr("맞으면 정리를 제안해요"); color: page.theme.muted; font.pixelSize: UiMetrics.textBody }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        AppSwitch { id: watchCountCheck; Layout.fillWidth: true; Layout.minimumWidth: 0; indicatorOnLeft: true; text: qsTr("분류 가능한 파일이"); dark: page.dark }
                        AppSpinBox { id: watchCountSpin; objectName: "watchCountSpin"; editable: true; Layout.preferredWidth: UiMetrics.watchConditionInputWidth; from: 1; to: 1000000; value: 20; enabled: watchCountCheck.checked }
                        Label { text: qsTr("개 이상"); font.pixelSize: UiMetrics.textBody; color: page.theme.muted }
                    }
                    Rectangle { Layout.fillWidth: true; implicitHeight: UiMetrics.borderNormal; color: page.theme.border }
                    RowLayout {
                        Layout.fillWidth: true
                        AppSwitch { id: watchSizeCheck; Layout.fillWidth: true; Layout.minimumWidth: 0; indicatorOnLeft: true; text: qsTr("파일 전체 크기가"); dark: page.dark }
                        AppSpinBox { id: watchSizeSpin; objectName: "watchSizeSpin"; editable: true; Layout.preferredWidth: UiMetrics.watchConditionInputWidth; from: 1; to: 1000000; value: 100; enabled: watchSizeCheck.checked }
                        AppComboBox {
                            id: watchSizeUnit
                            objectName: "watchSizeUnitCombo"
                            Layout.preferredWidth: UiMetrics.watchUnitWidth
                            Layout.minimumWidth: 0
                            Layout.maximumWidth: UiMetrics.watchUnitWidth
                            enabled: watchSizeCheck.checked
                            model: [{text: "KB", value: "KB"}, {text: "MB", value: "MB"}, {text: "GB", value: "GB"}, {text: "MiB", value: "MiB"}]
                            textRole: "text"
                            valueRole: "value"
                            currentIndex: 1
                        }
                        Label { text: qsTr("이상"); font.pixelSize: UiMetrics.textBody; color: page.theme.muted }
                    }
                    Rectangle { Layout.fillWidth: true; implicitHeight: UiMetrics.borderNormal; color: page.theme.border }
                    RowLayout {
                        Layout.fillWidth: true
                        AppSwitch { id: watchAgeCheck; Layout.fillWidth: true; Layout.minimumWidth: 0; indicatorOnLeft: true; text: qsTr("가장 오래된 파일이"); dark: page.dark }
                        AppSpinBox { id: watchAgeSpin; objectName: "watchAgeSpin"; editable: true; Layout.preferredWidth: UiMetrics.watchConditionInputWidth; from: 1; to: 1000000; value: 60; enabled: watchAgeCheck.checked }
                        Label { text: qsTr("분 지남"); font.pixelSize: UiMetrics.textBody; color: page.theme.muted }
                    }
                }
                SettingsSection {
                    Layout.fillWidth: true
                    title: qsTr("세지 않을 파일")
                    dark: page.dark
                    WatchPatternEditor { id: watchExcludeField; Layout.fillWidth: true; dark: page.dark }
                    Label { Layout.fillWidth: true; text: qsTr("다운로드 중인 임시 파일 등을 제외할 수 있어요."); wrapMode: Text.Wrap; font.pixelSize: UiMetrics.textBody; color: page.theme.muted }
                }
                Label { Layout.fillWidth: true; text: qsTr("파일은 자동으로 이동하지 않아요. 정리 제안 후 별도로 승인해요."); wrapMode: Text.Wrap; font.pixelSize: UiMetrics.textBody; color: page.theme.muted }
            }
        }
        Label {
            Layout.fillWidth: true
            visible: text.length > 0
            text: page.controller.statusMessage
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            color: page.theme.muted
            font.pixelSize: UiMetrics.textBody
            HoverHandler { id: messageHover }
            ToolTip.visible: messageHover.hovered
            ToolTip.text: text
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: UiMetrics.spaceGroup
            Label { Layout.fillWidth: true; Layout.minimumWidth: 0; text: page.changeCount > 0 ? qsTr("저장하지 않은 변경 %1개").arg(page.changeCount) : qsTr("저장된 설정"); wrapMode: Text.Wrap; font.pixelSize: UiMetrics.textBody; color: page.changeCount > 0 ? page.theme.warning : page.theme.muted }
            AppButton { objectName: "watchResetButton"; text: qsTr("되돌리기"); dark: page.dark; enabled: page.changeCount > 0; onClicked: page.reload() }
            AppButton { objectName: "watchSaveButton"; text: qsTr("저장"); primary: true; dark: page.dark; enabled: page.changeCount > 0; onClicked: page.save() }
        }
    }
}
