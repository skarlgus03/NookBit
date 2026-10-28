import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 감시 조건의 입력 초안과 저장 요청을 소유하는 페이지.
PageSurface {
    id: page
    required property var controller
    property bool initialized: false
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
    }
    Component.onCompleted: {
        initialized = true;
        if (visible)
            reload();
    }
    onVisibleChanged: if (initialized && visible)
                          reload()

    objectName: "watchSettingsDialog"

    background: Rectangle { color: page.theme.window }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: UiMetrics.spaceSection
        spacing: UiMetrics.spaceGroup
        Label {
            Layout.fillWidth: true
            text: qsTr("조건을 만족하면 정리를 제안해요. 파일은 자동으로 이동하지 않아요.")
            wrapMode: Text.Wrap
            font.pixelSize: UiMetrics.textBody
            color: page.theme.muted
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
                SettingsSection {
                    Layout.fillWidth: true
                    title: qsTr("감시 폴더")
                    dark: page.dark
                    AppTextField { id: watchFolderField; Layout.fillWidth: true; placeholderText: qsTr("예: C:/Users/이름/Downloads"); Accessible.name: qsTr("감시할 폴더") }
                    AppSwitch { id: watchEnabledCheck; Layout.fillWidth: true; text: qsTr("자동 감시 켜기"); dark: page.dark }
                    AppSwitch { id: watchRecursiveCheck; Layout.fillWidth: true; text: qsTr("하위 폴더도 감시"); dark: page.dark }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { Layout.fillWidth: true; text: qsTr("재검사 간격"); color: page.theme.text; font.pixelSize: UiMetrics.textBody }
                        AppSpinBox { id: watchIntervalSpin; objectName: "watchIntervalSpin"; editable: true; Layout.preferredWidth: UiMetrics.numericInputWidth; from: 1; to: 1440; value: 30 }
                        Label { text: qsTr("분"); color: page.theme.muted; font.pixelSize: UiMetrics.textBody }
                    }
                }
                SettingsSection {
                    Layout.fillWidth: true
                    title: qsTr("정리 제안 조건")
                    dark: page.dark
                    RowLayout {
                        Layout.fillWidth: true
                        Label { Layout.fillWidth: true; text: qsTr("조건 조합"); color: page.theme.text; font.pixelSize: UiMetrics.textBody }
                        AppComboBox { id: watchCombination; model: [qsTr("하나만 충족"), qsTr("모두 충족")] }
                    }
                    Rectangle { Layout.fillWidth: true; implicitHeight: UiMetrics.borderNormal; color: page.theme.border }
                    AppSwitch { id: watchCountCheck; Layout.fillWidth: true; text: qsTr("파일 개수 조건"); dark: page.dark }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { Layout.fillWidth: true; text: qsTr("분류 가능한 파일"); font.pixelSize: UiMetrics.textBody; color: page.theme.muted }
                        AppSpinBox { id: watchCountSpin; objectName: "watchCountSpin"; editable: true; Layout.preferredWidth: UiMetrics.numericInputWidth; from: 1; to: 1000000; value: 20; enabled: watchCountCheck.checked }
                        Label { text: qsTr("개 이상"); font.pixelSize: UiMetrics.textBody; color: page.theme.muted }
                    }
                    Rectangle { Layout.fillWidth: true; implicitHeight: UiMetrics.borderNormal; color: page.theme.border }
                    AppSwitch { id: watchSizeCheck; Layout.fillWidth: true; text: qsTr("파일 전체 크기 조건"); dark: page.dark }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { Layout.fillWidth: true; text: qsTr("합계"); font.pixelSize: UiMetrics.textBody; color: page.theme.muted }
                        AppSpinBox { id: watchSizeSpin; objectName: "watchSizeSpin"; editable: true; Layout.preferredWidth: UiMetrics.numericInputWidth; from: 1; to: 1000000; value: 100; enabled: watchSizeCheck.checked }
                        AppComboBox {
                            id: watchSizeUnit
                            objectName: "watchSizeUnitCombo"
                            enabled: watchSizeCheck.checked
                            model: [{text: "KB", value: "KB"}, {text: "MB", value: "MB"}, {text: "GB", value: "GB"}, {text: "MiB", value: "MiB"}]
                            textRole: "text"
                            valueRole: "value"
                            currentIndex: 1
                        }
                        Label { text: qsTr("이상"); font.pixelSize: UiMetrics.textBody; color: page.theme.muted }
                    }
                    Rectangle { Layout.fillWidth: true; implicitHeight: UiMetrics.borderNormal; color: page.theme.border }
                    AppSwitch { id: watchAgeCheck; Layout.fillWidth: true; text: qsTr("파일 경과 시간 조건"); dark: page.dark }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { Layout.fillWidth: true; text: qsTr("가장 오래된 파일"); font.pixelSize: UiMetrics.textBody; color: page.theme.muted }
                        AppSpinBox { id: watchAgeSpin; objectName: "watchAgeSpin"; editable: true; Layout.preferredWidth: UiMetrics.numericInputWidth; from: 1; to: 1000000; value: 60; enabled: watchAgeCheck.checked }
                        Label { text: qsTr("분 이상"); font.pixelSize: UiMetrics.textBody; color: page.theme.muted }
                    }
                }
                SettingsSection {
                    Layout.fillWidth: true
                    title: qsTr("제외할 파일")
                    dark: page.dark
                    Label { Layout.fillWidth: true; text: qsTr("파일명 패턴을 쉼표로 구분해 주세요."); wrapMode: Text.Wrap; color: page.theme.muted; font.pixelSize: UiMetrics.textBody }
                    AppTextField { id: watchExcludeField; Layout.fillWidth: true; placeholderText: qsTr("*.partial, *.crdownload, ~$*"); Accessible.name: qsTr("제외할 파일명 패턴") }
                }
            }
        }
        Label {
            Layout.fillWidth: true
            visible: text.length > 0
            text: page.controller.statusMessage
            wrapMode: Text.Wrap
            color: page.theme.muted
            font.pixelSize: UiMetrics.textBody
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: UiMetrics.spaceGroup
            AppButton {
                text: qsTr("저장된 조건 지금 검사")
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                dark: page.dark
                onClicked: page.controller.checkNow()
            }
            AppButton {
                text: qsTr("조건 저장")
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                primary: true
                dark: page.dark
                onClicked: page.controller.saveSettings(watchFolderField.text, watchEnabledCheck.checked,
                    watchRecursiveCheck.checked, watchIntervalSpin.value, watchCombination.currentIndex === 1,
                    watchCountCheck.checked, watchCountSpin.value, watchSizeCheck.checked, watchSizeSpin.value,
                    watchAgeCheck.checked, watchAgeSpin.value, watchExcludeField.text, watchSizeUnit.currentValue)
            }
        }
    }
}
