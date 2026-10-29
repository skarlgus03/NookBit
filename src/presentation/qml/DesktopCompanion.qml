import QtQuick

// 바탕화면 캐릭터의 표현·입력 연결만 소유하고 알림의 실제 동작은 바깥에 요청한다.
Item {
    id: desktop
    required property var character
    required property var settings
    required property var placement
    required property var interactions
    required property var motion
    property bool dark: false
    property bool attentionActionAvailable: false
    readonly property var summary: character.animation
    signal attentionRequested()
    signal pageRequested(string pageId)

    // 종료·숨김 전에 마지막 창 이동량을 표시 설정에 확정하는 함수.
    function commitState() { pet.commitPosition() }
    // 실제 알림이나 작업 상태를 바꾸지 않고 말풍선 배치를 시험하는 함수.
    function preview() { placement.setShown(true); pet.testBubble() }

    CharacterWindow {
        id: pet
        placement: desktop.placement
        descriptor: desktop.character.animation
        bubbleSettings: desktop.character.bubble
        dark: desktop.dark
        message: desktop.character.message
        interactions: desktop.interactions
        onGestureHeldChanged: desktop.motion.setPointerHeld(gestureHeld)
        onVisualBoundsUpdated: function(left, right) { desktop.motion.setVisualBounds(left, right) }
        userScale: desktop.settings.userScale
        userPivotOffset: desktop.settings.pivotOffset
        userBubbleOffset: desktop.settings.bubbleOffset
        actionAvailable: desktop.character.reactionId === "" && desktop.attentionActionAvailable
        onActionRequested: if (actionAvailable) desktop.attentionRequested()
        onSettingsRequested: desktop.pageRequested("companion.settings")
    }
}
