import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

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
    property var savedEdits: ({})
    property string editStatus: ""
    property point userPivotOffset: Qt.point(0, 0)
    property point userBubbleOffset: Qt.point(0, 0)
    signal applyRequested(var edits)
    property var drafts: ({})
    property var durationDrafts: ({})
    property real viewZoom: 1
    property real viewPanX: 0
    property real viewPanY: 0
    readonly property var focusedItem: pane.Window.window ? pane.Window.window.activeFocusItem : null
    readonly property bool typing: focusedItem !== null && typeof focusedItem.cursorPosition === "number"
    readonly property var authoredDescriptor: animations && animations.length && choice.currentIndex >= 0 && choice.currentIndex < animations.length ? animations[choice.currentIndex].descriptor : ({})
    readonly property var descriptor: withDurationDrafts(authoredDescriptor)
    readonly property var step: sprite.renderedStep
    readonly property string frameKey: step.frameId || ""
    readonly property var draft: drafts[frameKey] || ({})
    readonly property var basePivot: draft.pivot || step.pivot || ({
            x: 0,
            y: 0
        })
    readonly property var pivot: ({
            x: basePivot.x + userPivotOffset.x,
            y: basePivot.y + userPivotOffset.y
        })
    readonly property var baseAttachment: draft.attachment || bubble.attachment || ({
            x: 0,
            y: -120
        })
    readonly property var attachment: ({
            x: baseAttachment.x + userBubbleOffset.x - userPivotOffset.x,
            y: baseAttachment.y + userBubbleOffset.y - userPivotOffset.y
        })
    // 저장값의 초기 바인딩이 초안을 복원하므로 저장값을 먼저 평가한 뒤 초안을 비교한다.
    readonly property bool dirty: {
        const frames = JSON.stringify(savedEdits.frames || ({}));
        const durations = JSON.stringify(savedEdits.durations || ({}));
        return frames !== JSON.stringify(drafts) || durations !== JSON.stringify(durationDrafts);
    }
    readonly property bool playbackActive: active && running && !clock.finished && sprite.steps.length > 0
    color: "transparent"
    radius: UiMetrics.radiusPanel
    border.width: 0
    clip: true

    // 선택한 프레임에서 재생을 정지하는 함수.
    function selectFrame(index) {
        if (!sprite.steps.length)
            return;
        running = false;
        clock.seekFrame(Math.max(0, Math.min(index, sprite.steps.length - 1)));
    }
    // 재생 막대를 옮긴 정확한 시각에서 멈추는 함수.
    function seekTime(timeMs) {
        running = false;
        clock.seek(timeMs);
    }
    // 애니메이션의 각 등장 순서에 저장한 임시 지속시간을 재생 데이터에 반영하는 함수.
    function withDurationDrafts(authored) {
        const changes = durationDrafts[authored.id];
        if (!changes)
            return authored;
        const steps = sprite.resolveSteps(authored).map(function (step, index) {
            return changes[index] === undefined ? step : Object.assign({}, step, {
                durationMs: changes[index]
            });
        });
        return Object.assign({}, authored, {
            steps: steps
        });
    }
    // 프레임 길이를 변경하되 현재 시간 위치는 보존하는 함수.
    function resizeFrame(index, durationMs) {
        if (!editing || !Number.isInteger(index) || index < 0 || index >= sprite.steps.length || !isFinite(durationMs))
            return;
        running = false;
        const position = clock.positionMs;
        const all = Object.assign({}, durationDrafts);
        const changes = Object.assign({}, all[authoredDescriptor.id] || {});
        changes[index] = Math.max(16, Math.min(60000, Math.round(durationMs)));
        all[authoredDescriptor.id] = changes;
        durationDrafts = all;
        clock.seek(position);
    }
    // 기준점과 지속시간 초안을 함께 초기화하는 함수.
    function resetEdits() {
        drafts = ({});
        durationDrafts = ({});
    }
    // 마지막 적용값을 복제하여 미적용 변경을 되돌리는 함수.
    function reloadEdits() {
        drafts = JSON.parse(JSON.stringify(savedEdits.frames || ({})));
        durationDrafts = JSON.parse(JSON.stringify(savedEdits.durations || ({})));
    }
    // 저장 성공 알림을 화면 위에 잠깐 표시하는 함수.
    function showSavedNotice() {
        saveNoticeAnimation.restart();
    }
    // 편집 데이터를 바꾸지 않고 포인터 위치를 중심으로 보기 배율을 변경하는 함수.
    function zoomPreview(value, centerX, centerY) {
        if (!isFinite(value) || !isFinite(centerX) || !isFinite(centerY))
            return;
        const before = previewWorld.mapFromItem(stage, centerX, centerY);
        viewZoom = Math.max(UiMetrics.companionPreviewZoomMin, Math.min(UiMetrics.companionPreviewZoomMax, value));
        const after = previewWorld.mapToItem(stage, before.x, before.y);
        viewPanX += centerX - after.x;
        viewPanY += centerY - after.y;
    }
    // 보기 배율과 이동을 초기 상태로 되돌리는 함수.
    function resetPreview() {
        viewZoom = 1;
        viewPanX = 0;
        viewPanY = 0;
    }
    // 캐릭터와 말풍선 전체가 보이는 보기 위치와 배율을 구하는 함수.
    function fitPreview() {
        if (!sprite.metadataValid)
            return;
        let left = stage.origin.x, top = stage.origin.y;
        let right = left + step.region.width / step.pixelsPerUnit * stage.zoom;
        let bottom = top + step.region.height / step.pixelsPerUnit * stage.zoom;
        if (showBubble) {
            left = Math.min(left, bubblePreview.x);
            top = Math.min(top, bubblePreview.y);
            right = Math.max(right, bubblePreview.x + bubblePreview.width);
            bottom = Math.max(bottom, bubblePreview.y + bubblePreview.height);
        }
        viewZoom = Math.max(UiMetrics.companionPreviewZoomMin, Math.min(UiMetrics.companionPreviewZoomMax, (stage.width - UiMetrics.spaceSection * 2) / Math.max(1, right - left), (stage.height - UiMetrics.spaceSection * 2) / Math.max(1, bottom - top)));
        viewPanX = (stage.width / 2 - (left + right) / 2) * viewZoom;
        viewPanY = (stage.height / 2 - (top + bottom) / 2) * viewZoom;
    }
    // 이미지 원점 기준의 두 점을 숫자 입력에서 함께 설정하는 함수.
    function setCoordinates(px, py, ax, ay) {
        if (!editing || ![px, py, ax, ay].every(isFinite))
            return;
        running = false;
        movePoint(0, px, py);
        movePoint(1, ax, ay);
    }
    // 현재 기준점을 선택한 애니메이션 안의 모든 프레임에 정확히 복사하는 함수.
    function alignFrames(kind) {
        if (!editing || !sprite.ready || ![0, 1, 2].includes(kind))
            return;
        running = false;
        const sourcePivot = {
            x: basePivot.x,
            y: basePivot.y
        };
        const bubblePoint = {
            x: basePivot.x + baseAttachment.x,
            y: basePivot.y + baseAttachment.y
        };
        const values = Object.assign({}, drafts);
        for (const frame of sprite.steps) {
            const value = Object.assign({}, values[frame.frameId] || {});
            if (kind === 0 || kind === 2)
                value.pivot = {
                    x: sourcePivot.x,
                    y: sourcePivot.y
                };
            const targetPivot = value.pivot || frame.pivot;
            if (kind === 1 || kind === 2)
                value.attachment = {
                    x: bubblePoint.x - targetPivot.x,
                    y: bubblePoint.y - targetPivot.y
                };
            values[frame.frameId] = value;
        }
        drafts = values;
    }
    // 완료한 재생은 처음으로 돌리고 재생과 일시정지를 전환하는 함수.
    function togglePlayback() {
        const resume = !playbackActive;
        if (clock.finished)
            clock.seek(0);
        running = resume;
    }
    // 선택한 프레임의 기준점을 임시 조정값으로 보관하는 함수.
    function movePoint(kind, x, y) {
        if (!editing || !sprite.ready)
            return;
        const values = Object.assign({}, drafts);
        const value = Object.assign({}, values[frameKey] || {});
        if (kind === 0)
            value.pivot = {
                x: x - userPivotOffset.x,
                y: y - userPivotOffset.y
            };
        else
            value.attachment = {
                x: x - pivot.x - userBubbleOffset.x + userPivotOffset.x,
                y: y - pivot.y - userBubbleOffset.y + userPivotOffset.y
            };
        values[frameKey] = value;
        drafts = values;
    }
    onSavedEditsChanged: reloadEdits()
    Component.onCompleted: reloadEdits()
    onAnimationsChanged: {
        editing = false;
        reloadEdits();
        choice.currentIndex = animations && animations.length ? 0 : -1;
        running = true;
        resetPreview();
    }
    onEditingChanged: if (editing) {
        running = false;
        if (active)
            stage.forceActiveFocus();
    }
    onActiveChanged: if (!active)
        running = false
    Shortcut {
        objectName: "companionPreviousFrameShortcut"
        sequence: "Left"
        context: Qt.WindowShortcut
        enabled: pane.active && pane.editing && !pane.typing && !coordinateEditor.opened
        onActivated: pane.selectFrame(sprite.sequenceIndex - 1)
    }
    Shortcut {
        objectName: "companionNextFrameShortcut"
        sequence: "Right"
        context: Qt.WindowShortcut
        enabled: pane.active && pane.editing && !pane.typing && !coordinateEditor.opened
        onActivated: pane.selectFrame(sprite.sequenceIndex + 1)
    }

    CompanionPlaybackClock {
        id: clock
        objectName: "companionPlaybackClock"
        steps: sprite.steps
        playing: pane.active && pane.running
        ready: sprite.ready
        loop: !!pane.descriptor.loop
        onFrameChanging: sprite.retainedStep = sprite.renderedStep
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 0
        spacing: UiMetrics.spaceSection
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: pane.theme.surface
            radius: UiMetrics.radiusCard
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: UiMetrics.spaceGroup
                spacing: UiMetrics.spaceGroup
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: qsTr("애니메이션")
                        color: pane.theme.text
                    }
                    AppComboBox {
                        id: choice
                        objectName: "companionAnimationChoice"
                        Layout.fillWidth: true
                        model: pane.animations
                        textRole: "label"
                        enabled: !pane.editing
                    }
                    AppButton {
                        visible: pane.allowEditing
                        text: pane.editing ? qsTr("편집 종료") : qsTr("프레임 편집")
                        appearance: AppButton.Soft
                        dark: pane.dark
                        checkable: true
                        checked: pane.editing
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("편집 중 ← / →: 이전·다음 프레임 · 편집을 끄면 애니메이션 변경 가능")
                        onClicked: pane.editing = checked
                    }
                    AppButton {
                        visible: pane.editing
                        text: qsTr("기본값")
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("팩 기본값으로 되돌리기")
                        onClicked: pane.resetEdits()
                    }
                    AppButton {
                        objectName: "companionCancelEditsButton"
                        visible: pane.allowEditing && (pane.editing || pane.dirty)
                        text: qsTr("취소")
                        enabled: pane.dirty
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("마지막 적용 상태로 돌아가기")
                        onClicked: {
                            pane.running = false;
                            pane.reloadEdits();
                        }
                    }
                    AppButton {
                        objectName: "companionApplyEditsButton"
                        primary: true
                        visible: pane.allowEditing && (pane.editing || pane.dirty)
                        text: qsTr("적용")
                        enabled: pane.dirty
                        onClicked: pane.applyRequested({
                                                           frames: pane.drafts,
                                                           durations: pane.durationDrafts
                                                       })
                    }
                }
                Rectangle {
                    id: stage
                    objectName: "companionPreviewStage"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: pane.theme.window
                    radius: UiMetrics.radiusControl
                    clip: true
                    Canvas {
                        id: previewGrid
                        anchors.fill: parent
                        onWidthChanged: requestPaint()
                        onHeightChanged: requestPaint()
                        Connections {
                            target: pane
                            // 테마가 바뀌면 고정 배경 점만 다시 그리는 함수.
                            function onThemeChanged() {
                                previewGrid.requestPaint();
                            }
                        }
                        // 표시 배율·팩 데이터와 독립적인 점 배경을 그리는 함수.
                        onPaint: {
                            const ctx = getContext("2d");
                            ctx.reset();
                            ctx.clearRect(0, 0, width, height);
                            ctx.fillStyle = pane.theme.border;
                            const gap = UiMetrics.companionPreviewGridSpacing;
                            for (let x = gap / 2; x < width; x += gap)
                                for (let y = gap / 2; y < height; y += gap)
                                    ctx.fillRect(x, y, UiMetrics.borderNormal, UiMetrics.borderNormal);
                        }
                    }
                    readonly property real frameWidth: sprite.steps.reduce((size, frame) => Math.max(size,
                                                                                                     frame.region.width
                                                                                                     / frame.pixelsPerUnit),
                    1)
                    readonly property real frameHeight: sprite.steps.reduce((size, frame) => Math.max(size,
                                                                                                      frame.region.height
                                                                                                      / frame.pixelsPerUnit),
                    1)
                    readonly property real zoom: Math.max(0.01, Math.min(width * 0.7 / frameWidth, height * 0.7
                                                                         / frameHeight))

                    // 편집 중에는 이미지 원점을 고정하고 재생 중에는 공통 접점에 맞추는 좌표.
                    readonly property point origin: pane.editing && !pane.running && pane.step.region ? Qt.point((width
                                                                                                                  - pane.step.region.width
                                                                                                                  / pane.step.pixelsPerUnit
                                                                                                                  * zoom) / 2,
                                                                                                                 (height - pane.step.region.height
                                                                                                                  / pane.step.pixelsPerUnit
                                                                                                                  * zoom) / 2) :
                                                                                                        Qt.point(width
                                                                                                                 / 2 - pane.pivot.x
                                                                                                                 * zoom, height
                                                                                                                 * 0.8 - pane.pivot.y
                                                                                                                 * zoom)
                    Item {
                        id: previewWorld
                        objectName: "companionPreviewWorld"
                        width: stage.width
                        height: stage.height
                        x: pane.viewPanX
                        y: pane.viewPanY
                        scale: pane.viewZoom
                        transformOrigin: Item.Center
                        CharacterSprite {
                            id: sprite
                            objectName: "companionPagePreview"
                            anchors.fill: parent
                            descriptor: pane.descriptor
                            playing: false
                            externallyDriven: true
                            sequenceIndex: clock.currentIndex
                            userScale: stage.zoom
                            anchor: pane.step.pivot ? Qt.point(stage.origin.x + pane.step.pivot.x * userScale,
                                                               stage.origin.y + pane.step.pivot.y * userScale) :
                                                      Qt.point(0, 0)
                        }
                        CharacterBubbleView {
                            id: bubblePreview
                            objectName: "companionBubblePreview"
                            visible: pane.showBubble && pane.active
                            configuration: pane.bubble
                            dark: pane.dark
                            message: pane.previewMessage
                            constrainToArea: false
                            screenArea: Qt.rect(0, 0, parent.width, parent.height)
                            anchorPoint: Qt.point(stage.origin.x + (pane.pivot.x + pane.attachment.x) * stage.zoom,
                                                  stage.origin.y + (pane.pivot.y + pane.attachment.y) * stage.zoom)
                            x: placementX
                            y: placementY
                        }
                        Repeater {
                            model: 2
                            delegate: Item {
                                id: handle
                                required property int index
                                visible: pane.editing && !pane.running && sprite.metadataValid
                                readonly property color guide: index === 0 ? pane.theme.pivotGuide :
                                                                             pane.theme.bubbleGuide
                                x: stage.origin.x + (pane.pivot.x + (index === 1 ? pane.attachment.x : 0)) * stage.zoom
                                y: stage.origin.y + (pane.pivot.y + (index === 1 ? pane.attachment.y : 0)) * stage.zoom
                                Rectangle {
                                    x: -handle.x
                                    width: stage.width
                                    height: UiMetrics.borderNormal
                                    color: handle.guide
                                    opacity: 0.35
                                }
                                Rectangle {
                                    y: -handle.y
                                    width: UiMetrics.borderNormal
                                    height: stage.height
                                    color: handle.guide
                                    opacity: 0.35
                                }
                                AnchorHandle {
                                    id: marker
                                    x: -width / 2
                                    y: -height / 2
                                    scale: 1 / pane.viewZoom
                                    markerColor: handle.guide
                                    backdropColor: pane.theme.surface
                                    diamond: handle.index === 1
                                    description: handle.index === 0 ? qsTr("캐릭터 기준점 · 십자 중심을 드래그") : qsTr(
                                                                          "말풍선 부착점 · 십자 중심을 드래그")
                                    // 이미지 논리 좌표로 변환하여 기준점을 이동하는 함수.
                                    onDragged: function (localX, localY) {
                                        const pos = marker.mapToItem(previewWorld, localX, localY);
                                        pane.movePoint(handle.index, (pos.x - stage.origin.x) / stage.zoom, (pos.y
                                                                                                             - stage.origin.y)
                                                       / stage.zoom);
                                    }
                                }
                            }
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.RightButton | Qt.MiddleButton
                        preventStealing: true
                        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.ArrowCursor
                        property point startMouse: Qt.point(0, 0)
                        property point startPan: Qt.point(0, 0)
                        // 우클릭 또는 가운데 버튼을 누른 위치에서 화면 이동을 시작하는 함수.
                        onPressed: function (mouse) {
                            stage.forceActiveFocus();
                            startMouse = Qt.point(mouse.x, mouse.y);
                            startPan = Qt.point(pane.viewPanX, pane.viewPanY);
                        }
                        // 캐릭터 좌표를 바꾸지 않고 보기 위치만 이동하는 함수.
                        onPositionChanged: function (mouse) {
                            if (!pressed)
                                return;
                            pane.viewPanX = startPan.x + mouse.x - startMouse.x;
                            pane.viewPanY = startPan.y + mouse.y - startMouse.y;
                        }
                    }
                    WheelHandler {
                        id: previewWheel
                        target: null
                        blocking: true
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                        // 마우스 위치를 유지하며 휠 또는 터치패드로 확대·축소하는 함수.
                        onWheel: function (event) {
                            const delta = event.angleDelta.y || (event.pixelDelta ? event.pixelDelta.y : 0);
                            if (delta === 0)
                                return;
                            pane.zoomPreview(pane.viewZoom * Math.pow(UiMetrics.companionPreviewZoomStep, delta > 0 ? 1 : -1),
                                             previewWheel.point.position.x, previewWheel.point.position.y);
                        }
                    }
                    Rectangle {
                        objectName: "companionCoordinateReadout"
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.margins: UiMetrics.spaceGroup
                        width: Math.min(UiMetrics.companionCoordinateWidth, parent.width - UiMetrics.spaceGroup * 2)
                        height: coordinateLabels.implicitHeight + UiMetrics.spaceControl * 2
                        visible: pane.editing && sprite.metadataValid
                        color: pane.theme.alternate
                        border.color: pane.theme.border
                        radius: UiMetrics.radiusControl
                        Column {
                            id: coordinateLabels
                            anchors.fill: parent
                            anchors.margins: UiMetrics.spaceControl
                            spacing: UiMetrics.spaceSmall
                            Label {
                                width: parent.width
                                text: qsTr("%1 · 이미지 좌상단 기준").arg(pane.frameKey)
                                elide: Text.ElideRight
                                color: pane.theme.muted
                                font.pixelSize: UiMetrics.textBody
                            }
                            Label {
                                objectName: "companionPivotCoordinates"
                                text: qsTr("기준점   X %1   Y %2").arg(pane.pivot.x.toFixed(1)).arg(pane.pivot.y.toFixed(
                                                                                                     1))

                                color: pane.theme.pivotGuide
                                font.pixelSize: UiMetrics.textBody
                                font.bold: true
                            }
                            Label {
                                objectName: "companionAttachmentCoordinates"
                                text: qsTr("말풍선   X %1   Y %2").arg((pane.pivot.x + pane.attachment.x).toFixed(1)).arg((
                                                                                                                           pane.pivot.y
                                                                                                                           + pane.attachment.y).toFixed(
                                                                                                                           1))
                                color: pane.theme.bubbleGuide
                                font.pixelSize: UiMetrics.textBody
                                font.bold: true
                            }
                            Row {
                                spacing: UiMetrics.spaceSmall
                                AppButton {
                                    text: qsTr("좌표 입력")
                                    enabled: !pane.running && sprite.ready
                                    onClicked: coordinateEditor.open()
                                }
                                AppButton {
                                    text: qsTr("프레임 정렬")
                                    enabled: !pane.running && sprite.ready
                                    onClicked: alignmentMenu.open()
                                    Menu {
                                        id: alignmentMenu
                                        MenuItem {
                                            text: qsTr("현재 기준점을 전체 프레임에 복사")
                                            onTriggered: pane.alignFrames(0)
                                        }
                                        MenuItem {
                                            text: qsTr("현재 말풍선 위치를 전체 프레임에 복사")
                                            onTriggered: pane.alignFrames(1)
                                        }
                                        MenuItem {
                                            text: qsTr("두 점 모두 전체 프레임에 복사")
                                            onTriggered: pane.alignFrames(2)
                                        }
                                        MenuSeparator {}
                                        MenuItem {
                                            text: qsTr("현재 두 점을 정수 좌표로 맞추기")
                                            onTriggered: pane.setCoordinates(Math.round(pane.pivot.x), Math.round(
                                                                                 pane.pivot.y), Math.round(pane.pivot.x
                                                                                                           + pane.attachment.x),
                                                                             Math.round(pane.pivot.y
                                                                                        + pane.attachment.y))
                                        }
                                    }
                                }
                            }
                        }
                    }
                    CompanionCoordinateEditor {
                        id: coordinateEditor
                        parent: stage
                        pivot: Qt.point(pane.pivot.x, pane.pivot.y)
                        attachment: Qt.point(pane.pivot.x + pane.attachment.x, pane.pivot.y + pane.attachment.y)
                        onSubmitted: function (px, py, ax, ay) {
                            pane.setCoordinates(px, py, ax, ay);
                        }
                        onClosed: stage.forceActiveFocus()
                    }
                    Rectangle {
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: UiMetrics.spaceGroup
                        width: zoomActions.implicitWidth + UiMetrics.spaceExtraSmall * 2
                        height: UiMetrics.controlHeight + UiMetrics.spaceExtraSmall * 2
                        color: pane.theme.surface
                        radius: UiMetrics.radiusControl
                        border.color: pane.theme.border
                        RowLayout {
                            id: zoomActions
                            anchors.centerIn: parent
                            spacing: 0
                            AppButton {
                                implicitWidth: UiMetrics.controlHeight
                                flat: true
                                dark: pane.dark
                                text: "−"
                                Accessible.name: qsTr("미리보기 축소")
                                onClicked: pane.zoomPreview(pane.viewZoom / UiMetrics.companionPreviewZoomStep,
                                                            stage.width / 2, stage.height / 2)
                            }
                            AppButton {
                                text: Math.round(pane.viewZoom * 100) + "%"
                                flat: true
                                dark: pane.dark
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("클릭: 보기 초기화 · 휠: 확대·축소 · 우클릭/가운데 드래그: 화면 이동")
                                onClicked: pane.resetPreview()
                            }
                            AppButton {
                                implicitWidth: UiMetrics.controlHeight
                                flat: true
                                dark: pane.dark
                                text: "+"
                                Accessible.name: qsTr("미리보기 확대")
                                onClicked: pane.zoomPreview(pane.viewZoom * UiMetrics.companionPreviewZoomStep,
                                                            stage.width / 2, stage.height / 2)
                            }
                            AppButton {
                                implicitWidth: UiMetrics.controlHeight
                                flat: true
                                dark: pane.dark
                                Accessible.name: qsTr("캐릭터와 말풍선 전체 보기")
                                ToolTip.visible: hovered
                                ToolTip.text: Accessible.name
                                contentItem: NavigationIcon {
                                    kind: "fit"
                                    strokeColor: pane.theme.text
                                }
                                onClicked: pane.fitPreview()
                            }
                        }
                    }
                    Rectangle {
                        id: savedNotice
                        objectName: "companionSavedNotice"
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: UiMetrics.spaceGroup
                        width: savedNoticeText.implicitWidth + UiMetrics.spaceSection * 2
                        height: UiMetrics.controlHeight
                        radius: UiMetrics.radiusControl
                        color: pane.theme.accent
                        opacity: 0
                        visible: opacity > 0
                        Label {
                            id: savedNoticeText
                            anchors.centerIn: parent
                            text: qsTr("저장되었습니다.")
                            color: pane.theme.accentText
                            font.pixelSize: UiMetrics.textBody
                        }
                    }
                    SequentialAnimation {
                        id: saveNoticeAnimation
                        PropertyAction {
                            target: savedNotice
                            property: "opacity"
                            value: 0
                        }
                        NumberAnimation {
                            target: savedNotice
                            property: "opacity"
                            to: 1
                            duration: UiMetrics.transientNoticeFadeMs
                        }
                        PauseAnimation {
                            duration: UiMetrics.transientNoticeHoldMs
                        }
                        NumberAnimation {
                            target: savedNotice
                            property: "opacity"
                            to: 0
                            duration: UiMetrics.transientNoticeFadeMs
                        }
                    }
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: UiMetrics.spaceGroup
                        height: errorLabel.implicitHeight + UiMetrics.spaceControl * 2
                        visible: pane.editStatus.length > 0
                        color: pane.theme.surface
                        border.color: pane.theme.border
                        radius: UiMetrics.radiusControl
                        Label {
                            id: errorLabel
                            anchors.fill: parent
                            anchors.margins: UiMetrics.spaceControl
                            text: pane.editStatus
                            color: pane.theme.text
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }
        }
        CompanionPlayback {
            dark: pane.dark
            objectName: "companionPlaybackSection"
            Layout.fillWidth: true
            steps: sprite.steps
            theme: pane.theme
            filtering: pane.descriptor.filtering || "nearest"
            playing: pane.playbackActive
            finished: clock.finished
            positionMs: clock.positionMs
            editable: pane.editing
            currentIndex: sprite.sequenceIndex
            onFrameSelected: function (index) {
                pane.selectFrame(index);
            }
            onToggleRequested: pane.togglePlayback()
            onTimeSelected: function (timeMs) {
                pane.seekTime(timeMs);
            }
            onDurationEditStarted: pane.running = false
            onDurationChanged: function (index, durationMs) {
                pane.resizeFrame(index, durationMs);
            }
        }
    }
}
