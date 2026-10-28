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
    property bool previewBubbleVisible: true
    property bool initialized: false
    Component.onCompleted: initialized = true
    // 컴패니언 페이지 소유 창만 닫고 재생·편집 초안은 유지하는 함수.
    onVisibleChanged: if (initialized && !visible) {
                          characterAnchorDialog.close();
                          imagePackFolderDialog.close();
                          characterGuideDownload.closeDialog();
                      }

    objectName: "characterSettingsDialog"
    background: Rectangle { color: page.theme.window }
    RowLayout {
        anchors.fill: parent
        anchors.margins: UiMetrics.spaceSection
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
            showBubble: page.previewBubbleVisible
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
        CompanionSettingsPanel {
            Layout.preferredWidth: UiMetrics.companionSettingsWidth
            Layout.fillHeight: true
            settings: page.settings
            placement: page.placement
            motion: page.motion
            canWalk: page.character.canWalk
            dirty: companionPreview.dirty
            portrait: page.character.animation
            showBubble: page.previewBubbleVisible
            dark: page.dark
            guideStatus: characterGuideDownload.status
            onBubbleToggled: checked => page.previewBubbleVisible = checked
            onImportRequested: imagePackFolderDialog.open()
            onGuideRequested: characterGuideDownload.saveDocument()
            onAnchorsRequested: characterAnchorDialog.open()
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
