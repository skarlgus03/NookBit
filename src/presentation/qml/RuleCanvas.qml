import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 캔버스 입력·격자·연결선·노드 배치만 담당하는 화면.
Rectangle {
    id: canvas
    required property var editor
    readonly property alias viewport: canvasViewport
    property bool dark: false
    readonly property var theme: UiTheme.colors(dark)
    readonly property color canvasColor: theme.editorCanvas
    readonly property color borderColor: theme.border
    readonly property color gridColor: theme.editorGrid
    readonly property color buttonFocusColor: theme.buttonFocus
    onDarkChanged: if (lineCanvas) lineCanvas.requestPaint()

    radius: UiMetrics.radiusPanel
    color: canvas.canvasColor
    border.color: canvas.borderColor

    Item {
        id: canvasViewport
        objectName: "ruleCanvasViewport"
        anchors.fill: parent
        anchors.margins: 1
        clip: true
        focus: true
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Alt) {
                canvas.editor.altHeld = true;
                event.accepted = true;
            } else if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
                canvas.editor.removeSelectedBlock();
                event.accepted = true;
            } else if (event.modifiers & Qt.ControlModifier) {
                if ((event.modifiers & Qt.ShiftModifier) && event.key === Qt.Key_L) {
                    canvas.editor.alignSelectedVertically();
                    event.accepted = true;
                } else if (event.key === Qt.Key_C) {
                    canvas.editor.copySelectedBlocks();
                    event.accepted = true;
                } else if (event.key === Qt.Key_V) {
                    canvas.editor.pasteCopiedBlocks();
                    event.accepted = true;
                }
            }
        }
        Keys.onReleased: event => {
            if (event.key === Qt.Key_Alt) {
                canvas.editor.altHeld = false;
                event.accepted = true;
            }
        }
        onActiveFocusChanged: {
            if (!activeFocus)
                canvas.editor.altHeld = false;
        }
        onWidthChanged: {
            lineCanvas.requestPaint();
        }
        onHeightChanged: {
            lineCanvas.requestPaint();
        }

        WheelHandler {
            target: null
            blocking: true
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: event => canvas.editor.zoomAt(event.angleDelta.y, point.position.x, point.position.y)
        }

        Canvas {
            id: lineCanvas
            anchors.fill: parent
            antialiasing: true
            onPaint: {
                const context = getContext("2d");
                context.clearRect(0, 0, width, height);
                context.fillStyle = canvas.canvasColor;
                context.fillRect(0, 0, width, height);
                context.strokeStyle = canvas.gridColor;
                context.lineWidth = 1;
                let gridStep = 40 * canvas.editor.zoom;
                while (gridStep < 24)
                    gridStep *= 2;
                while (gridStep > 96)
                    gridStep /= 2;
                const gridX = ((canvas.editor.panX % gridStep) + gridStep) % gridStep;
                const gridY = ((canvas.editor.panY % gridStep) + gridStep) % gridStep;
                for (let x = gridX; x <= width; x += gridStep) {
                    context.beginPath();
                    context.moveTo(x, 0);
                    context.lineTo(x, height);
                    context.stroke();
                }
                for (let y = gridY; y <= height; y += gridStep) {
                    context.beginPath();
                    context.moveTo(0, y);
                    context.lineTo(width, y);
                    context.stroke();
                }
                if (canvas.editor.snapXActive || canvas.editor.snapYActive) {
                    context.strokeStyle = canvas.theme.snap;
                    context.lineWidth = 2;
                    if (canvas.editor.snapXActive) {
                        const guideX = canvas.editor.screenPoint(canvas.editor.snapGuideX, 0).x;
                        context.beginPath();
                        context.moveTo(guideX, 0);
                        context.lineTo(guideX, height);
                        context.stroke();
                    }
                    if (canvas.editor.snapYActive) {
                        const guideY = canvas.editor.screenPoint(0, canvas.editor.snapGuideY
                                                                 + canvas.editor.blockHeight / 2).y;
                        context.beginPath();
                        context.moveTo(0, guideY);
                        context.lineTo(width, guideY);
                        context.stroke();
                    }
                }
                for (let index = 0; index < canvas.editor.edges.length; ++index) {
                    const edge = canvas.editor.edges[index];
                    const source = canvas.editor.blockForId(edge.from);
                    const target = canvas.editor.blockForId(edge.to);
                    if (source && target) {
                        const sourcePos = canvas.editor.positionFor(source);
                        const targetPos = canvas.editor.positionFor(target);
                        const from = canvas.editor.screenPoint(sourcePos.x + canvas.editor.blockWidth, sourcePos.y
                                                               + canvas.editor.blockHeight / 2);
                        const to = canvas.editor.screenPoint(targetPos.x, targetPos.y + canvas.editor.blockHeight / 2);
                        canvas.drawEdge(context, from.x, from.y, to.x, to.y, false, canvas.editor.altHeld && index
                                        === canvas.editor.hoveredEdgeIndex, canvas.editor.testTraceBlockIds.includes(
                                            edge.from) && canvas.editor.testTraceBlockIds.includes(edge.to));
                    }
                }
                if (canvas.editor.connectingFrom >= 0 && canvas.editor.draggingConnection) {
                    const source = canvas.editor.blockForId(canvas.editor.connectingFrom);
                    if (source) {
                        const sourcePos = canvas.editor.positionFor(source);
                        const from = canvas.editor.screenPoint(sourcePos.x + canvas.editor.blockWidth, sourcePos.y
                                                               + canvas.editor.blockHeight / 2);
                        const to = canvas.editor.screenPoint(canvas.editor.pointerX, canvas.editor.pointerY);
                        canvas.drawEdge(context, from.x, from.y, to.x, to.y, true, false, false);
                    }
                }
            }
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            preventStealing: true
            property bool breakingEdge: false
            onPressed: mouse => {
                canvasViewport.forceActiveFocus();
                if (mouse.modifiers & Qt.AltModifier)
                    canvas.editor.hoveredEdgeIndex = canvas.editor.edgeAt(mouse.x, mouse.y);
                breakingEdge = (mouse.modifiers & Qt.AltModifier) && canvas.editor.disconnectHoveredEdge();
                if (breakingEdge)
                    return;
                canvas.editor.marqueeStartX = mouse.x;
                canvas.editor.marqueeStartY = mouse.y;
                canvas.editor.marqueeEndX = mouse.x;
                canvas.editor.marqueeEndY = mouse.y;
                canvas.editor.marqueeActive = true;
            }
            onPositionChanged: mouse => {
                if (!pressed || breakingEdge)
                    return;
                canvas.editor.marqueeEndX = mouse.x;
                canvas.editor.marqueeEndY = mouse.y;
            }
            onReleased: mouse => {
                if (breakingEdge) {
                    breakingEdge = false;
                    return;
                }
                canvas.editor.marqueeEndX = mouse.x;
                canvas.editor.marqueeEndY = mouse.y;
                if (Math.abs(mouse.x - canvas.editor.marqueeStartX) < 4 && Math.abs(mouse.y
                                                                                    - canvas.editor.marqueeStartY)
                        < 4) {
                    if (!(mouse.modifiers & Qt.ShiftModifier))
                        canvas.editor.selectOnly(-1);
                    canvas.editor.marqueeActive = false;
                } else {
                    canvas.editor.finishMarquee((mouse.modifiers & Qt.ShiftModifier) !== 0);
                }
            }
            onCanceled: {
                breakingEdge = false;
                canvas.editor.marqueeActive = false;
            }
        }

        Repeater {
            model: canvas.editor.blocks
            delegate: RuleNodeCard {
                editor: canvas.editor
                viewport: canvasViewport
                dark: canvas.dark
                onFocusRequested: canvasViewport.forceActiveFocus()
                onRepaintRequested: lineCanvas.requestPaint()
            }
        }

        Rectangle {
            visible: canvas.editor.marqueeActive
            x: Math.min(canvas.editor.marqueeStartX, canvas.editor.marqueeEndX)
            y: Math.min(canvas.editor.marqueeStartY, canvas.editor.marqueeEndY)
            width: Math.abs(canvas.editor.marqueeEndX - canvas.editor.marqueeStartX)
            height: Math.abs(canvas.editor.marqueeEndY - canvas.editor.marqueeStartY)
            color: canvas.theme.marquee
            border.color: canvas.buttonFocusColor
            border.width: 2
        }
    }

    MouseArea {
        id: rightPanArea
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        preventStealing: true
        property real pressX: 0
        property real pressY: 0
        property real lastX: 0
        property real lastY: 0
        property bool dragged: false
        onPressed: mouse => {
            canvasViewport.forceActiveFocus();
            pressX = lastX = mouse.x;
            pressY = lastY = mouse.y;
            dragged = false;
        }
        onPositionChanged: mouse => {
            if (!pressed)
                return;
            if (!dragged && Math.abs(mouse.x - pressX) < 5 && Math.abs(mouse.y - pressY) < 5)
                return;
            dragged = true;
            canvas.editor.panX += mouse.x - lastX;
            canvas.editor.panY += mouse.y - lastY;
            lastX = mouse.x;
            lastY = mouse.y;
        }
        onReleased: mouse => {
            if (!dragged) {
                const point = mapToItem(canvasViewport, mouse.x, mouse.y);
                canvas.openAddMenu(point.x, point.y);
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        visible: canvas.editor.altHeld
        acceptedButtons: Qt.NoButton
        hoverEnabled: true
        onEntered: {
            const point = mapToItem(canvasViewport, mouseX, mouseY);
            canvas.editor.updateHoveredEdge(point.x, point.y);
        }
        onPositionChanged: mouse => {
            const point = mapToItem(canvasViewport, mouse.x, mouse.y);
            canvas.editor.updateHoveredEdge(point.x, point.y);
        }
        onExited: {
            canvas.editor.hoveredEdgeIndex = -1;
            lineCanvas.requestPaint();
        }
    }
    // 이 캔버스의 키보드 조작 대상으로 초점을 옮기는 함수.
    function focusCanvas() {
        canvasViewport.forceActiveFocus();
    }
    // 페이지를 떠날 때 추가 메뉴만 닫는 함수.
    function dismissTransientUi() {
        addNodeMenu.close();
        editor.cancelConnection();
    }
    Connections {
        target: canvas.editor
        // 편집 상태의 갱신을 선 그리기에 반영하는 함수.
        function onRepaintRequested() {
            lineCanvas.requestPaint();
        }
    }
    component ThemeButton: RuleEditorButton {
        dark: canvas.dark
    }
    // 화면의 지정 위치에서 블록 추가 메뉴를 열고 삽입 좌표를 기억하는 함수.
    function openAddMenu(screenX, screenY) {
        const point = editor.worldPoint(screenX, screenY);
        editor.addMenuWorldX = point.x;
        editor.addMenuWorldY = point.y;
        addNodeMenu.x = Math.max(0, Math.min(screenX, canvasViewport.width - addNodeMenu.width));
        addNodeMenu.y = Math.max(0, Math.min(screenY, canvasViewport.height - addNodeMenu.height));
        addNodeMenu.open();
    }
    // 선택한 직선·곡선 방식으로 연결선과 드래그 중인 선을 그리는 함수.
    function drawEdge(context, x1, y1, x2, y2, temporary, highlighted, tested) {
        context.beginPath();
        context.moveTo(x1, y1);
        if (editor.connectionStyle === 0) {
            for (const curve of editor.connectionSegments(x1, y1, x2, y2))
                context.bezierCurveTo(curve.cx1, curve.cy1, curve.cx2, curve.cy2, curve.x2, curve.y2);
        } else {
            context.lineTo(x2, y2);
        }
        context.lineWidth = highlighted || tested ? 8 : Math.max(1, (temporary ? 4 : 4.5) * editor.zoom);
        context.lineCap = "round";
        context.strokeStyle = highlighted ? theme.edgeDisconnect : tested ? theme.edgeTrace : temporary
                                                                            ? theme.edgePending : theme.edge;
        context.stroke();
    }
    Popup {
        id: addNodeMenu
        objectName: "addNodeMenu"
        parent: canvasViewport
        width: UiMetrics.nodeAddMenuWidth
        height: Math.min(nodeAddColumn.implicitHeight + topPadding + bottomPadding, Math.max(UiMetrics.controlHeight, canvasViewport.height
                                                                                             - UiMetrics.spaceControl
                                                                                             * 2))
        padding: UiMetrics.buttonPadding
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            radius: UiMetrics.radiusControl
            color: canvas.theme.surface
            border.color: canvas.borderColor
        }
        contentItem: ScrollView {
            id: nodeAddScroll
            contentWidth: availableWidth
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            Column {
                id: nodeAddColumn
                width: nodeAddScroll.availableWidth
                spacing: UiMetrics.spaceGroup
                Repeater {
                    model: canvas.editor.nodeAddGroups
                    delegate: Column {
                        id: nodeGroup
                        required property var modelData
                        width: nodeAddColumn.width
                        spacing: UiMetrics.spaceExtraSmall
                        Label {
                            objectName: "nodeAddGroupHeading"
                            width: parent.width
                            leftPadding: UiMetrics.spaceControl
                            text: nodeGroup.modelData.title
                            font.pixelSize: UiMetrics.textCaption
                            font.bold: true
                            color: canvas.theme.muted
                        }
                        Repeater {
                            model: nodeGroup.modelData.nodes
                            delegate: ThemeButton {
                                required property var modelData
                                objectName: "addNode_" + modelData.type
                                width: nodeGroup.width
                                text: "+ " + modelData.title
                                hoverEnabled: true
                                ToolTip.visible: hovered
                                ToolTip.text: nodeGroup.modelData.hint
                                onClicked: {
                                    canvas.editor.addBlock(modelData.type, canvas.editor.addMenuWorldX,
                                                           canvas.editor.addMenuWorldY);
                                    addNodeMenu.close();
                                    canvasViewport.forceActiveFocus();
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
