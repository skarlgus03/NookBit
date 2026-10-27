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

    ColumnLayout {
        anchors.fill: parent
        spacing: UiMetrics.spaceCompact

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
                spacing: UiMetrics.spaceGroup
                Label {
                    text: qsTr("감시할 폴더")
                }
                AppTextField {
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
                    Label {
                        text: qsTr("정기 재검사 간격")
                    }
                    AppSpinBox {
                        id: watchIntervalSpin
                        objectName: "watchIntervalSpin"
                        editable: true
                        Layout.preferredWidth: UiMetrics.numericInputWidth
                        from: 1
                        to: 1440
                        value: 30
                    }
                    Label {
                        text: qsTr("분")
                    }
                }
                RowLayout {
                    Label {
                        text: qsTr("조건 조합")
                    }
                    AppComboBox {
                        id: watchCombination
                        model: [qsTr("하나만 충족"), qsTr("모두 충족")]
                    }
                }
                RowLayout {
                    CheckBox {
                        id: watchCountCheck
                        text: qsTr("분류 가능한 파일이")
                    }
                    AppSpinBox {
                        id: watchCountSpin
                        objectName: "watchCountSpin"
                        editable: true
                        Layout.preferredWidth: UiMetrics.numericInputWidth
                        from: 1
                        to: 1000000
                        value: 20
                        enabled: watchCountCheck.checked
                    }
                    Label {
                        text: qsTr("개 이상")
                    }
                }
                RowLayout {
                    CheckBox {
                        id: watchSizeCheck
                        text: qsTr("파일 전체 크기가")
                    }
                    AppSpinBox {
                        id: watchSizeSpin
                        objectName: "watchSizeSpin"
                        editable: true
                        Layout.preferredWidth: UiMetrics.numericInputWidth
                        from: 1
                        to: 1000000
                        value: 100
                        enabled: watchSizeCheck.checked
                    }
                    AppComboBox {
                        id: watchSizeUnit
                        objectName: "watchSizeUnitCombo"
                        enabled: watchSizeCheck.checked
                        model: [
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
                            },
                            {
                                text: "MiB",
                                value: "MiB"
                            }
                        ]
                        textRole: "text"
                        valueRole: "value"
                        currentIndex: 1
                    }
                    Label {
                        text: qsTr("이상")
                    }
                }
                RowLayout {
                    CheckBox {
                        id: watchAgeCheck
                        text: qsTr("가장 오래된 파일이")
                    }
                    AppSpinBox {
                        id: watchAgeSpin
                        objectName: "watchAgeSpin"
                        editable: true
                        Layout.preferredWidth: UiMetrics.numericInputWidth
                        from: 1
                        to: 1000000
                        value: 60
                        enabled: watchAgeCheck.checked
                    }
                    Label {
                        text: qsTr("분 이상")
                    }
                }
                Label {
                    text: qsTr("제외할 파일명 패턴 · 쉼표로 구분")
                }
                AppTextField {
                    id: watchExcludeField
                    Layout.fillWidth: true
                    placeholderText: qsTr("*.partial, *.crdownload, ~$*")
                }
            }
        }
        Label {
            Layout.fillWidth: true
            text: page.controller.statusMessage
            wrapMode: Text.WordWrap
            color: page.theme.success
        }
        RowLayout {
            AppButton {
                text: qsTr("조건 저장")
                onClicked: page.controller.saveSettings(watchFolderField.text, watchEnabledCheck.checked,
                                                        watchRecursiveCheck.checked, watchIntervalSpin.value,
                                                        watchCombination.currentIndex === 1, watchCountCheck.checked,
                                                        watchCountSpin.value, watchSizeCheck.checked,
                                                        watchSizeSpin.value, watchAgeCheck.checked, watchAgeSpin.value,
                                                        watchExcludeField.text, watchSizeUnit.currentValue)
            }
            AppButton {
                text: qsTr("저장된 조건 지금 검사")
                onClicked: page.controller.checkNow()
            }
        }
    }
}
