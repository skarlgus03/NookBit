import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T

Item {
    id: page
    signal openCurrentEditor()
    signal editorDraftReplaced()

    property bool oldEditorFormDirty: false
    property int fontPresetIndex: 1
    readonly property int nodeFontSize: [11, 13, 16][fontPresetIndex]
    readonly property int blockWidth: [206, 226, 262][fontPresetIndex]
    readonly property int blockHeight: [102, 112, 132][fontPresetIndex]
    property bool darkMode: false
    readonly property color pageColor: darkMode ? "#161a22" : "#f5f5f2"
    readonly property color surfaceColor: darkMode ? "#202633" : "#ffffff"
    readonly property color canvasColor: darkMode ? "#191f2a" : "#f8faf7"
    readonly property color borderColor: darkMode ? "#3d4757" : "#d9dfd8"
    readonly property color primaryText: darkMode ? "#edf2f8" : "#27352b"
    readonly property color mutedText: darkMode ? "#aab6c7" : "#58655b"
    readonly property color gridColor: darkMode ? "#2a3241" : "#e8ede7"
    readonly property color nodeColor: darkMode ? "#252d3b" : "#ffffff"
    readonly property color buttonColor: darkMode ? "#303b4d" : "#f3f5f1"
    readonly property color buttonFocusColor: darkMode ? "#86aad6" : "#7da58a"
    palette.window: pageColor
    palette.windowText: primaryText
    palette.button: buttonColor
    palette.buttonText: primaryText
    palette.base: darkMode ? "#252d3b" : "#ffffff"
    palette.text: primaryText
    palette.highlight: darkMode ? "#55769f" : "#80a88a"
    palette.highlightedText: "#ffffff"

    // 마우스를 올려도 배경과 글자색은 유지하고 테두리만 강조하는 버튼.
    component ThemeButton: T.Button {
        id: themedButton
        padding: 8
        implicitWidth: Math.max(80, contentItem.implicitWidth + leftPadding + rightPadding)
        implicitHeight: Math.max(36, contentItem.implicitHeight + topPadding + bottomPadding)
        contentItem: Text {
            text: themedButton.text
            font: themedButton.font
            color: themedButton.enabled ? page.primaryText : page.mutedText
            opacity: themedButton.enabled ? 1 : 0.55
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: 7
            color: page.buttonColor
            border.color: themedButton.enabled &&
                          (themedButton.hovered || themedButton.visualFocus || themedButton.down)
                          ? page.buttonFocusColor : page.borderColor
            border.width: themedButton.enabled &&
                          (themedButton.hovered || themedButton.visualFocus || themedButton.down) ? 2 : 1
        }
    }
    property var blocks: [
        { id: 1, type: "start", x: 70, y: 270, value: "정리할 파일" },
        { id: 2, type: "size", x: 390, y: 220, value: "5", comparison: "greaterThan", unit: "MiB" },
        { id: 3, type: "extension", x: 720, y: 220, value: "png, jpg" },
        { id: 4, type: "destination", x: 1050, y: 220, value: "Pictures" }
    ]
    property var edges: [
        { from: 1, to: 2 }, { from: 2, to: 3 }, { from: 3, to: 4 }
    ]
    property var livePositions: ({})
    property int nextId: 5
    property bool graphDirty: false
    property bool stagedGraph: false
    property int selectedId: -1
    property var selectedIds: []
    property bool marqueeActive: false
    property real marqueeStartX: 0
    property real marqueeStartY: 0
    property real marqueeEndX: 0
    property real marqueeEndY: 0
    property int connectingFrom: -1
    property bool draggingConnection: false
    property real pointerX: 0
    property real pointerY: 0
    property real zoom: 1.0
    property real panX: 0
    property real panY: 0
    property int connectionStyle: 0
    readonly property color rootBorderColor: darkMode ? "#f5ca69" : "#a46b12"
    property bool altHeld: false
    property int hoveredEdgeIndex: -1
    property real hoverX: 0
    property real hoverY: 0
    property real addMenuWorldX: 0
    property real addMenuWorldY: 0
    property var copiedBlocks: []
    property var copiedEdges: []
    property int pasteCount: 0
    property string statusText: qsTr("예시 그래프입니다. 블록을 이동하거나 연결을 바꿔 보세요.")
    property var selectedBlock: selectedIds.length === 1 ? blockForId(selectedId) : null

    // 단일 선택을 만들고 상세 설정 대상을 맞추는 함수.
    function selectOnly(id) {
        selectedIds = id < 0 ? [] : [id]
        selectedId = id
    }

    // 파일 입력 바로 아래에 연결된 최상위 조건 블록인지 확인하는 함수.
    function isRootBlock(id) {
        return edges.some(edge => edge.from === 1 && edge.to === id)
    }

    // Shift+클릭으로 노드를 다중 선택에 더하거나 빼는 함수.
    function toggleSelection(id) {
        const next = selectedIds.slice()
        const index = next.indexOf(id)
        if (index < 0) next.push(id)
        else next.splice(index, 1)
        selectedIds = next
        selectedId = next.length === 1 ? next[0] : -1
    }

    // 선택 박스에 닿는 노드를 고르고 Shift를 누르면 기존 선택과 합치는 함수.
    function finishMarquee(addToSelection) {
        const left = Math.min(marqueeStartX, marqueeEndX)
        const right = Math.max(marqueeStartX, marqueeEndX)
        const top = Math.min(marqueeStartY, marqueeEndY)
        const bottom = Math.max(marqueeStartY, marqueeEndY)
        const hits = blocks.filter(block => {
            const pos = positionFor(block)
            const screen = screenPoint(pos.x, pos.y)
            return screen.x < right && screen.x + blockWidth * zoom > left &&
                   screen.y < bottom && screen.y + blockHeight * zoom > top
        }).map(block => block.id)
        const next = addToSelection ? selectedIds.concat(hits.filter(id => !selectedIds.includes(id))) : hits
        selectedIds = next
        selectedId = next.length === 1 ? next[0] : -1
        marqueeActive = false
    }

    // 현재 편집 초안의 규칙을 노드와 선으로 다시 펼치는 함수.
    function loadGraphFromDraft() {
        const restored = ruleEditor.graph()
        if (!restored.valid) {
            selectOnly(-1)
            marqueeActive = false
            blocks = [{ id: 1, type: "start", x: 70, y: 220, value: "정리할 파일" }]
            edges = []
            livePositions = ({})
            nextId = 2
            graphDirty = false
            stagedGraph = false
            statusText = restored.message || qsTr("규칙을 노드 화면에 불러오지 못했어요.")
            lineCanvas.requestPaint()
            return
        }
        selectOnly(-1)
        marqueeActive = false
        connectingFrom = -1
        draggingConnection = false
        livePositions = ({})
        blocks = restored.blocks
        edges = restored.edges
        nextId = restored.nextId
        graphDirty = false
        stagedGraph = false
        statusText = restored.incompleteLeaves > 0
                     ? qsTr("기존 규칙에 종착점 없는 가지가 %1개 있어요. 목적지나 정리 제외를 연결해 주세요.").arg(restored.incompleteLeaves)
                     : qsTr("저장된 규칙을 노드 화면에 불러왔어요.")
        lineCanvas.requestPaint()
        Qt.callLater(function() { page.fitAllNodes() })
    }

    Component.onCompleted: loadGraphFromDraft()
    onBlocksChanged: {
        graphDirty = true
        stagedGraph = false
    }
    onEdgesChanged: {
        graphDirty = true
        stagedGraph = false
    }

    Connections {
        target: ruleEditor
        // 규칙 저장 후에는 확정한 그래프의 배치를 남기고 새 저장본을 다시 펼치는 함수.
        function onRulesSaved() {
            if (page.stagedGraph) {
                const layoutSaved = ruleEditor.saveGraphLayout(page.blocks, page.edges)
                page.loadGraphFromDraft()
                page.statusText = layoutSaved
                    ? qsTr("규칙과 노드 위치를 모두 저장했어요.")
                    : qsTr("규칙은 저장됐지만 노드 위치는 저장하지 못했어요.")
            } else if (page.graphDirty) {
                page.statusText = qsTr("규칙 저장본이 바뀌었어요. 화면의 미저장 그래프는 유지했으니 필요하면 다시 불러오세요.")
            } else {
                page.loadGraphFromDraft()
            }
        }
    }

    // 그래프를 검증한 뒤 규칙 초안과 노드 위치를 한 번에 저장하는 함수.
    function saveAll() {
        if (!ruleEditor.importGraph(blocks, edges)) {
            statusText = ruleEditor.message
            return
        }
        editorDraftReplaced()
        stagedGraph = true
        if (!ruleEditor.save()) {
            stagedGraph = false
            statusText = ruleEditor.message
        }
    }

    // 노드 종류에 맞는 입력값과 비교 방식을 카드에 표시하는 함수.
    function displayValue(block) {
        if (block.type === "size" || block.type === "age") {
            const comparisons = {
                greaterThan: qsTr("초과"), atLeast: qsTr("이상"),
                lessThan: qsTr("미만"), atMost: qsTr("이하")
            }
            const unit = block.type === "size" ? (block.unit || "MiB") : qsTr("일")
            return block.value + " " + unit + " " + (comparisons[block.comparison] || "")
        }
        return block.value
    }

    // 숫자 조건의 현재 비교 방식 이름을 반환하는 함수.
    function comparisonTitle(block) {
        const names = {
            greaterThan: qsTr("초과"), atLeast: qsTr("이상"),
            lessThan: qsTr("미만"), atMost: qsTr("이하")
        }
        return names[block.comparison] || qsTr("선택 안 됨")
    }

    // ID로 화면의 블록 하나를 찾는 함수.
    function blockForId(id) {
        for (const block of blocks) {
            if (block.id === id)
                return block
        }
        return null
    }

    // 블록 입력점에 다른 블록의 선이 연결되었는지 반환하는 함수.
    function hasIncoming(id) {
        return edges.some(edge => edge.to === id)
    }

    // 블록 출력점에서 다른 블록으로 선이 이어지는지 반환하는 함수.
    function hasOutgoing(id) {
        return edges.some(edge => edge.from === id)
    }

    // 드래그 중인 블록은 아직 모델에 저장되지 않은 화면 위치를 반환하는 함수.
    function positionFor(block) {
        return livePositions[block.id] || block
    }

    // 화면 좌표를 경계 없는 작업 공간 좌표로 바꾸는 함수.
    function worldPoint(screenX, screenY) {
        return { x: (screenX - panX) / zoom, y: (screenY - panY) / zoom }
    }

    // 작업 공간 좌표를 화면 좌표로 바꾸는 함수.
    function screenPoint(worldX, worldY) {
        return { x: panX + worldX * zoom, y: panY + worldY * zoom }
    }

    // 휠 아래의 작업 공간 위치를 유지하며 확대하거나 축소하는 함수.
    function zoomAt(delta, viewportX, viewportY) {
        if (delta === 0)
            return
        const oldZoom = zoom
        const nextZoom = Math.max(0.05, Math.min(2.5, oldZoom * Math.pow(1.12, delta / 120)))
        if (nextZoom === oldZoom)
            return
        const worldX = (viewportX - panX) / oldZoom
        const worldY = (viewportY - panY) / oldZoom
        zoom = nextZoom
        panX = viewportX - worldX * nextZoom
        panY = viewportY - worldY * nextZoom
    }

    // 모든 노드가 보이도록 화면 위치와 확대율만 맞추는 함수.
    function fitAllNodes() {
        if (blocks.length === 0 || canvasViewport.width <= 0 || canvasViewport.height <= 0)
            return
        let left = Infinity, top = Infinity, right = -Infinity, bottom = -Infinity
        for (const block of blocks) {
            const pos = positionFor(block)
            left = Math.min(left, pos.x)
            top = Math.min(top, pos.y)
            right = Math.max(right, pos.x + blockWidth)
            bottom = Math.max(bottom, pos.y + blockHeight)
        }
        const margin = 80
        zoom = Math.max(0.05, Math.min(2.5,
            Math.min((canvasViewport.width - margin * 2) / Math.max(1, right - left),
                     (canvasViewport.height - margin * 2) / Math.max(1, bottom - top))))
        panX = (canvasViewport.width - (left + right) * zoom) / 2
        panY = (canvasViewport.height - (top + bottom) * zoom) / 2
        lineCanvas.requestPaint()
    }

    // 종류에 따라 블록 제목을 정하는 함수.
    function titleFor(type) {
        switch (type) {
        case "start": return qsTr("파일 입력")
        case "extension": return qsTr("확장자")
        case "size": return qsTr("파일 크기")
        case "name": return qsTr("이름 포함")
        case "age": return qsTr("수정 후 지난 일수")
        case "otherwise": return qsTr("그 외")
        case "destination": return qsTr("목적지")
        case "exclude": return qsTr("정리 제외")
        }
        return qsTr("조건")
    }

    // 종류에 따라 블록의 강조 색을 정하는 함수.
    function colorFor(type) {
        if (darkMode) {
            switch (type) {
            case "start": return "#24624d"
            case "extension": return "#286c9c"
            case "size": return "#986126"
            case "name": return "#7750a2"
            case "age": return "#237c83"
            case "otherwise": return "#596575"
            case "destination": return "#39734c"
            case "exclude": return "#944b68"
            }
            return "#53677e"
        }
        switch (type) {
        case "start": return "#35584a"
        case "extension": return "#3d7292"
        case "size": return "#99683f"
        case "name": return "#756397"
        case "age": return "#7b7650"
        case "otherwise": return "#69737b"
        case "destination": return "#38684c"
        case "exclude": return "#865b79"
        }
        return "#647069"
    }

    // 화면의 지정 위치에서 블록 추가 메뉴를 열고 삽입 좌표를 기억하는 함수.
    function openAddMenu(screenX, screenY) {
        const point = worldPoint(screenX, screenY)
        addMenuWorldX = point.x
        addMenuWorldY = point.y
        addNodeMenu.x = Math.max(0, Math.min(screenX, canvasViewport.width - addNodeMenu.width))
        addNodeMenu.y = Math.max(0, Math.min(screenY, canvasViewport.height - addNodeMenu.height))
        addNodeMenu.open()
    }

    // 우클릭 메뉴에서 고른 종류의 임시 블록을 기억한 위치에 추가하는 함수.
    function addBlock(type, worldX, worldY) {
        const examples = {
            extension: "png, jpg", size: "5", name: "report",
            age: "7", otherwise: "조건 없음", destination: "Pictures",
            exclude: "이 파일은 이동하지 않음"
        }
        const block = {
            id: nextId++, type: type,
            x: worldX,
            y: worldY,
            value: examples[type],
            comparison: type === "size" ? "greaterThan" : "atLeast",
            unit: "MiB"
        }
        blocks = blocks.concat([block])
        selectOnly(block.id)
        statusText = qsTr("블록을 추가했어요. 블록을 끌어 위치를 바꾸고, 오른쪽 점을 왼쪽 점에 연결해 보세요.")
        lineCanvas.requestPaint()
    }

    // 선택한 시작 노드 이외의 블록과 그 사이 연결선만 내부 복사본으로 보관하는 함수.
    function copySelectedBlocks() {
        const chosen = blocks.filter(block => selectedIds.includes(block.id) && block.type !== "start")
        if (chosen.length === 0) {
            statusText = qsTr("복사할 블록을 먼저 선택해 주세요. 파일 입력 블록은 복사할 수 없어요.")
            return
        }
        const ids = chosen.map(block => block.id)
        copiedBlocks = chosen.map(block => Object.assign({}, block))
        copiedEdges = edges.filter(edge => ids.includes(edge.from) && ids.includes(edge.to))
                           .map(edge => Object.assign({}, edge))
        pasteCount = 0
        statusText = qsTr("블록 %1개를 복사했어요. Ctrl+V로 화면 가운데에 붙여넣으세요.").arg(chosen.length)
    }

    // 내부 복사본의 노드 ID를 새로 만들고 연결 관계를 유지한 채 붙여넣는 함수.
    function pasteCopiedBlocks() {
        if (copiedBlocks.length === 0) {
            statusText = qsTr("붙여넣을 블록이 없어요. 먼저 Ctrl+C로 복사해 주세요.")
            return
        }
        const left = Math.min(...copiedBlocks.map(block => block.x))
        const top = Math.min(...copiedBlocks.map(block => block.y))
        const center = worldPoint(canvasViewport.width / 2, canvasViewport.height / 2)
        const shift = pasteCount * 32 / zoom
        const idMap = ({})
        const created = copiedBlocks.map(block => {
            const copy = Object.assign({}, block, {
                id: nextId++, x: center.x + block.x - left + shift,
                y: center.y + block.y - top + shift
            })
            delete copy.ruleId
            delete copy.parentRuleId
            idMap[block.id] = copy.id
            return copy
        })
        const copiedConnections = copiedEdges.map(edge => ({ from: idMap[edge.from],
                                                               to: idMap[edge.to] }))
        blocks = blocks.concat(created)
        edges = edges.concat(copiedConnections)
        selectedIds = created.map(block => block.id)
        selectedId = selectedIds.length === 1 ? selectedIds[0] : -1
        pasteCount += 1
        statusText = qsTr("블록 %1개를 붙여넣었어요. 분류 규칙에 적용하려면 연결과 값을 확인해 주세요.").arg(created.length)
        lineCanvas.requestPaint()
    }

    // 선택한 노드들의 드래그 위치를 한 번에 임시 그래프에 반영하는 함수.
    function commitDraggedBlocks() {
        const positions = livePositions
        blocks = blocks.map(block => positions[block.id]
                            ? Object.assign({}, block, {
                                x: Math.round(positions[block.id].x),
                                y: Math.round(positions[block.id].y)
                            }) : block)
        livePositions = ({})
        lineCanvas.requestPaint()
    }

    // 선택한 블록의 실제 조건 입력값을 그래프에서 바꾸는 함수.
    function updateValue(id, value) {
        blocks = blocks.map(block => block.id === id
                            ? Object.assign({}, block, { value: value }) : block)
        statusText = qsTr("입력값을 바꿨어요. 규칙 초안으로 변환해야 적용됩니다.")
    }

    // 숫자 조건의 비교 방식을 다음 항목으로 바꾸는 함수.
    function cycleComparison(id) {
        blocks = blocks.map(block => {
            if (block.id !== id) return block
            const choices = block.type === "size"
                            ? ["greaterThan", "atLeast", "lessThan", "atMost"]
                            : ["atLeast", "lessThan"]
            const index = choices.indexOf(block.comparison)
            return Object.assign({}, block, { comparison: choices[(index + 1) % choices.length] })
        })
    }

    // 크기 조건의 바이트·MiB 단위를 바꾸는 함수.
    function toggleUnit(id) {
        blocks = blocks.map(block => block.id === id
                            ? Object.assign({}, block, { unit: block.unit === "MiB" ? "B" : "MiB" })
                            : block)
    }

    // 선택한 조건 또는 그 외 가지의 사용 여부를 바꾸는 함수.
    function toggleEnabled(id) {
        blocks = blocks.map(block => block.id === id
                            ? Object.assign({}, block, { enabled: block.enabled === false })
                            : block)
    }

    // 같은 부모 아래 일반 조건의 검사 순서를 반환하는 함수.
    function orderFor(id) {
        const incoming = edges.find(edge => edge.to === id)
        if (!incoming) return -1
        const block = blockForId(id)
        if (!block || block.type === "destination" || block.type === "exclude" ||
            block.type === "otherwise") return -1
        let order = 0
        for (const edge of edges) {
            if (edge.from !== incoming.from) continue
            const sibling = blockForId(edge.to)
            if (sibling && sibling.type !== "otherwise") ++order
            if (edge.to === id) return order
        }
        return -1
    }

    // 선택한 조건을 같은 부모의 앞뒤 조건과 교환하는 함수.
    function moveSelectedOrder(direction) {
        const incoming = edges.find(edge => edge.to === selectedId)
        if (!incoming || orderFor(selectedId) < 0) return
        const siblingEdges = edges.filter(edge => edge.from === incoming.from &&
                                                  blockForId(edge.to).type !== "otherwise")
        const index = siblingEdges.findIndex(edge => edge.to === selectedId)
        const target = siblingEdges[index + direction]
        if (!target) return
        const next = edges.slice()
        const first = next.findIndex(edge => edge.from === incoming.from && edge.to === selectedId)
        const second = next.findIndex(edge => edge.from === incoming.from && edge.to === target.to)
        const saved = next[first]
        next[first] = next[second]
        next[second] = saved
        edges = next
        statusText = qsTr("같은 부모 아래 조건의 검사 순서를 바꿨어요.")
        lineCanvas.requestPaint()
    }

    // 이미 연결된 경로를 따라 목적지 ID에 도달하는지 검사하는 함수.
    function hasPath(fromId, targetId) {
        if (fromId === targetId)
            return true
        for (const edge of edges) {
            if (edge.from === fromId && hasPath(edge.to, targetId))
                return true
        }
        return false
    }

    // 한 부모와 한 자식 사이에 순환 없는 임시 연결을 만드는 함수.
    function connectBlocks(fromId, toId) {
        const source = blockForId(fromId)
        const target = blockForId(toId)
        if (!source || !target || source.type === "destination" ||
            source.type === "exclude" || target.type === "start")
            return false
        if (fromId === toId || hasPath(toId, fromId)) {
            statusText = qsTr("자기 자신이나 조상 블록으로 연결할 수 없어요.")
            return false
        }
        for (const edge of edges) {
            if (edge.to === toId) {
                statusText = qsTr("입력 하나에는 부모 블록 하나만 연결할 수 있어요.")
                return false
            }
        }
        const existing = edges.filter(edge => edge.from === fromId)
        const terminal = target.type === "destination" || target.type === "exclude"
        if (source.type === "start" && terminal) {
            statusText = qsTr("파일 입력 뒤에는 조건 또는 '그 외' 노드를 먼저 연결해 주세요.")
            return false
        }
        if ((terminal && existing.length > 0) ||
            (!terminal && existing.some(edge => {
                const child = blockForId(edge.to)
                return child.type === "destination" || child.type === "exclude"
            }))) {
            statusText = qsTr("목적지·정리 제외 노드는 다른 자식과 함께 연결할 수 없어요.")
            return false
        }
        if (target.type === "otherwise" && existing.some(edge => blockForId(edge.to).type === "otherwise")) {
            statusText = qsTr("같은 부모에는 '그 외'를 하나만 연결할 수 있어요.")
            return false
        }
        const next = edges.slice()
        const otherwiseIndex = next.findIndex(edge => edge.from === fromId &&
                                                      blockForId(edge.to).type === "otherwise")
        if (target.type !== "otherwise" && otherwiseIndex >= 0)
            next.splice(otherwiseIndex, 0, { from: fromId, to: toId })
        else
            next.push({ from: fromId, to: toId })
        edges = next
        connectingFrom = -1
        draggingConnection = false
        statusText = qsTr("연결했어요. 이 그래프는 아직 분류 규칙으로 저장되지 않습니다.")
        lineCanvas.requestPaint()
        return true
    }

    // 출력점에서 놓은 위치에 입력점이 있으면 두 블록을 연결하는 함수.
    function finishConnectionAt(x, y) {
        if (connectingFrom < 0)
            return
        const sourceId = connectingFrom
        connectingFrom = -1
        draggingConnection = false
        lineCanvas.requestPaint()
        for (const block of blocks) {
            if (block.type !== "start" && Math.abs(x - block.x) < 28 &&
                Math.abs(y - (block.y + blockHeight / 2)) < 28) {
                connectBlocks(sourceId, block.id)
                return
            }
        }
        statusText = qsTr("연결점 밖에 놓아 선 연결을 취소했어요.")
        lineCanvas.requestPaint()
    }

    // 선택한 블록들과 이어진 선을 임시 그래프에서 함께 제거하는 함수.
    function removeSelectedBlock() {
        const removed = selectedIds.filter(id => id !== 1)
        if (removed.length === 0)
            return
        blocks = blocks.filter(block => !removed.includes(block.id))
        edges = edges.filter(edge => !removed.includes(edge.from) && !removed.includes(edge.to))
        selectOnly(-1)
        if (removed.includes(connectingFrom))
            connectingFrom = -1
        draggingConnection = false
        const next = Object.assign({}, livePositions)
        for (const id of removed) delete next[id]
        livePositions = next
        statusText = qsTr("블록과 연결선을 화면에서 제거했어요.")
        lineCanvas.requestPaint()
    }

    // 예시 블록과 연결을 다시 만드는 함수.
    function resetExample() {
        blocks = [
            { id: 1, type: "start", x: 70, y: 270, value: "정리할 파일" },
            { id: 2, type: "size", x: 390, y: 220, value: "5", comparison: "greaterThan", unit: "MiB" },
            { id: 3, type: "extension", x: 720, y: 220, value: "png, jpg" },
            { id: 4, type: "destination", x: 1050, y: 220, value: "Pictures" }
        ]
        edges = [{ from: 1, to: 2 }, { from: 2, to: 3 }, { from: 3, to: 4 }]
        nextId = 5
        livePositions = ({})
        selectOnly(-1)
        connectingFrom = -1
        draggingConnection = false
        zoom = 1.0
        panX = 0
        panY = 0
        statusText = qsTr("예시 그래프를 다시 불러왔어요.")
        lineCanvas.requestPaint()
    }

    // 화면의 한 점에서 선분까지의 거리를 구하는 함수.
    function distanceToSegment(px, py, x1, y1, x2, y2) {
        const dx = x2 - x1
        const dy = y2 - y1
        const lengthSquared = dx * dx + dy * dy
        const fraction = lengthSquared > 0
            ? Math.max(0, Math.min(1, ((px - x1) * dx + (py - y1) * dy) / lengthSquared)) : 0
        return Math.hypot(px - (x1 + fraction * dx), py - (y1 + fraction * dy))
    }

    // 커서에 가장 가까운 연결선을 찾아 Alt 강조 대상으로 정하는 함수.
    function edgeAt(screenX, screenY) {
        let nearest = -1
        let distance = 12
        for (let index = 0; index < edges.length; ++index) {
            const edge = edges[index]
            const source = blockForId(edge.from)
            const target = blockForId(edge.to)
            if (!source || !target) continue
            const a = positionFor(source)
            const b = positionFor(target)
            const from = screenPoint(a.x + blockWidth, a.y + blockHeight / 2)
            const to = screenPoint(b.x, b.y + blockHeight / 2)
            let current = Infinity
            if (connectionStyle === 1) {
                current = distanceToSegment(screenX, screenY, from.x, from.y, to.x, to.y)
            } else {
                const bend = Math.max(65, Math.abs(to.x - from.x) * 0.45)
                let previousX = from.x
                let previousY = from.y
                for (let step = 1; step <= 24; ++step) {
                    const t = step / 24
                    const inverse = 1 - t
                    const x = inverse * inverse * inverse * from.x +
                        3 * inverse * inverse * t * (from.x + bend) +
                        3 * inverse * t * t * (to.x - bend) + t * t * t * to.x
                    const y = inverse * inverse * inverse * from.y +
                        3 * inverse * inverse * t * from.y +
                        3 * inverse * t * t * to.y + t * t * t * to.y
                    current = Math.min(current, distanceToSegment(
                        screenX, screenY, previousX, previousY, x, y))
                    previousX = x
                    previousY = y
                }
            }
            if (current < distance) {
                nearest = index
                distance = current
            }
        }
        return nearest
    }

    // Alt를 누르는 동안 커서 아래 선만 강조하도록 갱신하는 함수.
    function updateHoveredEdge(screenX, screenY) {
        hoverX = screenX
        hoverY = screenY
        const next = altHeld ? edgeAt(screenX, screenY) : -1
        if (next !== hoveredEdgeIndex) {
            hoveredEdgeIndex = next
            lineCanvas.requestPaint()
        }
    }

    // 강조된 연결선 하나만 그래프에서 제거하는 함수.
    function disconnectHoveredEdge() {
        if (hoveredEdgeIndex < 0 || hoveredEdgeIndex >= edges.length) return false
        edges = edges.filter((edge, index) => index !== hoveredEdgeIndex)
        hoveredEdgeIndex = -1
        statusText = qsTr("선 하나를 끊었어요. 연결을 다시 잇거나 저장본을 다시 열 수 있어요.")
        lineCanvas.requestPaint()
        return true
    }

    // 선택한 직선·곡선 방식으로 연결선과 드래그 중인 선을 그리는 함수.
    function drawEdge(context, x1, y1, x2, y2, temporary, highlighted) {
        const bend = Math.max(65, Math.abs(x2 - x1) * 0.45)
        context.beginPath()
        context.moveTo(x1, y1)
        if (connectionStyle === 0)
            context.bezierCurveTo(x1 + bend, y1, x2 - bend, y2, x2, y2)
        else
            context.lineTo(x2, y2)
        context.lineWidth = highlighted ? 8 : Math.max(1, (temporary ? 4 : 4.5) * zoom)
        context.lineCap = "round"
        context.strokeStyle = highlighted ? "#f5c542"
            : temporary ? (darkMode ? "#b2c9e6" : "#718e78")
                        : (darkMode ? "#86aad6" : "#4e765d")
        context.stroke()
    }

    onSelectedIdChanged: {
        const block = blockForId(selectedId)
        inspectorValue.text = block ? block.value : ""
    }
    onDarkModeChanged: lineCanvas.requestPaint()
    onAltHeldChanged: updateHoveredEdge(hoverX, hoverY)
    onPanXChanged: lineCanvas.requestPaint()
    onPanYChanged: lineCanvas.requestPaint()
    onZoomChanged: lineCanvas.requestPaint()
    onFontPresetIndexChanged: lineCanvas.requestPaint()

    Rectangle {
        anchors.fill: parent
        color: page.pageColor
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: qsTr("분류 규칙")
                font.pixelSize: 29
                font.bold: true
                color: page.primaryText
            }
            Item { Layout.fillWidth: true }
            ThemeButton {
                text: page.darkMode ? qsTr("밝은 화면") : qsTr("어두운 화면")
                onClicked: page.darkMode = !page.darkMode
            }
            ThemeButton {
                text: qsTr("전체 저장")
                font.bold: true
                onClicked: {
                    if (ruleEditor.dirty || page.oldEditorFormDirty) confirmSave.open()
                    else page.saveAll()
                }
            }
            ThemeButton {
                text: qsTr("저장본 다시 열기")
                onClicked: confirmReload.open()
            }
            ThemeButton {
                text: qsTr("기존 편집기")
                onClicked: page.openCurrentEditor()
            }
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("캔버스에서 우클릭해 블록을 추가하고, 오른쪽 점에서 다음 블록의 왼쪽 점으로 선을 연결하세요.")
            color: page.mutedText
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            radius: 10
            color: page.darkMode ? "#3b3020" : "#fff4dd"
            border.color: page.darkMode ? "#806240" : "#dfc591"
            Label {
                anchors.fill: parent
                anchors.margins: 12
                verticalAlignment: Text.AlignVCenter
                wrapMode: Text.WordWrap
                text: qsTr("규칙은 저장 전에 초안으로 검증합니다. 노드 위치는 분류 규칙과 별도로 저장돼 검사 순서를 바꾸지 않아요.")
                color: page.darkMode ? "#f3d59d" : "#6a4a19"
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            ThemeButton {
                text: qsTr("+ 블록 추가")
                onClicked: page.openAddMenu(canvasViewport.width / 2, canvasViewport.height / 2)
            }
            Label { text: qsTr("연결선 모양"); color: page.primaryText }
            ThemeButton {
                text: qsTr("곡선")
                font.bold: page.connectionStyle === 0
                onClicked: {
                    page.connectionStyle = 0
                    lineCanvas.requestPaint()
                }
            }
            ThemeButton {
                text: qsTr("직선")
                font.bold: page.connectionStyle === 1
                onClicked: {
                    page.connectionStyle = 1
                    lineCanvas.requestPaint()
                }
            }
            ThemeButton {
                text: qsTr("노드 전체 보기")
                onClicked: page.fitAllNodes()
            }
            ThemeButton {
                text: qsTr("글씨: %1").arg([qsTr("작게"), qsTr("보통"), qsTr("크게")][page.fontPresetIndex])
                onClicked: page.fontPresetIndex = (page.fontPresetIndex + 1) % 3
            }
            Item { Layout.fillWidth: true }
            Label {
                text: qsTr("%1%").arg(Math.round(page.zoom * 100))
                color: page.mutedText
            }
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("우클릭: 블록 추가 · 우드래그: 화면 이동 · 좌드래그: 선택 · Shift+클릭: 추가 선택 · Alt+선 클릭: 연결 해제 · Delete: 삭제 · Ctrl+C/V: 복사/붙여넣기")
            color: page.mutedText
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 10
                color: page.canvasColor
                border.color: page.borderColor

                Item {
                    id: canvasViewport
                    anchors.fill: parent
                    anchors.margins: 1
                    clip: true
                    focus: true
                    Keys.onPressed: (event) => {
                        if (event.key === Qt.Key_Alt) {
                            page.altHeld = true
                            event.accepted = true
                        } else if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
                            page.removeSelectedBlock()
                            event.accepted = true
                        } else if (event.modifiers & Qt.ControlModifier) {
                            if (event.key === Qt.Key_C) {
                                page.copySelectedBlocks()
                                event.accepted = true
                            } else if (event.key === Qt.Key_V) {
                                page.pasteCopiedBlocks()
                                event.accepted = true
                            }
                        }
                    }
                    Keys.onReleased: (event) => {
                        if (event.key === Qt.Key_Alt) {
                            page.altHeld = false
                            event.accepted = true
                        }
                    }
                    onActiveFocusChanged: {
                        if (!activeFocus) page.altHeld = false
                    }
                    onWidthChanged: lineCanvas.requestPaint()
                    onHeightChanged: lineCanvas.requestPaint()

                    WheelHandler {
                        target: null
                        blocking: true
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                        onWheel: (event) => page.zoomAt(event.angleDelta.y,
                                                         point.position.x, point.position.y)
                    }

                        Canvas {
                            id: lineCanvas
                            anchors.fill: parent
                            antialiasing: true
                            onPaint: {
                                const context = getContext("2d")
                                context.clearRect(0, 0, width, height)
                                context.fillStyle = page.canvasColor
                                context.fillRect(0, 0, width, height)
                                context.strokeStyle = page.gridColor
                                context.lineWidth = 1
                                let gridStep = 40 * page.zoom
                                while (gridStep < 24) gridStep *= 2
                                while (gridStep > 96) gridStep /= 2
                                const gridX = ((page.panX % gridStep) + gridStep) % gridStep
                                const gridY = ((page.panY % gridStep) + gridStep) % gridStep
                                for (let x = gridX; x <= width; x += gridStep) {
                                    context.beginPath()
                                    context.moveTo(x, 0)
                                    context.lineTo(x, height)
                                    context.stroke()
                                }
                                for (let y = gridY; y <= height; y += gridStep) {
                                    context.beginPath()
                                    context.moveTo(0, y)
                                    context.lineTo(width, y)
                                    context.stroke()
                                }
                                for (let index = 0; index < page.edges.length; ++index) {
                                    const edge = page.edges[index]
                                    const source = page.blockForId(edge.from)
                                    const target = page.blockForId(edge.to)
                                    if (source && target) {
                                        const sourcePos = page.positionFor(source)
                                        const targetPos = page.positionFor(target)
                                        const from = page.screenPoint(sourcePos.x + page.blockWidth,
                                                                      sourcePos.y + page.blockHeight / 2)
                                        const to = page.screenPoint(targetPos.x,
                                                                    targetPos.y + page.blockHeight / 2)
                                        page.drawEdge(context, from.x, from.y, to.x, to.y,
                                                      false, page.altHeld && index === page.hoveredEdgeIndex)
                                    }
                                }
                                if (page.connectingFrom >= 0 && page.draggingConnection) {
                                    const source = page.blockForId(page.connectingFrom)
                                    if (source) {
                                        const sourcePos = page.positionFor(source)
                                        const from = page.screenPoint(sourcePos.x + page.blockWidth,
                                                                      sourcePos.y + page.blockHeight / 2)
                                        const to = page.screenPoint(page.pointerX, page.pointerY)
                                        page.drawEdge(context, from.x, from.y, to.x, to.y, true, false)
                                    }
                                }
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton
                            preventStealing: true
                            property bool breakingEdge: false
                            onPressed: (mouse) => {
                                canvasViewport.forceActiveFocus()
                                if (mouse.modifiers & Qt.AltModifier)
                                    page.hoveredEdgeIndex = page.edgeAt(mouse.x, mouse.y)
                                breakingEdge = (mouse.modifiers & Qt.AltModifier) &&
                                    page.disconnectHoveredEdge()
                                if (breakingEdge) return
                                page.marqueeStartX = mouse.x
                                page.marqueeStartY = mouse.y
                                page.marqueeEndX = mouse.x
                                page.marqueeEndY = mouse.y
                                page.marqueeActive = true
                            }
                            onPositionChanged: (mouse) => {
                                if (!pressed || breakingEdge) return
                                page.marqueeEndX = mouse.x
                                page.marqueeEndY = mouse.y
                            }
                            onReleased: (mouse) => {
                                if (breakingEdge) {
                                    breakingEdge = false
                                    return
                                }
                                page.marqueeEndX = mouse.x
                                page.marqueeEndY = mouse.y
                                if (Math.abs(mouse.x - page.marqueeStartX) < 4 &&
                                    Math.abs(mouse.y - page.marqueeStartY) < 4) {
                                    if (!(mouse.modifiers & Qt.ShiftModifier)) page.selectOnly(-1)
                                    page.marqueeActive = false
                                } else {
                                    page.finishMarquee((mouse.modifiers & Qt.ShiftModifier) !== 0)
                                }
                            }
                            onCanceled: {
                                breakingEdge = false
                                page.marqueeActive = false
                            }
                        }

                        Repeater {
                            model: page.blocks
                            delegate: Rectangle {
                                id: blockCard
                                required property var modelData
                                x: page.screenPoint(page.positionFor(modelData).x,
                                                    page.positionFor(modelData).y).x
                                y: page.screenPoint(page.positionFor(modelData).x,
                                                    page.positionFor(modelData).y).y
                                width: page.blockWidth
                                height: page.blockHeight
                                scale: page.zoom
                                transformOrigin: Item.TopLeft
                                radius: modelData.type === "start" ? height / 2 : 12
                                color: modelData.type === "start"
                                       ? (page.darkMode ? "#214b45" : "#e1f2e8") : page.nodeColor
                                opacity: modelData.enabled === false ? 0.55 : 1.0
                                border.width: modelData.type === "start"
                                    ? (page.selectedIds.includes(modelData.id) ? 5 : 3)
                                    : page.isRootBlock(modelData.id) ? 4
                                    : page.selectedIds.includes(modelData.id) ? 3 : 1
                                border.color: modelData.type === "start" ? "#55b89a"
                                    : page.isRootBlock(modelData.id) ? page.rootBorderColor
                                    : page.selectedIds.includes(modelData.id)
                                      ? page.colorFor(modelData.type) : page.borderColor

                                // 유일한 파일 입력은 조건 카드와 구별되는 캡슐형 시작점으로 표시한다.
                                Row {
                                    visible: modelData.type === "start"
                                    anchors.left: parent.left
                                    anchors.leftMargin: 18
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 11
                                    Rectangle {
                                        width: 42
                                        height: 42
                                        radius: 21
                                        color: page.darkMode ? "#55b89a" : "#317b61"
                                        anchors.verticalCenter: parent.verticalCenter
                                        Label {
                                            anchors.centerIn: parent
                                            text: "▶"
                                            font.pixelSize: page.nodeFontSize + 5
                                            color: "#ffffff"
                                        }
                                    }
                                    Column {
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: 4
                                        Label {
                                            text: qsTr("파일 입력")
                                            font.bold: true
                                            font.pixelSize: page.nodeFontSize + 3
                                            color: page.darkMode ? "#f2fff9" : "#21523f"
                                        }
                                        Label {
                                            text: qsTr("모든 분류의 시작점")
                                            font.pixelSize: Math.max(10, page.nodeFontSize - 2)
                                            color: page.darkMode ? "#b7dece" : "#477663"
                                        }
                                    }
                                }
                                Rectangle {
                                    id: cardHeader
                                    visible: modelData.type !== "start"
                                    x: 1; y: 1
                                    width: parent.width - 2
                                    height: Math.round(page.blockHeight * 0.33)
                                    radius: 11
                                    color: page.colorFor(modelData.type)
                                    Label {
                                        anchors.left: parent.left
                                        anchors.leftMargin: page.orderFor(modelData.id) > 0 ? 42 : 12
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: page.titleFor(modelData.type)
                                        font.bold: true
                                        font.pixelSize: page.nodeFontSize + 1
                                        color: "#ffffff"
                                    }
                                    Rectangle {
                                        visible: page.orderFor(modelData.id) > 0
                                        x: 9; y: 6
                                        width: 25; height: 25
                                        radius: 7
                                        color: "#ffffff"
                                        opacity: 0.94
                                        Label {
                                            anchors.centerIn: parent
                                            text: page.orderFor(modelData.id)
                                            font.bold: true
                                            font.pixelSize: page.nodeFontSize
                                            color: page.colorFor(modelData.type)
                                        }
                                    }
                                }
                                Label {
                                    visible: modelData.type !== "start"
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: cardHeader.bottom
                                    anchors.margins: 12
                                    text: page.displayValue(modelData)
                                    elide: Text.ElideRight
                                    font.pixelSize: page.nodeFontSize
                                    color: page.primaryText
                                }
                                Label {
                                    visible: modelData.type !== "start"
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    anchors.margins: 10
                                    text: modelData.type === "destination" ? qsTr("도착")
                                          : modelData.type === "exclude" ? qsTr("이동 안 함") : qsTr("일치 →")
                                    font.pixelSize: Math.max(10, page.nodeFontSize - 2)
                                    color: page.mutedText
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
                                    onPressed: (mouse) => {
                                        canvasViewport.forceActiveFocus()
                                        moved = false
                                        dragAllowed = !(mouse.modifiers & Qt.ShiftModifier)
                                        if (!dragAllowed) {
                                            page.toggleSelection(modelData.id)
                                            return
                                        }
                                        if (!page.selectedIds.includes(modelData.id))
                                            page.selectOnly(modelData.id)
                                        const screen = mapToItem(canvasViewport, mouse.x, mouse.y)
                                        const point = page.worldPoint(screen.x, screen.y)
                                        pressWorldX = point.x
                                        pressWorldY = point.y
                                        const starts = ({})
                                        for (const id of page.selectedIds) {
                                            const block = page.blockForId(id)
                                            if (block) starts[id] = { x: block.x, y: block.y }
                                        }
                                        startPositions = starts
                                    }
                                    onPositionChanged: (mouse) => {
                                        if (!pressed || !dragAllowed) return
                                        const screen = mapToItem(canvasViewport, mouse.x, mouse.y)
                                        const point = page.worldPoint(screen.x, screen.y)
                                        const dx = point.x - pressWorldX
                                        const dy = point.y - pressWorldY
                                        if (!moved && Math.abs(dx) < 3 && Math.abs(dy) < 3) return
                                        moved = true
                                        const next = ({})
                                        for (const id of page.selectedIds) {
                                            const start = startPositions[id]
                                            if (!start) continue
                                            next[id] = { x: start.x + dx, y: start.y + dy }
                                        }
                                        page.livePositions = next
                                        lineCanvas.requestPaint()
                                    }
                                    onReleased: {
                                        if (moved) page.commitDraggedBlocks()
                                        else if (dragAllowed && page.selectedIds.length > 1)
                                            page.selectOnly(modelData.id)
                                        dragAllowed = false
                                    }
                                    onCanceled: {
                                        page.livePositions = ({})
                                        lineCanvas.requestPaint()
                                        dragAllowed = false
                                    }
                                }
                                Rectangle {
                                    id: inputPort
                                    visible: modelData.type !== "start"
                                    x: -8
                                    y: parent.height / 2 - 8
                                    width: 16; height: 16
                                    radius: 8
                                    color: page.hasIncoming(modelData.id)
                                           ? page.colorFor(modelData.type) : page.nodeColor
                                    border.width: 3
                                    border.color: page.colorFor(modelData.type)
                                    MouseArea {
                                        anchors.fill: parent
                                        anchors.margins: -12
                                        onClicked: {
                                            if (page.connectingFrom >= 0)
                                                page.connectBlocks(page.connectingFrom, modelData.id)
                                            else
                                                page.selectOnly(modelData.id)
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
                                    color: page.hasOutgoing(modelData.id)
                                           ? page.colorFor(modelData.type) : page.nodeColor
                                    border.width: modelData.type === "start" ? 3 : 2
                                    border.color: page.colorFor(modelData.type)
                                    MouseArea {
                                        anchors.fill: parent
                                        anchors.margins: -12
                                        preventStealing: true
                                        onPressed: (mouse) => {
                                            page.connectingFrom = modelData.id
                                            page.draggingConnection = true
                                            const screen = mapToItem(canvasViewport, mouse.x, mouse.y)
                                            const point = page.worldPoint(screen.x, screen.y)
                                            page.pointerX = point.x
                                            page.pointerY = point.y
                                            lineCanvas.requestPaint()
                                        }
                                        onPositionChanged: (mouse) => {
                                            if (page.connectingFrom === modelData.id && page.draggingConnection) {
                                                const screen = mapToItem(canvasViewport, mouse.x, mouse.y)
                                                const point = page.worldPoint(screen.x, screen.y)
                                                page.pointerX = point.x
                                                page.pointerY = point.y
                                                lineCanvas.requestPaint()
                                            }
                                        }
                                        onReleased: (mouse) => {
                                            const screen = mapToItem(canvasViewport, mouse.x, mouse.y)
                                            const point = page.worldPoint(screen.x, screen.y)
                                            page.finishConnectionAt(point.x, point.y)
                                        }
                                    }
                                }
                            }
                        }

                        Rectangle {
                            visible: page.marqueeActive
                            x: Math.min(page.marqueeStartX, page.marqueeEndX)
                            y: Math.min(page.marqueeStartY, page.marqueeEndY)
                            width: Math.abs(page.marqueeEndX - page.marqueeStartX)
                            height: Math.abs(page.marqueeEndY - page.marqueeStartY)
                            color: page.darkMode ? "#335b85b8" : "#3380a88a"
                            border.color: page.buttonFocusColor
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
                    onPressed: (mouse) => {
                        canvasViewport.forceActiveFocus()
                        pressX = lastX = mouse.x
                        pressY = lastY = mouse.y
                        dragged = false
                    }
                    onPositionChanged: (mouse) => {
                        if (!pressed) return
                        if (!dragged && Math.abs(mouse.x - pressX) < 5 &&
                            Math.abs(mouse.y - pressY) < 5) return
                        dragged = true
                        page.panX += mouse.x - lastX
                        page.panY += mouse.y - lastY
                        lastX = mouse.x
                        lastY = mouse.y
                    }
                    onReleased: (mouse) => {
                        if (!dragged) {
                            const point = mapToItem(canvasViewport, mouse.x, mouse.y)
                            page.openAddMenu(point.x, point.y)
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    visible: page.altHeld
                    acceptedButtons: Qt.NoButton
                    hoverEnabled: true
                    onEntered: {
                        const point = mapToItem(canvasViewport, mouseX, mouseY)
                        page.updateHoveredEdge(point.x, point.y)
                    }
                    onPositionChanged: (mouse) => {
                        const point = mapToItem(canvasViewport, mouse.x, mouse.y)
                        page.updateHoveredEdge(point.x, point.y)
                    }
                    onExited: {
                        page.hoveredEdgeIndex = -1
                        lineCanvas.requestPaint()
                    }
                }
            }

            Frame {
                Layout.preferredWidth: 238
                Layout.fillHeight: true
                padding: 16
                background: Rectangle { radius: 10; color: page.surfaceColor; border.color: page.borderColor }
                ScrollView {
                    id: inspectorScroll
                    anchors.fill: parent
                    clip: true
                    contentWidth: availableWidth
                    contentHeight: inspectorContent.implicitHeight
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    ColumnLayout {
                    id: inspectorContent
                    width: inspectorScroll.availableWidth
                    spacing: 10
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("블록 설정")
                        font.pixelSize: 18
                        font.bold: true
                        color: page.primaryText
                    }
                    Label {
                        Layout.fillWidth: true
                        text: page.selectedIds.length > 1
                              ? qsTr("%1개 블록 선택됨 · 함께 이동하거나 Delete로 제거할 수 있어요.").arg(page.selectedIds.length)
                              : page.selectedBlock ? page.titleFor(page.selectedBlock.type)
                                                   : qsTr("블록을 선택하세요")
                        wrapMode: Text.WordWrap
                        color: page.mutedText
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: page.selectedBlock !== null && page.selectedBlock.type === "start"
                        text: qsTr("파일 입력은 하나만 존재하는 시작점이에요. 위치를 옮기거나 오른쪽 연결점에서 조건을 이어 갈 수 있어요.")
                        wrapMode: Text.WordWrap
                        color: page.mutedText
                    }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("표시 값")
                        visible: page.selectedBlock !== null &&
                                 page.selectedBlock.type !== "start" &&
                                 page.selectedBlock.type !== "otherwise" &&
                                 page.selectedBlock.type !== "exclude"
                    }
                    TextField {
                        id: inspectorValue
                        Layout.fillWidth: true
                        visible: page.selectedBlock !== null &&
                                 page.selectedBlock.type !== "start" &&
                                 page.selectedBlock.type !== "otherwise" &&
                                 page.selectedBlock.type !== "exclude"
                        enabled: visible
                        placeholderText: qsTr("예시 값")
                        color: page.primaryText
                        placeholderTextColor: page.mutedText
                        background: Rectangle {
                            radius: 6
                            color: page.nodeColor
                            border.color: page.borderColor
                        }
                        onEditingFinished: {
                            if (page.selectedBlock)
                                page.updateValue(page.selectedBlock.id, text)
                        }
                    }
                    ThemeButton {
                        Layout.fillWidth: true
                        visible: page.selectedBlock !== null &&
                                 (page.selectedBlock.type === "size" || page.selectedBlock.type === "age")
                        text: page.selectedBlock ? qsTr("비교: %1").arg(page.comparisonTitle(page.selectedBlock)) : ""
                        onClicked: page.cycleComparison(page.selectedId)
                    }
                    ThemeButton {
                        Layout.fillWidth: true
                        visible: page.selectedBlock !== null && page.selectedBlock.type === "size"
                        text: page.selectedBlock ? qsTr("단위: %1").arg(page.selectedBlock.unit || "MiB") : ""
                        onClicked: page.toggleUnit(page.selectedId)
                    }
                    ThemeButton {
                        Layout.fillWidth: true
                        visible: page.selectedBlock !== null &&
                                 page.selectedBlock.type !== "start" &&
                                 page.selectedBlock.type !== "destination" &&
                                 page.selectedBlock.type !== "exclude"
                        text: page.selectedBlock && page.selectedBlock.enabled === false
                              ? qsTr("규칙 사용하기") : qsTr("규칙 끄기")
                        onClicked: page.toggleEnabled(page.selectedId)
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: page.selectedBlock !== null && page.selectedBlock.type === "exclude"
                        text: qsTr("이 가지에 도달한 파일은 이동하지 않고, 전체 예외도 적용하지 않아요.")
                        wrapMode: Text.WordWrap
                        color: page.mutedText
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: page.orderFor(page.selectedId) > 0
                        text: qsTr("같은 부모에서 검사 순서: %1").arg(page.orderFor(page.selectedId))
                        color: page.mutedText
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: page.orderFor(page.selectedId) > 0
                        ThemeButton {
                            Layout.fillWidth: true
                            text: qsTr("순서 앞으로")
                            onClicked: page.moveSelectedOrder(-1)
                        }
                        ThemeButton {
                            Layout.fillWidth: true
                            text: qsTr("순서 뒤로")
                            onClicked: page.moveSelectedOrder(1)
                        }
                    }
                    }
                }
            }
        }

        Label {
            Layout.fillWidth: true
            text: page.statusText
            color: page.darkMode ? "#a9c5e9" : "#42634b"
            elide: Text.ElideRight
        }
    }

    Popup {
        id: addNodeMenu
        parent: canvasViewport
        width: 208
        padding: 8
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            radius: 9
            color: page.surfaceColor
            border.color: page.borderColor
        }
        contentItem: Column {
            spacing: 4
            Label {
                width: parent.width
                leftPadding: 8
                bottomPadding: 5
                text: qsTr("블록 추가")
                font.bold: true
                color: page.primaryText
            }
            Repeater {
                model: [
                    { type: "extension", title: qsTr("확장자") },
                    { type: "size", title: qsTr("크기") },
                    { type: "name", title: qsTr("이름 포함") },
                    { type: "age", title: qsTr("수정 경과일") },
                    { type: "otherwise", title: qsTr("그 외") },
                    { type: "destination", title: qsTr("목적지") },
                    { type: "exclude", title: qsTr("정리 제외") }
                ]
                delegate: ThemeButton {
                    required property var modelData
                    width: addNodeMenu.width - addNodeMenu.leftPadding - addNodeMenu.rightPadding
                    text: "+ " + modelData.title
                    onClicked: {
                        page.addBlock(modelData.type, page.addMenuWorldX, page.addMenuWorldY)
                        addNodeMenu.close()
                        canvasViewport.forceActiveFocus()
                    }
                }
            }
        }
    }

    Dialog {
        id: confirmSave
        modal: true
        anchors.centerIn: parent
        width: Math.min(page.width - 40, 420)
        title: qsTr("기존 편집기의 초안을 교체할까요?")
        standardButtons: Dialog.NoButton
        palette.window: page.surfaceColor
        palette.windowText: page.primaryText
        background: Rectangle {
            radius: 10
            color: page.surfaceColor
            border.color: page.borderColor
        }
        contentItem: ColumnLayout {
            spacing: 14
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: page.primaryText
                text: qsTr("기존 편집기에서 저장하지 않은 초안을 현재 그래프로 교체하고, 규칙과 노드 위치를 함께 저장합니다.")
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ThemeButton { text: qsTr("취소"); onClicked: confirmSave.close() }
                ThemeButton {
                    text: qsTr("전체 저장")
                    onClicked: {
                        confirmSave.close()
                        page.saveAll()
                    }
                }
            }
        }
    }

    Dialog {
        id: confirmReload
        modal: true
        anchors.centerIn: parent
        width: Math.min(page.width - 40, 420)
        title: qsTr("저장된 규칙 다시 열기")
        standardButtons: Dialog.NoButton
        palette.window: page.surfaceColor
        palette.windowText: page.primaryText
        background: Rectangle {
            radius: 10
            color: page.surfaceColor
            border.color: page.borderColor
        }
        contentItem: ColumnLayout {
            spacing: 14
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: page.primaryText
                text: qsTr("저장하지 않은 그래프와 기존 편집기의 규칙 초안을 버리고, 마지막 저장본을 다시 불러옵니다.")
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ThemeButton { text: qsTr("취소"); onClicked: confirmReload.close() }
                ThemeButton {
                    text: qsTr("다시 열기")
                    onClicked: {
                        confirmReload.close()
                        ruleEditor.reload()
                        page.loadGraphFromDraft()
                        page.editorDraftReplaced()
                    }
                }
            }
        }
    }
}
