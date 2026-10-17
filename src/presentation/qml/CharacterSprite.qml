import QtQuick

// 공통 프레임의 원본 영역을 잘라 pivot·개인 배율로 배치하는 재생기.
Item {
    id: sprite
    property var descriptor: ({})
    property bool playing: true
    property int sequenceIndex: 0
    property bool finished: false
    property var retainedStep: ({})
    property real userScale: 0.5
    property point userPivotOffset: Qt.point(0, 0)
    property point anchor: Qt.point(width / 2, height * 0.75)
    readonly property var steps: resolveSteps(descriptor)
    readonly property var currentStep: steps.length > 0 ? steps[Math.min(sequenceIndex, steps.length - 1)] : ({})
    readonly property bool metadataValid: steps.length > 0 && currentStep.source !== undefined
        && currentStep.imageWidth > 0 && currentStep.imageHeight > 0
        && currentStep.region && currentStep.region.width > 0 && currentStep.region.height > 0
        && currentStep.region.x >= 0 && currentStep.region.y >= 0
        && currentStep.region.x + currentStep.region.width <= currentStep.imageWidth
        && currentStep.region.y + currentStep.region.height <= currentStep.imageHeight
        && currentStep.pivot && isFinite(currentStep.pivot.x) && isFinite(currentStep.pivot.y)
        && currentStep.pixelsPerUnit > 0 && isFinite(currentStep.pixelsPerUnit) && currentStep.durationMs >= 16
    readonly property int currentFrame: currentStep.frameNumber !== undefined ? currentStep.frameNumber : sequenceIndex
    readonly property string currentFrameId: currentStep.frameId || ""
    readonly property bool retainingFrame: atlas.status === Image.Loading && retainedStep.source !== undefined
    readonly property var renderedStep: retainingFrame ? retainedStep : currentStep
    readonly property real pixelScale: metadataValid ? userScale / renderedStep.pixelsPerUnit : 0
    readonly property bool geometryValid: isFinite(userScale) && userScale > 0
        && isFinite(userPivotOffset.x) && isFinite(userPivotOffset.y)
        && isFinite(anchor.x) && isFinite(anchor.y)
        && isFinite(crop.x) && isFinite(crop.y) && isFinite(crop.width) && isFinite(crop.height)
    readonly property bool ready: metadataValid && atlas.status === Image.Ready
        && atlas.sourceSize.width === currentStep.imageWidth && atlas.sourceSize.height === currentStep.imageHeight
        && geometryValid
    readonly property string error: !metadataValid ? ""
        : atlas.status === Image.Error ? qsTr("스프라이트 이미지를 읽지 못했어요.")
        : atlas.status === Image.Ready && !ready ? qsTr("스프라이트 이미지 크기 또는 배치 설정이 맞지 않아요.") : ""
    implicitWidth: 64
    implicitHeight: 64
    // 큰 포즈를 같은 캔버스 폭에 강제로 맞추지 않고 프레임 영역만 잘라낸다.
    clip: false

    // 기존 격자 프로필도 ImagePack과 같은 프레임·시간 표현으로 변환하는 함수.
    function resolveSteps(value) {
        if (!value) return []
        if (value.steps) return value.steps
        if (!value.frames || value.columns <= 0 || value.rows <= 0
            || value.frameWidth <= 0 || value.frameHeight <= 0 || value.frameDurationMs < 16) return []
        return value.frames.map(function(frame) {
            return {frameId: "legacy-" + frame, frameNumber: frame, source: value.source,
                imageWidth: value.frameWidth * value.columns, imageHeight: value.frameHeight * value.rows,
                region: {x: (frame % value.columns) * value.frameWidth,
                    y: Math.floor(frame / value.columns) * value.frameHeight,
                    width: value.frameWidth, height: value.frameHeight},
                pivot: {x: value.frameWidth / 2, y: value.frameHeight * 0.75},
                pixelsPerUnit: 1, durationMs: value.frameDurationMs}
        })
    }

    // 새 재생 설정을 받으면 첫 프레임과 미완료 상태로 돌아가는 함수.
    function restart() {
        retainedStep = ({})
        sequenceIndex = 0
        finished = false
    }
    // 지정된 순서로 다음 프레임을 선택하고 일회 재생은 마지막 프레임에 멈추는 함수.
    function advanceFrame() {
        if (!ready || finished) return
        // 다음 PNG를 읽는 동안 이전 이미지의 영역과 피벗도 함께 유지한다.
        retainedStep = currentStep
        if (sequenceIndex + 1 < steps.length) sequenceIndex++
        else if (descriptor.loop) sequenceIndex = 0
        else finished = true
    }
    onDescriptorChanged: restart()

    Item {
        id: crop
        objectName: "characterFrameRegion"
        x: sprite.metadataValid ? sprite.anchor.x - (sprite.renderedStep.pivot.x + sprite.userPivotOffset.x) * sprite.userScale : 0
        y: sprite.metadataValid ? sprite.anchor.y - (sprite.renderedStep.pivot.y + sprite.userPivotOffset.y) * sprite.userScale : 0
        width: sprite.metadataValid ? sprite.renderedStep.region.width * sprite.pixelScale : 0
        height: sprite.metadataValid ? sprite.renderedStep.region.height * sprite.pixelScale : 0
        visible: sprite.ready || (sprite.metadataValid && sprite.geometryValid && sprite.retainingFrame)
        clip: true
        Image {
            id: atlas
            objectName: "characterAtlas"
            source: sprite.metadataValid ? sprite.currentStep.source : ""
            width: sprite.metadataValid ? sprite.renderedStep.imageWidth * sprite.pixelScale : 0
            height: sprite.metadataValid ? sprite.renderedStep.imageHeight * sprite.pixelScale : 0
            x: sprite.metadataValid ? -sprite.renderedStep.region.x * sprite.pixelScale : 0
            y: sprite.metadataValid ? -sprite.renderedStep.region.y * sprite.pixelScale : 0
            asynchronous: true
            retainWhileLoading: true
            smooth: sprite.descriptor.filtering === "linear"
            cache: true
        }
    }
    Timer {
        objectName: "characterSpriteTimer"
        interval: sprite.metadataValid ? sprite.currentStep.durationMs : 100
        repeat: true
        running: sprite.playing && sprite.visible && sprite.ready && !sprite.finished
            && sprite.steps.length > 1
        onTriggered: sprite.advanceFrame()
    }
}
