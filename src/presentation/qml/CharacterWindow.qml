import QtQuick
import QtQuick.Window

// 파일 작업과 별개로 캐릭터 표현 값만 재생하는 투명 독립 창.
Window {
    id: pet
    objectName: "desktopCharacterWindow"
    required property var placement
    required property var descriptor
    required property string message
    required property var interactions
    property real userScale: 0.5
    property point userPivotOffset: Qt.point(0, 0)
    property point userBubbleOffset: Qt.point(0, 0)
    property bool dark: true
    property var bubbleSettings: ({})
    // 캐릭터 부착점은 이미지 피벗 기준 논리 좌표이며 개인 배율을 적용한다.
    readonly property var currentAttachment: sprite.renderedStep.attachment || bubbleSettings.attachment || ({x: 0, y: -120})
    readonly property point bubbleOffset: Qt.point(currentAttachment.x, currentAttachment.y)
    readonly property bool mirrored: sprite.renderedMirrorX
    readonly property bool gestureHeld: gestureInput.tracking
    signal visualBoundsUpdated(real left, real right)
    property bool actionAvailable: false
    property bool testingBubble: false
    signal settingsRequested()
    signal actionRequested()
    transientParent: null
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    title: qsTr("NookBit 캐릭터")
    visible: placement.shown
    readonly property var bounds: frameBounds()
    onBoundsChanged: visualBoundsUpdated(bounds.left, bounds.right)
    Component.onCompleted: visualBoundsUpdated(bounds.left, bounds.right)
    width: Math.max(180, Math.ceil(bounds.right - bounds.left) + 32)
    height: Math.max(100, Math.ceil(bounds.bottom - bounds.top) + 76)
    x: Math.round(placement.anchorX - sprite.anchor.x)
    y: Math.round(placement.anchorY - sprite.anchor.y)
    // 닫기 명령은 앱 종료가 아니라 캐릭터 숨기기로 처리하는 함수.
    onClosing: function(event) { event.accepted = false; placement.setShown(false) }
    onXChanged: positionCommit.restart()
    onYChanged: positionCommit.restart()
    // 숨겨질 때 진행 중 입력과 반응을 함께 정리하는 함수.
    onVisibleChanged: {
        if (!visible && interactions) {
            interactions.cancel()
            gestureInput.tracking = false
            gestureInput.gestureKind = ""
        }
    }

    // 정수 창 좌표의 반올림 오차는 버리고 실제 창 이동량만 저장하는 함수.
    function commitPosition() {
        const dx = x - Math.round(placement.anchorX - sprite.anchor.x)
        const dy = y - Math.round(placement.anchorY - sprite.anchor.y)
        if (dx !== 0 || dy !== 0)
            placement.moveAnchor(placement.anchorX + dx, placement.anchorY + dy)
    }
    // 정리 제안이나 캐릭터 상태를 바꾸지 않고 말풍선 배치만 시험하는 함수.
    function testBubble() {
        testingBubble = true
        bubbleTestDeadline.restart()
    }
    CharacterBubble {
        dark: pet.dark
        configuration: pet.bubbleSettings
        transientParent: pet
        visible: pet.visible && message.length > 0
        screenArea: pet.placement.screenArea
        anchorPoint: Qt.point(pet.x + sprite.anchor.x + (pet.bubbleOffset.x + pet.userBubbleOffset.x - pet.userPivotOffset.x) * pet.userScale * (pet.mirrored ? -1 : 1),
                              pet.y + sprite.anchor.y + (pet.bubbleOffset.y + pet.userBubbleOffset.y - pet.userPivotOffset.y) * pet.userScale)
        message: pet.testingBubble ? qsTr("말풍선 위치 테스트예요. 캐릭터를 화면 가장자리로 옮겨보세요!") : pet.message
        actionAvailable: pet.testingBubble || pet.actionAvailable
        actionText: pet.testingBubble ? qsTr("닫기") : qsTr("확인")
        onActionRequested: {
            if (pet.testingBubble) pet.testingBubble = false
            else pet.actionRequested()
        }
        // 말풍선 닫기로 앱이 종료되지 않도록 캐릭터 표시만 해제하는 함수.
        onClosing: function(event) { event.accepted = false; pet.placement.setShown(false) }
    }
    Timer { id: bubbleTestDeadline; interval: 10000; onTriggered: pet.testingBubble = false }

    // 애니메이션 전체 포즈의 피벗 기준 영역을 구해 프레임마다 창이 흔들리지 않게 하는 함수.
    function frameBounds() {
        let left = -40, top = -48, right = 40, bottom = 16
        const steps = sprite.retainingFrame ? sprite.steps.concat([sprite.renderedStep]) : sprite.steps
        for (let i = 0; i < steps.length; ++i) {
            const step = steps[i]
            if (!step.region || !step.pivot || !(step.pixelsPerUnit > 0)) continue
            const l = -(step.pivot.x + userPivotOffset.x) * userScale, t = -(step.pivot.y + userPivotOffset.y) * userScale
            const r = l + step.region.width * userScale / step.pixelsPerUnit
            const b = t + step.region.height * userScale / step.pixelsPerUnit
            if (!isFinite(l) || !isFinite(t) || !isFinite(r) || !isFinite(b)) continue
            left = Math.min(left, mirrored ? -r : l); top = Math.min(top, t)
            right = Math.max(right, mirrored ? -l : r); bottom = Math.max(bottom, b)
        }
        // 비정상적으로 큰 팩의 좌표로 거대한 네이티브 창을 만들지 않는다.
        return {left: Math.max(-1024, left), top: Math.max(-1024, top),
            right: Math.min(1024, right), bottom: Math.min(1024, bottom)}
    }

    CharacterSprite {
        id: sprite
        objectName: "desktopCharacterSprite"
        anchors.fill: parent
        descriptor: pet.descriptor
        userScale: pet.userScale
        userPivotOffset: pet.userPivotOffset
        anchor: Qt.point((pet.width - (pet.bounds.right - pet.bounds.left)) / 2 - pet.bounds.left,
                         32 - pet.bounds.top)
        playing: pet.visible
    }
    Text {
        anchors.centerIn: parent
        // 정상 프레임 로딩은 이전 그림으로 기다리고 실제 오류일 때만 안내하는 표시.
        visible: sprite.error.length > 0
        text: sprite.error
        color: "#ffffff"
        style: Text.Outline
        styleColor: "#263246"
    }
    MouseArea {
        id: gestureInput
        objectName: "companionGestureInput"
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        preventStealing: true
        property bool tracking: false
        property string gestureKind: ""
        property point pressScreen: Qt.point(0, 0)
        property point startAnchor: Qt.point(0, 0)
        // 개인 배율과 추가 피벗 보정을 제거한 프레임 피벗 기준 논리 좌표를 구하는 함수.
        function contactPoint(mouse) {
            return Qt.point((mouse.x - sprite.anchor.x) / pet.userScale * (pet.mirrored ? -1 : 1) + pet.userPivotOffset.x,
                            (mouse.y - sprite.anchor.y) / pet.userScale + pet.userPivotOffset.y)
        }
        // 클릭은 놓을 때 확정하고 Shift 입력은 쓰다듬기로 구별하는 함수.
        onPressed: function(mouse) {
            if (mouse.button === Qt.RightButton) {
                if (tracking) pet.interactions.cancel()
                tracking = false
                gestureKind = ""
                pet.settingsRequested()
                return
            }
            pressScreen = mapToGlobal(mouse.x, mouse.y)
            tracking = true
            gestureKind = ""
            startAnchor = Qt.point(pet.placement.anchorX, pet.placement.anchorY)
            if (mouse.modifiers & Qt.ShiftModifier) {
                const point = contactPoint(mapFromGlobal(pressScreen.x, pressScreen.y))
                tracking = pet.interactions.begin("companion.pet", point.x, point.y)
                if (tracking) gestureKind = "companion.pet"
            }
        }
        // 이동 임계값을 넘은 조작만 끌기로 확정하고 수락된 경우에 위치를 바꾸는 함수.
        onPositionChanged: function(mouse) {
            if (!tracking || !(pressedButtons & Qt.LeftButton)) return
            const screen = mapToGlobal(mouse.x, mouse.y)
            const dx = screen.x - pressScreen.x, dy = screen.y - pressScreen.y
            const point = contactPoint(mouse)
            if (gestureKind === "") {
                if (Math.hypot(dx, dy) < Qt.styleHints.startDragDistance) return
                if (!pet.interactions.begin("companion.drag", point.x, point.y)) { tracking = false; return }
                gestureKind = "companion.drag"
            }
            if (!pet.interactions.update(point.x, point.y)) { tracking = false; gestureKind = ""; return }
            if (gestureKind === "companion.drag")
                pet.placement.moveAnchor(Math.round(startAnchor.x + dx), Math.round(startAnchor.y + dy))
        }
        // 연속 입력의 종료와 단발 클릭을 서로 배타적으로 처리하는 함수.
        onReleased: function(mouse) {
            if (mouse.button !== Qt.LeftButton || !tracking) return
            if (gestureKind !== "") pet.interactions.end()
            else {
                const point = contactPoint(mouse)
                pet.interactions.invoke("companion.click", point.x, point.y)
            }
            tracking = false
            gestureKind = ""
        }
        // 포인터 입력을 잃으면 나중에 클릭이나 종료를 추가 실행하지 않는 함수.
        onCanceled: {
            if (tracking) pet.interactions.cancel()
            tracking = false
            gestureKind = ""
        }
    }
    Timer {
        id: positionCommit
        interval: 180
        // 운영체제의 창 이동 결과를 화면 피벗 좌표로 저장하는 함수.
        onTriggered: pet.commitPosition()
    }
}
