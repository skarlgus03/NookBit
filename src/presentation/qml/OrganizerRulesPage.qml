import QtQuick

// 규칙 편집기와 예전 개발용 폼의 초안·보조 창 연결을 기능 안에 모은 페이지.
PageSurface {
    id: page
    required property var controller
    required property var destinations
    required property var progress
    required property var guideService
    required property Item overlayHost
    property bool initialized: false
    Component.onCompleted: initialized = true
    onVisibleChanged: if (initialized && !visible) legacyForm.close()

    // 개발 도구나 편집기의 요청으로 같은 기능이 소유한 예전 폼을 여는 함수.
    function openLegacyEditor() { legacyForm.open() }

    RuleEditorPage {
        id: editor
        objectName: "ruleEditorPage"
        anchors.fill: parent
        controller: page.controller
        destinations: page.destinations
        guideService: page.guideService
        darkMode: page.dark
        oldEditorFormDirty: legacyForm.dirty
        onOpenCurrentEditor: page.openLegacyEditor()
        onEditorDraftReplaced: legacyForm.resetDraftForm()
    }
    LegacyRuleEditorDialog {
        id: legacyForm
        parent: page.overlayHost
        controller: page.controller
        busy: page.progress.busy
        dark: page.dark
    }
}
