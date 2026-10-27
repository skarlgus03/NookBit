import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs as NativeDialogs

// 컴패니언 재생·팩 설정과 페이지 소유 보조 창을 모은 관리 페이지.
PageSurface {
    id: page
    required property var character
    required property var settings
    required property var placement
    required property var motion
    required property var edits
    required property var guideService
    property bool initialized: false
    Component.onCompleted: initialized = true
    // 컴패니언 페이지 소유 창만 닫고 재생·편집 초안은 유지하는 함수.
    onVisibleChanged: if (initialized && !visible) {
                          characterAnchorDialog.close();
                          imagePackFolderDialog.close();
                          characterGuideDownload.closeDialog();
                      }

    objectName: "characterSettingsDialog"
    RowLayout {
        anchors.fill: parent
        spacing: UiMetrics.spaceSection
        CompanionPreview {
            id: companionPreview
            enabled: !page.settings.busy
            Layout.fillWidth: true
            Layout.fillHeight: true
            animations: page.character.previewAnimations
            bubble: page.character.bubble
            theme: page.theme
            dark: page.dark
            active: page.visible
            showBubble: previewBubbleToggle.checked
            savedEdits: page.edits.applied
            editStatus: page.edits.status
            allowEditing: page.edits.available
            userPivotOffset: page.settings.pivotOffset
            userBubbleOffset: page.settings.bubbleOffset
            onApplyRequested: function (edits) {
                if (page.edits.applyChanges(edits))
                    companionPreview.showSavedNotice();
            }
        }
        ScrollView {
            id: companionControls
            Layout.preferredWidth: UiMetrics.companionSettingsWidth
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                width: companionControls.availableWidth
                spacing: UiMetrics.spaceGroup
                Label {
                    text: qsTr("캐릭터 팩")
                    font.bold: true
                }
                CheckBox {
                    id: previewBubbleToggle
                    text: qsTr("미리보기 말풍선 표시")
                    checked: true
                }
                AppButton {
                    objectName: "characterUserGuideButton"
                    text: qsTr("제작 가이드 저장")
                    onClicked: characterGuideDownload.saveDocument()
                }
                Label {
                    Layout.fillWidth: true
                    visible: characterGuideDownload.status !== ""
                    text: characterGuideDownload.status
                    wrapMode: Text.Wrap
                }
                CheckBox {
                    text: qsTr("컴패니언을 바탕화면에 표시")
                    checked: page.placement.shown
                    onToggled: page.placement.setShown(checked)
                }
                AppButton {
                    text: qsTr("바탕화면 위치 되돌리기")
                    onClicked: page.placement.resetPosition()
                }
                CheckBox {
                    text: qsTr("쉬는 동안 가끔 걷기")
                    checked: page.motion.enabled
                    enabled: page.character.canWalk
                    onToggled: page.motion.setEnabled(checked)
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("클릭: 반응 · 드래그: 이동\nShift + 드래그: 쓰다듬기 · 우클릭: 설정")
                    wrapMode: Text.Wrap
                    font.pixelSize: UiMetrics.textBody
                }
                AppComboBox {
                    id: characterPackChoice
                    Layout.fillWidth: true
                    model: page.settings.packs
                    textRole: "label"
                    valueRole: "key"
                    currentIndex: {
                        const items = page.settings.packs;
                        for (let i = 0; i < items.length; ++i)
                            if (items[i].key === page.settings.selectedKey)
                                return i;
                        return -1;
                    }
                    enabled: !page.settings.busy && !companionPreview.dirty
                    onActivated: page.settings.selectPack(currentValue)
                }
                AppButton {
                    text: qsTr("팩 폴더 가져오기")
                    enabled: !page.settings.busy && !companionPreview.dirty
                    onClicked: imagePackFolderDialog.open()
                }
                Label {
                    text: qsTr("표시 크기: %1%").arg(Math.round(page.settings.userScale * 100))
                }
                Slider {
                    Layout.fillWidth: true
                    from: 0.1
                    to: 2.0
                    stepSize: 0.05
                    value: page.settings.userScale
                    onMoved: page.settings.setUserScale(value)
                }
                AppButton {
                    text: qsTr("전체 기준점 보정")
                    onClicked: characterAnchorDialog.open()
                }
                AppButton {
                    text: qsTr("크기 기본값 (50%)")
                    onClicked: page.settings.setUserScale(0.5)
                }
                Label {
                    Layout.fillWidth: true
                    text: page.settings.status
                    wrapMode: Text.Wrap
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("선택과 크기는 자동 저장돼요. 팩 원본은 변경하지 않아요.")
                    wrapMode: Text.Wrap
                    font.pixelSize: UiMetrics.textBody
                }
            }
        }
    }

    NativeDialogs.FolderDialog {
        id: imagePackFolderDialog
        title: qsTr("imagepack.json이 들어 있는 캐릭터 팩 폴더 선택")
        onAccepted: page.settings.importFolder(selectedFolder)
    }
    GuideDownload {
        id: characterGuideDownload
        objectName: "characterGuideDownload"
        parent: page
        guideKey: "character"
        service: page.guideService
    }
    CharacterAnchorEditor {
        id: characterAnchorDialog
        parent: page
        settings: page.settings
        dark: page.dark
        descriptor: page.character.animation
        bubble: page.character.bubble
    }
}
