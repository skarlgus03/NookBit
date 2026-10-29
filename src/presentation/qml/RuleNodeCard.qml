import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 노드 하나를 그리며 선택·드래그·연결 요청을 편집 세션에 전달한다.
Rectangle {
    id: blockCard
    required property var modelData
    required property var editor
    required property Item viewport
    signal focusRequested
    signal repaintRequested
    property bool dark: false
    readonly property var theme: UiTheme.colors(dark)
    readonly property color borderColor: theme.border
    readonly property color primaryText: theme.text
    readonly property color mutedText: theme.muted
    readonly property color nodeColor: theme.nodeSurface

    x: blockCard.editor.screenPoint(blockCard.editor.positionFor(modelData).x, blockCard.editor.positionFor(
                                        modelData).y).x
    y: blockCard.editor.screenPoint(blockCard.editor.positionFor(modelData).x, blockCard.editor.positionFor(
                                        modelData).y).y
    width: blockCard.editor.blockWidth
    height: blockCard.editor.blockHeight
    scale: blockCard.editor.zoom
    transformOrigin: Item.TopLeft
    radius: modelData.type === "start" ? height / 2 : 12
    color: modelData.type === "start" ? blockCard.theme.nodeInputSurface : blockCard.nodeColor
    opacity: modelData.enabled === false ? 0.55 : 1.0
    border.width: blockCard.editor.testTraceBlockIds.includes(modelData.id) ? 4 : modelData.type === "start" ? (
                                                                                                                   blockCard.editor.selectedIds.includes(
                                                                                                                       modelData.id)
                                                                                                                   ? 5 : 3) :
                                                                                                               blockCard.editor.selectedIds.includes(
                                                                                                                   modelData.id)
                                                                                                               ? 3 : 1
    border.color: blockCard.editor.testTraceBlockIds.includes(modelData.id) ? blockCard.theme.edgeTrace : modelData.type
                                                                              === "start"
                                                                              ? blockCard.theme.nodeInputBorder :
                                                                                blockCard.editor.selectedIds.includes(
                                                                                    modelData.id) ? blockCard.colorFor(
                                                                                                        modelData.type) :
                                                                                                    blockCard.borderColor

    // 유일한 파일 입력은 조건 카드와 구별되는 캡슐형 시작점으로 표시한다.
    Row {
        visible: modelData.type === "start"
        anchors.left: parent.left
        anchors.leftMargin: 18
        anchors.verticalCenter: parent.verticalCenter
        spacing: UiMetrics.spaceGroup
        Rectangle {
            width: 42
            height: 42
            radius: 21
            color: blockCard.theme.nodeInputIcon
            anchors.verticalCenter: parent.verticalCenter
            Label {
                anchors.centerIn: parent
                text: "▶"
                font.pixelSize: UiMetrics.nodeSymbolSizes[blockCard.editor.fontPresetIndex]
                color: blockCard.theme.onNode
            }
        }
        Column {
            anchors.verticalCenter: parent.verticalCenter
            spacing: UiMetrics.spaceExtraSmall
            Label {
                text: qsTr("파일 입력")
                font.bold: true
                font.pixelSize: blockCard.editor.nodeFontSize
                color: blockCard.theme.nodeInputText
            }
            Label {
                text: qsTr("모든 분류의 시작점")
                font.pixelSize: UiMetrics.nodeCaptionSizes[blockCard.editor.fontPresetIndex]
                color: blockCard.theme.nodeInputMuted
            }
        }
    }
    Rectangle {
        id: cardHeader
        visible: modelData.type !== "start"
        x: 1
        y: 1
        width: parent.width - 2
        height: Math.round(blockCard.editor.blockHeight * 0.35)
        radius: 11
        color: blockCard.colorFor(modelData.type)
        Label {
            anchors.left: parent.left
            anchors.leftMargin: blockCard.editor.orderFor(modelData.id) > 0 ? 42 : 12
            anchors.right: parent.right
            anchors.rightMargin: rootBadge.visible ? 60 : 10
            anchors.verticalCenter: parent.verticalCenter
            text: blockCard.editor.titleFor(modelData.type)
            font.bold: true
            font.pixelSize: blockCard.editor.nodeFontSize
            elide: Text.ElideRight
            color: blockCard.theme.onNode
        }
        Rectangle {
            visible: blockCard.editor.orderFor(modelData.id) > 0
            x: 9
            anchors.verticalCenter: parent.verticalCenter
            width: 25
            height: 24
            radius: UiMetrics.radiusControl
            color: blockCard.theme.onNode
            opacity: 0.94
            Label {
                anchors.centerIn: parent
                text: blockCard.editor.orderFor(modelData.id)
                font.bold: true
                font.pixelSize: blockCard.editor.nodeFontSize
                color: blockCard.colorFor(modelData.type)
            }
        }
        // 파일 입력에 직접 연결된 조건을 테두리 대신 작은 배지로 표시한다.
        Rectangle {
            id: rootBadge
            visible: blockCard.editor.isRootBlock(modelData.id)
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            width: 43
            height: 23
            radius: 11
            color: blockCard.theme.onNode
            Label {
                anchors.centerIn: parent
                text: qsTr("루트")
                font.bold: true
                font.pixelSize: UiMetrics.nodeCaptionSizes[blockCard.editor.fontPresetIndex]
                color: blockCard.colorFor(modelData.type)
            }
        }
    }
    Label {
        visible: modelData.type !== "start"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: cardHeader.bottom
        // 연결점의 클릭 영역이 카드 안쪽까지 들어오므로 본문은 그 밖에서 시작한다.
        anchors.leftMargin: 28
        anchors.rightMargin: modelData.type === "destination" || modelData.type === "exclude" ? 12 : 28
        anchors.topMargin: 7
        text: blockCard.editor.displayValue(modelData)
        elide: Text.ElideRight
        font.pixelSize: blockCard.editor.nodeFontSize
        font.weight: Font.DemiBold
        color: blockCard.primaryText
    }
    Label {
        visible: modelData.type !== "start"
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 12
        anchors.bottomMargin: 8
        text: modelData.type === "destination" ? qsTr("도착") : modelData.type === "exclude" ? qsTr("이동 안 함") : qsTr(
                                                                                                 "일치 →")
        font.pixelSize: UiMetrics.nodeCaptionSizes[blockCard.editor.fontPresetIndex]
        font.weight: Font.Medium
        color: blockCard.mutedText
    }
    MouseArea {
        id: blockDragArea
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        preventStealing: true
        property real pressWorldX: 0
        property real pressWorldY: 0
        property var startPositions: ({})
        property bool dragAllowed: false
        property bool moved: false
        onPressed: mouse => {
            blockCard.focusRequested();
            moved = false;
            blockCard.editor.snapXActive = false;
            blockCard.editor.snapYActive = false;
            dragAllowed = !(mouse.modifiers & Qt.ShiftModifier);
            if (!dragAllowed) {
                blockCard.editor.toggleSelection(modelData.id);
                return;
            }
            if (!blockCard.editor.selectedIds.includes(modelData.id))
                blockCard.editor.selectOnly(modelData.id);
            const screen = mapToItem(blockCard.viewport, mouse.x, mouse.y);
            const point = blockCard.editor.worldPoint(screen.x, screen.y);
            pressWorldX = point.x;
            pressWorldY = point.y;
            const starts = ({});
            for (const id of blockCard.editor.selectedIds) {
                const block = blockCard.editor.blockForId(id);
                if (block)
                    starts[id] = {
                        x: block.x,
                        y: block.y
                    };
            }
            startPositions = starts;
        }
        onPositionChanged: mouse => {
            if (!pressed || !dragAllowed)
                return;
            const screen = mapToItem(blockCard.viewport, mouse.x, mouse.y);
            const point = blockCard.editor.worldPoint(screen.x, screen.y);
            const dx = point.x - pressWorldX;
            const dy = point.y - pressWorldY;
            if (!moved && Math.abs(dx) < 3 && Math.abs(dy) < 3)
                return;
            moved = true;
            blockCard.editor.updateDraggedPositions(modelData.id, startPositions, dx, dy, (mouse.modifiers
                                                                                           & Qt.ControlModifier) !== 0);
        }
        onReleased: {
            if (moved)
                blockCard.editor.commitDraggedBlocks();
            else if (dragAllowed && blockCard.editor.selectedIds.length > 1)
                blockCard.editor.selectOnly(modelData.id);
            dragAllowed = false;
        }
        onCanceled: {
            blockCard.editor.livePositions = ({});
            blockCard.editor.snapXActive = false;
            blockCard.editor.snapYActive = false;
            blockCard.repaintRequested();
            dragAllowed = false;
        }
    }
    Rectangle {
        id: inputPort
        visible: modelData.type !== "start"
        x: -8
        y: parent.height / 2 - 8
        width: 16
        height: 16
        radius: UiMetrics.radiusControl
        color: blockCard.editor.hasIncoming(modelData.id) ? blockCard.colorFor(modelData.type) : blockCard.nodeColor
        border.width: 3
        border.color: blockCard.colorFor(modelData.type)
        MouseArea {
            anchors.fill: parent
            anchors.margins: -12
            onClicked: {
                if (blockCard.editor.connectingFrom >= 0)
                    blockCard.editor.connectBlocks(blockCard.editor.connectingFrom, modelData.id);
                else
                    blockCard.editor.selectOnly(modelData.id);
            }
        }
    }
    Rectangle {
        id: outputPort
        visible: modelData.type !== "destination" && modelData.type !== "exclude"
        x: parent.width - width / 2
        y: parent.height / 2 - height / 2
        width: modelData.type === "start" ? 22 : 16
        height: width
        radius: width / 2
        color: blockCard.editor.hasOutgoing(modelData.id) ? blockCard.colorFor(modelData.type) : blockCard.nodeColor
        border.width: modelData.type === "start" ? 3 : 2
        border.color: blockCard.colorFor(modelData.type)
        MouseArea {
            objectName: "ruleOutputPort_" + modelData.id
            anchors.fill: parent
            anchors.margins: -12
            preventStealing: true
            onPressed: mouse => {
                blockCard.editor.connectingFrom = modelData.id;
                blockCard.editor.draggingConnection = true;
                const screen = mapToItem(blockCard.viewport, mouse.x, mouse.y);
                const point = blockCard.editor.worldPoint(screen.x, screen.y);
                blockCard.editor.pointerX = point.x;
                blockCard.editor.pointerY = point.y;
                blockCard.repaintRequested();
            }
            onPositionChanged: mouse => {
                if (blockCard.editor.connectingFrom === modelData.id && blockCard.editor.draggingConnection) {
                    const screen = mapToItem(blockCard.viewport, mouse.x, mouse.y);
                    const point = blockCard.editor.worldPoint(screen.x, screen.y);
                    blockCard.editor.pointerX = point.x;
                    blockCard.editor.pointerY = point.y;
                    blockCard.repaintRequested();
                }
            }
            onReleased: mouse => {
                const screen = mapToItem(blockCard.viewport, mouse.x, mouse.y);
                const point = blockCard.editor.worldPoint(screen.x, screen.y);
                blockCard.editor.finishConnectionAt(point.x, point.y);
            }
            onCanceled: blockCard.editor.cancelConnection()
        }
    }
    // 노드 종류에 맞는 기능 색상을 공통 테마에서 읽는 함수.
    function colorFor(type) {
        return theme.nodeKinds[type] || theme.nodeKinds.fallback;
    }
}
