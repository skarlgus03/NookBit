import QtQuick

// 그래프·선택·카메라·시험 결과만 소유하며 화면 부품과 Controller를 알지 않는 편집 세션.
QtObject {
    id: editor
    objectName: "ruleEditorState"
    property real viewportWidth: 0
    property real viewportHeight: 0
    signal repaintRequested
    signal destinationUpdated(int blockId, string value)
    readonly property var nodeAddGroups: [
        {
            title: qsTr("조건 검사"),
            hint: qsTr("조건이 맞으면 다음 노드로 진행해요."),
            nodes: [
                {
                    type: "extension",
                    title: qsTr("확장자")
                },
                {
                    type: "size",
                    title: qsTr("크기")
                },
                {
                    type: "name",
                    title: qsTr("이름 포함")
                },
                {
                    type: "age",
                    title: qsTr("수정 경과일")
                }
            ]
        },
        {
            title: qsTr("분기"),
            hint: qsTr("같은 부모의 다른 조건이 모두 맞지 않을 때 진행해요."),
            nodes: [
                {
                    type: "otherwise",
                    title: qsTr("그 외")
                }
            ]
        },
        {
            title: qsTr("종착점"),
            hint: qsTr("여기서 분류가 끝나요. 자식 노드를 연결할 수 없어요."),
            nodes: [
                {
                    type: "destination",
                    title: qsTr("목적지")
                },
                {
                    type: "exclude",
                    title: qsTr("정리 제외")
                }
            ]
        }
    ]
    property int fontPresetIndex: 1
    readonly property int nodeFontSize: UiMetrics.nodeTextSizes[fontPresetIndex]
    readonly property int blockWidth: UiMetrics.nodeWidths[fontPresetIndex]
    readonly property int blockHeight: UiMetrics.nodeHeights[fontPresetIndex]
    property var blocks: [
        {
            id: 1,
            type: "start",
            x: 70,
            y: 270,
            value: "정리할 파일"
        },
        {
            id: 2,
            type: "size",
            x: 390,
            y: 220,
            value: "5",
            comparison: "greaterThan",
            unit: "MB"
        },
        {
            id: 3,
            type: "extension",
            x: 720,
            y: 220,
            value: "png, jpg"
        },
        {
            id: 4,
            type: "destination",
            x: 1050,
            y: 220,
            value: "Pictures"
        }
    ]
    property var edges: [
        {
            from: 1,
            to: 2
        },
        {
            from: 2,
            to: 3
        },
        {
            from: 3,
            to: 4
        }
    ]
    property var livePositions: ({})
    property bool snapXActive: false
    property bool snapYActive: false
    property real snapGuideX: 0
    property real snapGuideY: 0
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
    property int testMode: 0
    property url testFileUrl: ""
    property var testResult: ({})
    property string testError: ""
    property var testTraceBlockIds: []
    readonly property var selectedBlock: selectedIds.length === 1 ? blockForId(selectedId) : null

    onBlocksChanged: {
        graphDirty = true;
        stagedGraph = false;
        testTraceBlockIds = [];
        testResult = ({});
        repaintRequested();
    }
    onEdgesChanged: {
        graphDirty = true;
        stagedGraph = false;
        testTraceBlockIds = [];
        testResult = ({});
        repaintRequested();
    }
    onTestModeChanged: {
        testResult = ({});
        testError = "";
        testTraceBlockIds = [];
    }
    onAltHeldChanged: updateHoveredEdge(hoverX, hoverY)
    onPanXChanged: repaintRequested()
    onPanYChanged: repaintRequested()
    onZoomChanged: repaintRequested()
    onFontPresetIndexChanged: repaintRequested()
    onTestTraceBlockIdsChanged: repaintRequested()
    onConnectionStyleChanged: repaintRequested()
    // 단일 선택을 만들고 상세 설정 대상을 맞추는 함수.
    function selectOnly(id) {
        selectedIds = id < 0 ? [] : [id];
        selectedId = id;
    }

    // 파일 입력 바로 아래에 연결된 최상위 조건 블록인지 확인하는 함수.
    function isRootBlock(id) {
        return edges.some(edge => edge.from === 1 && edge.to === id);
    }

    // Shift+클릭으로 노드를 다중 선택에 더하거나 빼는 함수.
    function toggleSelection(id) {
        const next = selectedIds.slice();
        const index = next.indexOf(id);
        if (index < 0)
            next.push(id);
        else
            next.splice(index, 1);
        selectedIds = next;
        selectedId = next.length === 1 ? next[0] : -1;
    }

    // 선택 박스에 닿는 노드를 고르고 Shift를 누르면 기존 선택과 합치는 함수.
    function finishMarquee(addToSelection) {
        const left = Math.min(marqueeStartX, marqueeEndX);
        const right = Math.max(marqueeStartX, marqueeEndX);
        const top = Math.min(marqueeStartY, marqueeEndY);
        const bottom = Math.max(marqueeStartY, marqueeEndY);
        const hits = blocks.filter(block => {
            const pos = positionFor(block);
            const screen = screenPoint(pos.x, pos.y);
            return screen.x < right && screen.x + blockWidth * zoom > left && screen.y < bottom && screen.y
                    + blockHeight * zoom > top;
        }).map(block => block.id);
        const next = addToSelection ? selectedIds.concat(hits.filter(id => !selectedIds.includes(id))) : hits;
        selectedIds = next;
        selectedId = next.length === 1 ? next[0] : -1;
        marqueeActive = false;
    }

    // 노드 종류에 맞는 입력값과 비교 방식을 카드에 표시하는 함수.
    function displayValue(block) {
        if (block.type === "size" || block.type === "age") {
            const comparisons = {
                greaterThan: qsTr("초과"),
                atLeast: qsTr("이상"),
                lessThan: qsTr("미만"),
                atMost: qsTr("이하")
            };
            const unit = block.type === "size" ? (block.unit || "MB") : qsTr("일");
            return block.value + " " + unit + " " + (comparisons[block.comparison] || "");
        }
        return block.value;
    }

    // 숫자 조건의 현재 비교 방식 이름을 반환하는 함수.
    function comparisonTitle(block) {
        const names = {
            greaterThan: qsTr("초과"),
            atLeast: qsTr("이상"),
            lessThan: qsTr("미만"),
            atMost: qsTr("이하")
        };
        return names[block.comparison] || qsTr("선택 안 됨");
    }

    // ID로 화면의 블록 하나를 찾는 함수.
    function blockForId(id) {
        for (const block of blocks) {
            if (block.id === id)
                return block;
        }
        return null;
    }

    // 블록 입력점에 다른 블록의 선이 연결되었는지 반환하는 함수.
    function hasIncoming(id) {
        return edges.some(edge => edge.to === id);
    }

    // 블록 출력점에서 다른 블록으로 선이 이어지는지 반환하는 함수.
    function hasOutgoing(id) {
        return edges.some(edge => edge.from === id);
    }

    // 드래그 중인 블록은 아직 모델에 저장되지 않은 화면 위치를 반환하는 함수.
    function positionFor(block) {
        return livePositions[block.id] || block;
    }

    // 화면 좌표를 경계 없는 작업 공간 좌표로 바꾸는 함수.
    function worldPoint(screenX, screenY) {
        return {
            x: (screenX - panX) / zoom,
            y: (screenY - panY) / zoom
        };
    }

    // 작업 공간 좌표를 화면 좌표로 바꾸는 함수.
    function screenPoint(worldX, worldY) {
        return {
            x: panX + worldX * zoom,
            y: panY + worldY * zoom
        };
    }

    // 휠 아래의 작업 공간 위치를 유지하며 확대하거나 축소하는 함수.
    function zoomAt(delta, viewportX, viewportY) {
        if (delta === 0)
            return;
        const oldZoom = zoom;
        const nextZoom = Math.max(0.05, Math.min(2.5, oldZoom * Math.pow(1.12, delta / 120)));
        if (nextZoom === oldZoom)
            return;
        const worldX = (viewportX - panX) / oldZoom;
        const worldY = (viewportY - panY) / oldZoom;
        zoom = nextZoom;
        panX = viewportX - worldX * nextZoom;
        panY = viewportY - worldY * nextZoom;
    }

    // 규칙을 불러왔을 때 100% 배율로 파일 입력 노드부터 보여주는 함수.
    function resetDefaultView() {
        zoom = 1.0;
        const start = blocks.find(block => block.type === "start") || blocks[0];
        if (!start || viewportWidth <= 0 || viewportHeight <= 0)
            return;
        const pos = positionFor(start);
        panX = 80 - pos.x;
        panY = (viewportHeight - blockHeight) / 2 - pos.y;
        repaintRequested();
    }

    // 모든 노드가 보이도록 화면 위치와 확대율만 맞추는 함수.
    function fitAllNodes() {
        if (blocks.length === 0 || viewportWidth <= 0 || viewportHeight <= 0)
            return;
        let left = Infinity, top = Infinity, right = -Infinity, bottom = -Infinity;
        for (const block of blocks) {
            const pos = positionFor(block);
            left = Math.min(left, pos.x);
            top = Math.min(top, pos.y);
            right = Math.max(right, pos.x + blockWidth);
            bottom = Math.max(bottom, pos.y + blockHeight);
        }
        const margin = 80;
        zoom = Math.max(0.05, Math.min(2.5, Math.min((viewportWidth - margin * 2) / Math.max(1, right - left), (
                                                         viewportHeight - margin * 2) / Math.max(1, bottom - top))));
        panX = (viewportWidth - (left + right) * zoom) / 2;
        panY = (viewportHeight - (top + bottom) * zoom) / 2;
        repaintRequested();
    }

    // 종류에 따라 블록 제목을 정하는 함수.
    function titleFor(type) {
        switch (type) {
        case "start":
            return qsTr("파일 입력");
        case "extension":
            return qsTr("확장자");
        case "size":
            return qsTr("파일 크기");
        case "name":
            return qsTr("이름 포함");
        case "age":
            return qsTr("수정 후 지난 일수");
        case "otherwise":
            return qsTr("그 외");
        case "destination":
            return qsTr("목적지");
        case "exclude":
            return qsTr("정리 제외");
        }
        return qsTr("조건");
    }

    // 우클릭 메뉴에서 고른 종류의 임시 블록을 기억한 위치에 추가하는 함수.
    function addBlock(type, worldX, worldY) {
        const examples = {
            extension: "png, jpg",
            size: "5",
            name: "report",
            age: "7",
            otherwise: "조건 없음",
            destination: "Pictures",
            exclude: "이 파일은 이동하지 않음"
        };
        const block = {
            id: nextId++,
            type: type,
            x: worldX,
            y: worldY,
            value: examples[type],
            comparison: type === "size" ? "greaterThan" : "atLeast",
            unit: "MB"
        };
        blocks = blocks.concat([block]);
        selectOnly(block.id);
        statusText = qsTr("블록을 추가했어요. 블록을 끌어 위치를 바꾸고, 오른쪽 점을 왼쪽 점에 연결해 보세요.");
        repaintRequested();
    }

    // 선택한 시작 노드 이외의 블록과 그 사이 연결선만 내부 복사본으로 보관하는 함수.
    function copySelectedBlocks() {
        const chosen = blocks.filter(block => selectedIds.includes(block.id) && block.type !== "start");
        if (chosen.length === 0) {
            statusText = qsTr("복사할 블록을 먼저 선택해 주세요. 파일 입력 블록은 복사할 수 없어요.");
            return;
        }
        const ids = chosen.map(block => block.id);
        copiedBlocks = chosen.map(block => Object.assign({}, block));
        copiedEdges = edges.filter(edge => ids.includes(edge.from) && ids.includes(edge.to)).map(edge => Object.assign({},
                                                                                                                       edge));
        pasteCount = 0;
        statusText = qsTr("블록 %1개를 복사했어요. Ctrl+V로 화면 가운데에 붙여넣으세요.").arg(chosen.length);
    }

    // 내부 복사본의 노드 ID를 새로 만들고 연결 관계를 유지한 채 붙여넣는 함수.
    function pasteCopiedBlocks() {
        if (copiedBlocks.length === 0) {
            statusText = qsTr("붙여넣을 블록이 없어요. 먼저 Ctrl+C로 복사해 주세요.");
            return;
        }
        const left = Math.min(...copiedBlocks.map(block => block.x));
        const top = Math.min(...copiedBlocks.map(block => block.y));
        const center = worldPoint(viewportWidth / 2, viewportHeight / 2);
        const shift = pasteCount * 32 / zoom;
        const idMap = ({});
        const created = copiedBlocks.map(block => {
            const copy = Object.assign({}, block, {
                                           id: nextId++,
                                           x: center.x + block.x - left + shift,
                                           y: center.y + block.y - top + shift
                                       });
            delete copy.ruleId;
            delete copy.parentRuleId;
            idMap[block.id] = copy.id;
            return copy;
        });
        const copiedConnections = copiedEdges.map(edge => ({
            from: idMap[edge.from],
            to: idMap[edge.to]
        }));
        blocks = blocks.concat(created);
        edges = edges.concat(copiedConnections);
        selectedIds = created.map(block => block.id);
        selectedId = selectedIds.length === 1 ? selectedIds[0] : -1;
        pasteCount += 1;
        statusText = qsTr("블록 %1개를 붙여넣었어요. 분류 규칙에 적용하려면 연결과 값을 확인해 주세요.").arg(created.length);
        repaintRequested();
    }

    // 선택한 노드들의 드래그 위치를 한 번에 임시 그래프에 반영하는 함수.
    function commitDraggedBlocks() {
        const positions = livePositions;
        blocks = blocks.map(block => positions[block.id] ? Object.assign({}, block, {
                                                                             x: Math.round(positions[block.id].x),
                                                                             y: Math.round(positions[block.id].y)
                                                                         }) : block);
        livePositions = ({});
        snapXActive = false;
        snapYActive = false;
        repaintRequested();
    }

    // 드래그 중 부모와 높이, 형제와 가로 위치가 가까우면 선택 묶음을 함께 붙이는 함수.
    function updateDraggedPositions(primaryId, starts, dx, dy, snapEnabled) {
        let adjustedX = dx;
        let adjustedY = dy;
        snapXActive = false;
        snapYActive = false;
        const origin = starts[primaryId];
        const incoming = edges.find(edge => edge.to === primaryId);
        if (snapEnabled && origin && incoming) {
            const threshold = 12 / zoom;
            const parent = blockForId(incoming.from);
            if (parent && !selectedIds.includes(parent.id)) {
                const parentY = positionFor(parent).y;
                if (Math.abs(origin.y + dy - parentY) <= threshold) {
                    adjustedY = parentY - origin.y;
                    snapGuideY = parentY;
                    snapYActive = true;
                }
            }
            let closestX = threshold;
            for (const edge of edges) {
                if (edge.from !== incoming.from || selectedIds.includes(edge.to))
                    continue;
                const sibling = blockForId(edge.to);
                if (!sibling)
                    continue;
                const siblingX = positionFor(sibling).x;
                const distance = Math.abs(origin.x + dx - siblingX);
                if (distance <= closestX) {
                    closestX = distance;
                    adjustedX = siblingX - origin.x;
                    snapGuideX = siblingX;
                    snapXActive = true;
                }
            }
        }
        const next = ({});
        for (const id of selectedIds) {
            const start = starts[id];
            if (start)
                next[id] = {
                    x: start.x + adjustedX,
                    y: start.y + adjustedY
                };
        }
        livePositions = next;
        repaintRequested();
    }

    // 선택한 일반 노드를 세로 한 줄에 고르게 배치하되 규칙 검사 순서는 바꾸지 않는 함수.
    function alignSelectedVertically() {
        const chosen = blocks.filter(block => selectedIds.includes(block.id) && block.type !== "start");
        if (chosen.length < 2) {
            statusText = qsTr("세로 정렬할 블록을 두 개 이상 선택해 주세요.");
            return;
        }
        chosen.sort((a, b) => a.y - b.y || a.id - b.id);
        const xs = chosen.map(block => block.x).sort((a, b) => a - b);
        const targetX = Math.round(xs[Math.floor(xs.length / 2)]);
        const topY = Math.min(...chosen.map(block => block.y));
        const positions = ({});
        chosen.forEach((block, index) => {
            positions[block.id] = {
                x: targetX,
                y: topY + index * (blockHeight + 24)
            };
        });
        blocks = blocks.map(block => positions[block.id] ? Object.assign({}, block, positions[block.id]) : block);
        statusText = qsTr("선택한 블록을 세로로 정렬했어요. 규칙 검사 순서는 그대로예요.");
        repaintRequested();
    }

    // 선택한 블록의 실제 조건 입력값을 그래프에서 바꾸는 함수.
    function updateValue(id, value) {
        blocks = blocks.map(block => block.id === id ? Object.assign({}, block, {
                                                                         value: value
                                                                     }) : block);
        statusText = qsTr("입력값을 바꿨어요. 규칙 초안으로 변환해야 적용됩니다.");
    }

    // 드롭다운에서 선택한 비교 방식을 해당 조건에 적용하는 함수.
    function setComparison(id, value) {
        blocks = blocks.map(block => {
            if (block.id !== id)
                return block;
            const choices = block.type === "size" ? ["greaterThan", "atLeast", "lessThan", "atMost"] : ["atLeast",
                                                                                                        "lessThan"];
            return choices.indexOf(value) >= 0 ? Object.assign({}, block, {
                                                                   comparison: value
                                                               }) : block;
        });
    }

    // 목적지의 기준과 입력 경로를 함께 갱신하는 함수.
    function setDestination(id, value, homeRelative) {
        blocks = blocks.map(block => block.id === id ? Object.assign({}, block, {
                                                                         value: value,
                                                                         homeRelative: homeRelative
                                                                     }) : block);
        destinationUpdated(id, value);
    }

    // 선택한 표시 단위를 적용하고 기존 바이트·MiB 규칙도 유지하는 함수.
    function setSizeUnit(id, unit) {
        if (["KB", "MB", "GB", "B", "MiB"].indexOf(unit) < 0)
            return;
        blocks = blocks.map(block => block.id === id ? Object.assign({}, block, {
                                                                         unit: unit
                                                                     }) : block);
    }

    // 선택한 조건 또는 그 외 가지의 사용 여부를 바꾸는 함수.
    function toggleEnabled(id) {
        blocks = blocks.map(block => block.id === id ? Object.assign({}, block, {
                                                                         enabled: block.enabled === false
                                                                     }) : block);
    }

    // 같은 부모 아래 일반 조건의 검사 순서를 반환하는 함수.
    function orderFor(id) {
        const incoming = edges.find(edge => edge.to === id);
        if (!incoming)
            return -1;
        const block = blockForId(id);
        if (!block || block.type === "destination" || block.type === "exclude" || block.type === "otherwise")
            return -1;
        let order = 0;
        for (const edge of edges) {
            if (edge.from !== incoming.from)
                continue;
            const sibling = blockForId(edge.to);
            if (sibling && sibling.type !== "otherwise")
                ++order;
            if (edge.to === id)
                return order;
        }
        return -1;
    }

    // 선택한 조건을 같은 부모의 앞뒤 조건과 교환하는 함수.
    function moveSelectedOrder(direction) {
        const incoming = edges.find(edge => edge.to === selectedId);
        if (!incoming || orderFor(selectedId) < 0)
            return;
        const siblingEdges = edges.filter(edge => edge.from === incoming.from && blockForId(edge.to).type
                                                  !== "otherwise");
        const index = siblingEdges.findIndex(edge => edge.to === selectedId);
        const target = siblingEdges[index + direction];
        if (!target)
            return;
        const next = edges.slice();
        const first = next.findIndex(edge => edge.from === incoming.from && edge.to === selectedId);
        const second = next.findIndex(edge => edge.from === incoming.from && edge.to === target.to);
        const saved = next[first];
        next[first] = next[second];
        next[second] = saved;
        edges = next;
        statusText = qsTr("같은 부모 아래 조건의 검사 순서를 바꿨어요.");
        repaintRequested();
    }

    // 이미 연결된 경로를 따라 목적지 ID에 도달하는지 검사하는 함수.
    function hasPath(fromId, targetId) {
        if (fromId === targetId)
            return true;
        for (const edge of edges) {
            if (edge.from === fromId && hasPath(edge.to, targetId))
                return true;
        }
        return false;
    }

    // 한 부모와 한 자식 사이에 순환 없는 임시 연결을 만드는 함수.
    function connectBlocks(fromId, toId) {
        const source = blockForId(fromId);
        const target = blockForId(toId);
        if (!source || !target || source.type === "destination" || source.type === "exclude" || target.type === "start")
            return false;
        if (fromId === toId || hasPath(toId, fromId)) {
            statusText = qsTr("자기 자신이나 조상 블록으로 연결할 수 없어요.");
            return false;
        }
        for (const edge of edges) {
            if (edge.to === toId) {
                statusText = qsTr("입력 하나에는 부모 블록 하나만 연결할 수 있어요.");
                return false;
            }
        }
        const existing = edges.filter(edge => edge.from === fromId);
        const terminal = target.type === "destination" || target.type === "exclude";
        if (source.type === "start" && terminal) {
            statusText = qsTr("파일 입력 뒤에는 조건 또는 '그 외' 노드를 먼저 연결해 주세요.");
            return false;
        }
        if ((terminal && existing.length > 0) || (!terminal && existing.some(edge => {
            const child = blockForId(edge.to);
            return child.type === "destination" || child.type === "exclude";
        }))) {
            statusText = qsTr("목적지·정리 제외 노드는 다른 자식과 함께 연결할 수 없어요.");
            return false;
        }
        if (target.type === "otherwise" && existing.some(edge => blockForId(edge.to).type === "otherwise")) {
            statusText = qsTr("같은 부모에는 '그 외'를 하나만 연결할 수 있어요.");
            return false;
        }
        const next = edges.slice();
        const otherwiseIndex = next.findIndex(edge => edge.from === fromId && blockForId(edge.to).type === "otherwise");
        if (target.type !== "otherwise" && otherwiseIndex >= 0)
            next.splice(otherwiseIndex, 0, {
                            from: fromId,
                            to: toId
                        });
        else
            next.push({
                          from: fromId,
                          to: toId
                      });
        edges = next;
        connectingFrom = -1;
        draggingConnection = false;
        statusText = qsTr("연결했어요. 이 그래프는 아직 분류 규칙으로 저장되지 않습니다.");
        repaintRequested();
        return true;
    }

    // 출력점에서 놓은 위치에 입력점이 있으면 두 블록을 연결하는 함수.
    function finishConnectionAt(x, y) {
        if (connectingFrom < 0)
            return;
        const sourceId = connectingFrom;
        connectingFrom = -1;
        draggingConnection = false;
        repaintRequested();
        for (const block of blocks) {
            if (block.type !== "start" && Math.abs(x - block.x) < 28 && Math.abs(y - (block.y + blockHeight / 2))
                    < 28) {
                connectBlocks(sourceId, block.id);
                return;
            }
        }
        statusText = qsTr("연결점 밖에 놓아 선 연결을 취소했어요.");
        repaintRequested();
    }

    // 선택한 블록들과 이어진 선을 임시 그래프에서 함께 제거하는 함수.
    function removeSelectedBlock() {
        const removed = selectedIds.filter(id => id !== 1);
        if (removed.length === 0)
            return;
        blocks = blocks.filter(block => !removed.includes(block.id));
        edges = edges.filter(edge => !removed.includes(edge.from) && !removed.includes(edge.to));
        selectOnly(-1);
        if (removed.includes(connectingFrom))
            connectingFrom = -1;
        draggingConnection = false;
        const next = Object.assign({}, livePositions);
        for (const id of removed)
            delete next[id];
        livePositions = next;
        statusText = qsTr("블록과 연결선을 화면에서 제거했어요.");
        repaintRequested();
    }

    // 예시 블록과 연결을 다시 만드는 함수.
    function resetExample() {
        blocks = [
                    {
                        id: 1,
                        type: "start",
                        x: 70,
                        y: 270,
                        value: "정리할 파일"
                    },
                    {
                        id: 2,
                        type: "size",
                        x: 390,
                        y: 220,
                        value: "5",
                        comparison: "greaterThan",
                        unit: "MB"
                    },
                    {
                        id: 3,
                        type: "extension",
                        x: 720,
                        y: 220,
                        value: "png, jpg"
                    },
                    {
                        id: 4,
                        type: "destination",
                        x: 1050,
                        y: 220,
                        value: "Pictures"
                    }
                ];
        edges = [
                    {
                        from: 1,
                        to: 2
                    },
                    {
                        from: 2,
                        to: 3
                    },
                    {
                        from: 3,
                        to: 4
                    }
                ];
        nextId = 5;
        livePositions = ({});
        selectOnly(-1);
        connectingFrom = -1;
        draggingConnection = false;
        zoom = 1.0;
        panX = 0;
        panY = 0;
        statusText = qsTr("예시 그래프를 다시 불러왔어요.");
        repaintRequested();
    }

    // 화면의 한 점에서 선분까지의 거리를 구하는 함수.
    function distanceToSegment(px, py, x1, y1, x2, y2) {
        const dx = x2 - x1;
        const dy = y2 - y1;
        const lengthSquared = dx * dx + dy * dy;
        const fraction = lengthSquared > 0 ? Math.max(0, Math.min(1, ((px - x1) * dx + (py - y1) * dy) / lengthSquared)) :
                                             0;
        return Math.hypot(px - (x1 + fraction * dx), py - (y1 + fraction * dy));
    }

    // 커서에 가장 가까운 연결선을 찾아 Alt 강조 대상으로 정하는 함수.
    function edgeAt(screenX, screenY) {
        let nearest = -1;
        let distance = 12;
        for (let index = 0; index < edges.length; ++index) {
            const edge = edges[index];
            const source = blockForId(edge.from);
            const target = blockForId(edge.to);
            if (!source || !target)
                continue;
            const a = positionFor(source);
            const b = positionFor(target);
            const from = screenPoint(a.x + blockWidth, a.y + blockHeight / 2);
            const to = screenPoint(b.x, b.y + blockHeight / 2);
            let current = Infinity;
            if (connectionStyle === 1) {
                current = distanceToSegment(screenX, screenY, from.x, from.y, to.x, to.y);
            } else {
                const bend = Math.max(65, Math.abs(to.x - from.x) * 0.45);
                let previousX = from.x;
                let previousY = from.y;
                for (let step = 1; step <= 24; ++step) {
                    const t = step / 24;
                    const inverse = 1 - t;
                    const x = inverse * inverse * inverse * from.x + 3 * inverse * inverse * t * (from.x + bend) + 3
                          * inverse * t * t * (to.x - bend) + t * t * t * to.x;
                    const y = inverse * inverse * inverse * from.y + 3 * inverse * inverse * t * from.y + 3 * inverse
                          * t * t * to.y + t * t * t * to.y;
                    current = Math.min(current, distanceToSegment(screenX, screenY, previousX, previousY, x, y));
                    previousX = x;
                    previousY = y;
                }
            }
            if (current < distance) {
                nearest = index;
                distance = current;
            }
        }
        return nearest;
    }

    // Alt를 누르는 동안 커서 아래 선만 강조하도록 갱신하는 함수.
    function updateHoveredEdge(screenX, screenY) {
        hoverX = screenX;
        hoverY = screenY;
        const next = altHeld ? edgeAt(screenX, screenY) : -1;
        if (next !== hoveredEdgeIndex) {
            hoveredEdgeIndex = next;
            repaintRequested();
        }
    }

    // 강조된 연결선 하나만 그래프에서 제거하는 함수.
    function disconnectHoveredEdge() {
        if (hoveredEdgeIndex < 0 || hoveredEdgeIndex >= edges.length)
            return false;
        edges = edges.filter((edge, index) => index !== hoveredEdgeIndex);
        hoveredEdgeIndex = -1;
        statusText = qsTr("선 하나를 끊었어요. 연결을 다시 잇거나 저장본을 다시 열 수 있어요.");
        repaintRequested();
        return true;
    }

    // 현재 편집 초안의 규칙을 노드와 선으로 다시 펼치는 함수.
    function restoreGraph(restored) {
        if (!restored.valid) {
            selectOnly(-1);
            marqueeActive = false;
            blocks = [
                        {
                            id: 1,
                            type: "start",
                            x: 70,
                            y: 220,
                            value: "정리할 파일"
                        }
                    ];
            edges = [];
            livePositions = ({});
            nextId = 2;
            graphDirty = false;
            stagedGraph = false;
            statusText = restored.message || qsTr("규칙을 노드 화면에 불러오지 못했어요.");
            repaintRequested();
            return;
        }
        selectOnly(-1);
        marqueeActive = false;
        connectingFrom = -1;
        draggingConnection = false;
        livePositions = ({});
        blocks = restored.blocks;
        edges = restored.edges;
        nextId = restored.nextId;
        graphDirty = false;
        stagedGraph = false;
        statusText = restored.incompleteLeaves > 0 ? qsTr("기존 규칙에 종착점 없는 가지가 %1개 있어요. 목적지나 정리 제외를 연결해 주세요.").arg(
                                                         restored.incompleteLeaves) : qsTr("저장된 규칙을 노드 화면에 불러왔어요.");
        repaintRequested();
    }

    // 시험 결과에서 실제 선택 경로에 해당하는 화면 노드를 추출하는 함수.
    function applyTestResult(result) {
        testResult = result;
        testError = result.ok ? "" : result.error;
        testTraceBlockIds = [];
        if (!result.ok)
            return;
        const ids = [1];
        for (const ruleId of result.path) {
            const block = blocks.find(item => (item.ruleId || ("graph-" + item.id)) === ruleId);
            if (block)
                ids.push(block.id);
        }
        if (result.outcome !== "unclassified" && result.path.length > 0) {
            const lastRule = result.path[result.path.length - 1];
            const terminal = blocks.find(item => item.parentRuleId === lastRule && (item.type === "destination" || item.type
                                                                                    === "exclude"));
            if (terminal)
                ids.push(terminal.id);
        }
        testTraceBlockIds = ids;
        repaintRequested();
    }
}
