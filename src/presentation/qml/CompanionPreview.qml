import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 한 작업 공간에서 애니메이션 재생과 프레임 기준점 구성을 확인하는 미리보기.
Rectangle {
    id: pane
    objectName: "companionPreviewWorkspace"
    required property var animations
    required property var bubble
    required property var theme
    property bool dark: false
    property bool active: false
    property bool showBubble: true
    property bool running: true
    property bool editing: false
    property bool allowEditing: true
    property string previewMessage: qsTr("잠깐 쉬고 있어요. 함께 정리해 볼까요?")
    property var drafts: ({})
    readonly property var descriptor: animations && animations.length && choice.currentIndex >= 0 && choice.currentIndex < animations.length
        ? animations[choice.currentIndex].descriptor : ({})
    readonly property var step: sprite.currentStep
    readonly property string frameKey: step.frameId || ""
    readonly property var draft: drafts[frameKey] || ({})
    readonly property var pivot: draft.pivot || step.pivot || ({x: 0, y: 0})
    readonly property var attachment: draft.attachment || bubble.attachment || ({x: 0, y: -120})
    readonly property bool playbackActive: active && running && !clock.finished && sprite.steps.length > 0
    color: theme.alternate
    radius: UiMetrics.radiusPanel
    border.color: theme.border
    clip: true

    // 선택한 프레임에서 재생을 정지하는 함수.
    function selectFrame(index) {
        if (!sprite.steps.length) return
        running = false
        clock.seekFrame(Math.max(0, Math.min(index, sprite.steps.length - 1)))
    }
    // 재생 막대를 옮긴 정확한 시각에서 멈추는 함수.
    function seekTime(timeMs) { running = false; clock.seek(timeMs) }
    // 완료한 재생은 처음으로 돌리고 재생과 일시정지를 전환하는 함수.
    function togglePlayback() {
        const resume = !playbackActive
        if (clock.finished) clock.seek(0)
        running = resume
    }
    // 선택한 프레임의 기준점을 임시 조정값으로 보관하는 함수.
    function movePoint(kind, x, y) {
        if (!sprite.metadataValid) return
        const values = Object.assign({}, drafts)
        const value = Object.assign({}, values[frameKey] || {})
        if (kind === 0) value.pivot = {x: x, y: y}
        else value.attachment = {x: x - pivot.x, y: y - pivot.y}
        values[frameKey] = value
        drafts = values
    }
    onAnimationsChanged: { drafts = ({}); choice.currentIndex = animations && animations.length ? 0 : -1; running = true }
    onEditingChanged: if (editing) running = false
    onActiveChanged: if (!active) running = false

    CompanionPlaybackClock {
        id: clock
        objectName: "companionPlaybackClock"
        steps: sprite.steps
        playing: pane.active && pane.running
        ready: sprite.ready
        loop: !!pane.descriptor.loop
        onFrameChanging: sprite.retainedStep = sprite.currentStep
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: UiMetrics.spaceGroup
        spacing: UiMetrics.spaceGroup
        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("애니메이션"); color: pane.theme.text }
            AppComboBox {
                id: choice
                objectName: "companionAnimationChoice"
                Layout.fillWidth: true
                model: pane.animations
                textRole: "label"
            }
            AppButton {
                visible: pane.allowEditing
                text: qsTr("편집")
                checkable: true
                checked: pane.editing
                onClicked: pane.editing = checked
            }
            AppButton {
                visible: pane.editing
                text: qsTr("조정 초기화")
                onClicked: pane.drafts = ({})
            }
        }
        Rectangle {
            id: stage
            objectName: "companionPreviewStage"
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: pane.theme.surface
            border.color: pane.theme.border
            radius: UiMetrics.radiusPanel
            clip: true
            readonly property real frameWidth: sprite.steps.reduce((size, frame) => Math.max(size, frame.region.width / frame.pixelsPerUnit), 1)
            readonly property real frameHeight: sprite.steps.reduce((size, frame) => Math.max(size, frame.region.height / frame.pixelsPerUnit), 1)
            readonly property real zoom: Math.max(0.01, Math.min(width * 0.7 / frameWidth, height * 0.7 / frameHeight))
            // 편집 중에는 이미지 원점을 고정하고 재생 중에는 공통 접점에 맞추는 좌표.
            readonly property point origin: pane.editing && !pane.running && pane.step.region
                ? Qt.point((width - pane.step.region.width / pane.step.pixelsPerUnit * zoom) / 2,
                    (height - pane.step.region.height / pane.step.pixelsPerUnit * zoom) / 2)
                : Qt.point(width / 2 - pane.pivot.x * zoom, height * 0.8 - pane.pivot.y * zoom)
            CharacterSprite {
                id: sprite
                objectName: "companionPagePreview"
                anchors.fill: parent
                descriptor: pane.descriptor
                playing: false
                externallyDriven: true
                sequenceIndex: clock.currentIndex
                userScale: stage.zoom
                anchor: pane.step.pivot ? Qt.point(stage.origin.x + pane.step.pivot.x * userScale, stage.origin.y + pane.step.pivot.y * userScale) : Qt.point(0, 0)
            }
            CharacterBubbleView {
                objectName: "companionBubblePreview"
                visible: pane.showBubble && pane.active
                configuration: pane.bubble
                dark: pane.dark
                message: pane.previewMessage
                screenArea: Qt.rect(0, 0, parent.width, parent.height)
                anchorPoint: Qt.point(stage.origin.x + (pane.pivot.x + pane.attachment.x) * stage.zoom,
                    stage.origin.y + (pane.pivot.y + pane.attachment.y) * stage.zoom)
                x: placementX; y: placementY
            }
            Repeater {
                model: 2
                delegate: Item {
                    id: handle
                    required property int index
                    visible: pane.editing && !pane.running && sprite.metadataValid
                    readonly property color guide: index === 0 ? UiTheme.characterPivotGuide : UiTheme.characterBubbleGuide
                    x: stage.origin.x + (pane.pivot.x + (index === 1 ? pane.attachment.x : 0)) * stage.zoom
                    y: stage.origin.y + (pane.pivot.y + (index === 1 ? pane.attachment.y : 0)) * stage.zoom
                    Rectangle { x: -handle.x; width: stage.width; height: UiMetrics.borderNormal; color: handle.guide; opacity: 0.6 }
                    Rectangle {
                        width: UiMetrics.characterAnchorHandle; height: width
                        x: -width / 2; y: -height / 2; radius: width / 2
                        color: handle.guide; border.color: pane.theme.surface
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.SizeAllCursor
                            // 이미지 논리 좌표로 변환하여 조정점을 이동하는 함수.
                            onPositionChanged: function(mouse) {
                                if (!pressed) return
                                const pos = mapToItem(stage, mouse.x, mouse.y)
                                pane.movePoint(handle.index, (pos.x - stage.origin.x) / stage.zoom, (pos.y - stage.origin.y) / stage.zoom)
                            }
                        }
                    }
                }
            }
        }
        Label {
            Layout.fillWidth: true
            visible: pane.editing
            text: qsTr("파랑: 캐릭터 기준점 · 주황: 말풍선 부착점\n화면 구성 확인용이에요. 조정값은 아직 저장되지 않아요.")
            color: pane.theme.muted
            wrapMode: Text.Wrap
            font.pixelSize: UiMetrics.textBody
        }
        CompanionPlayback {
            objectName: "companionPlaybackSection"
            Layout.fillWidth: true
            steps: sprite.steps
            theme: pane.theme
            filtering: pane.descriptor.filtering || "nearest"
            playing: pane.playbackActive
            finished: clock.finished
            positionMs: clock.positionMs
            currentIndex: sprite.sequenceIndex
            onFrameSelected: function(index) { pane.selectFrame(index) }
            onToggleRequested: pane.togglePlayback()
            onTimeSelected: function(timeMs) { pane.seekTime(timeMs) }
        }
    }
}
