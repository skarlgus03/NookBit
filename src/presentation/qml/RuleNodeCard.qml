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
    readonly property bool documentAnalysis: modelData.type === "documentAnalysis"

    x: blockCard.editor.screenPoint(blockCard.editor.positionFor(modelData).x, blockCard.editor.positionFor(
                                        modelData).y).x
    y: blockCard.editor.screenPoint(blockCard.editor.positionFor(modelData).x, blockCard.editor.positionFor(
                                        modelData).y).y
    width: blockCard.editor.widthFor(modelData)
    height: blockCard.editor.heightFor(modelData)
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
    border.color: blockCard.editor.testTraceBlockIds.includes(modelData.id) ? theme.edgeTrace
        : blockCard.editor.selectedIds.includes(modelData.id) ? (theme.nodeSelectionKinds[modelData.type] || theme.nodeSelectionKinds.fallback)
        : modelData.type === "start" ? theme.nodeInputBorder : blockCard.borderColor

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
    Label {
        anchors.centerIn: parent
        visible: blockCard.editor.isLogical(modelData)
        text: (blockCard.editor.orderFor(modelData.id) > 0 ? blockCard.editor.orderFor(modelData.id) + " · " : "") + blockCard.editor.titleFor(modelData.type)
        font.bold: true
        font.pixelSize: blockCard.editor.nodeFontSize
        color: blockCard.primaryText
    }
    Rectangle {
        id: predicatePort
        z: 2
        visible: blockCard.editor.isLogical(modelData)
        width: 16; height: 16
        x: -width / 2; y: parent.height * 0.75 - height / 2
        radius: UiMetrics.spaceTiny
        color: blockCard.editor.edges.some(edge => edge.to === modelData.id && edge.kind === "predicate") ? blockCard.theme.editorAccent : blockCard.nodeColor
        border.width: UiMetrics.borderActive
        border.color: blockCard.theme.editorAccent
        MouseArea {
            objectName: "rulePredicatePort_" + modelData.id
            anchors.fill: parent
            hoverEnabled: true
            ToolTip.visible: containsMouse
            ToolTip.text: qsTr("조건 입력 · 조건 출력점을 여기에 연결하세요")
            onClicked: if (blockCard.editor.connectingFrom >= 0)
                blockCard.editor.connectBlocks(blockCard.editor.connectingFrom, modelData.id, "predicate", blockCard.editor.connectingPort)
        }
    }
    Rectangle {
        id: cardHeader
        visible: modelData.type !== "start" && !blockCard.editor.isLogical(modelData)
        x: 1
        y: 1
        width: parent.width - 2
        height: blockCard.editor.nodeHeaderHeight
        radius: 11
        color: blockCard.colorFor(modelData.type)
        NavigationIcon {
            id: documentIcon
            visible: blockCard.documentAnalysis
            kind: "documentSearch"
            width: UiMetrics.iconSize
            height: width
            x: orderBadge.visible ? orderBadge.x + orderBadge.width + UiMetrics.spaceSmall : UiMetrics.spaceGroup
            anchors.verticalCenter: parent.verticalCenter
            strokeColor: blockCard.theme.onNode
        }
        Label {
            anchors.left: parent.left
            anchors.leftMargin: documentIcon.visible ? documentIcon.x + documentIcon.width + UiMetrics.spaceSmall
                : orderBadge.visible ? 42 : 12
            anchors.right: parent.right
            anchors.rightMargin: blockCard.documentAnalysis ? algorithmBadge.width + UiMetrics.spaceGroup * 2
                : rootBadge.visible ? 60 : 10
            anchors.verticalCenter: parent.verticalCenter
            text: blockCard.documentAnalysis ? qsTr("문서 분석") : blockCard.editor.titleFor(modelData.type)
            font.bold: true
            font.pixelSize: blockCard.editor.nodeFontSize
            elide: Text.ElideRight
            color: blockCard.theme.onNode
        }
        Rectangle {
            id: algorithmBadge
            visible: blockCard.documentAnalysis
            anchors.right: parent.right
            anchors.rightMargin: UiMetrics.spaceControl
            anchors.verticalCenter: parent.verticalCenter
            width: algorithmName.implicitWidth + UiMetrics.spaceControl * 2
            height: algorithmName.implicitHeight + UiMetrics.spaceTiny * 2
            radius: height / 2
            color: Qt.darker(cardHeader.color, 1.35)
            Label {
                id: algorithmName
                anchors.centerIn: parent
                text: "TF-IDF"
                color: blockCard.theme.onNode
                font.pixelSize: UiMetrics.nodeCaptionSizes[blockCard.editor.fontPresetIndex]
            }
        }
        Rectangle {
            id: orderBadge
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
            visible: !blockCard.documentAnalysis && blockCard.editor.isRootBlock(modelData.id)
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
        visible: !blockCard.documentAnalysis && modelData.type !== "start" && !blockCard.editor.isLogical(modelData)
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
        visible: !blockCard.documentAnalysis && modelData.type !== "start" && !blockCard.editor.isLogical(modelData)
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 12
        anchors.bottomMargin: 8
        text: modelData.type === "flowStart" ? qsTr("시작 →") : modelData.type === "folderScan" ? qsTr("파일별 →")
            : ["applyDefault", "callFlow"].includes(modelData.type) ? qsTr("흐름 호출")
            : modelData.type === "destination" ? qsTr("도착") : modelData.type === "exclude" ? qsTr("이동 안 함") : qsTr(
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
        visible: modelData.type !== "start" && modelData.type !== "flowStart"
        x: -8
        y: blockCard.editor.inputPortY(modelData) - height / 2
        width: 16
        height: 16
        radius: UiMetrics.radiusControl
        color: blockCard.editor.hasIncoming(modelData.id) ? border.color : blockCard.nodeColor
        border.width: 3
        border.color: blockCard.documentAnalysis ? blockCard.theme.editorAccent : blockCard.colorFor(modelData.type)
        MouseArea {
            anchors.fill: parent
            anchors.margins: -12
            onClicked: {
                if (blockCard.editor.connectingFrom >= 0)
                    blockCard.editor.connectBlocks(blockCard.editor.connectingFrom, modelData.id, "", blockCard.editor.connectingPort);
                else
                    blockCard.editor.selectOnly(modelData.id);
            }
        }
    }
    // 문서 분석 카드의 섹션 구분선을 현재 테마에서 그리는 부품.
    component DocumentDivider: Rectangle {
        x: UiMetrics.borderNormal
        width: blockCard.width - UiMetrics.borderNormal * 2
        height: UiMetrics.borderNormal
        color: blockCard.borderColor
    }
    // 문서 분석 카드에 놓이는 조밀한 읽기 전용 텍스트.
    component DocumentLabel: Label {
        color: blockCard.primaryText
        font.pixelSize: blockCard.editor.nodeFontSize
        textFormat: Text.PlainText
    }
    Item {
        id: documentBody
        visible: blockCard.documentAnalysis
        readonly property var categories: visible ? blockCard.modelData.categories || [] : []
        anchors.fill: parent
        DocumentLabel {
            visible: documentBody.categories.length === 0
            x: UiMetrics.spaceSection
            width: parent.width - x * 2
            y: blockCard.editor.nodeHeaderHeight
            height: blockCard.editor.documentRowHeight * 2
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.Wrap
            color: blockCard.mutedText
            text: qsTr("문서 내용으로 분류를 골라요.\n분류를 추가하고 키워드를 입력하세요.")
        }
        RowLayout {
            visible: documentBody.categories.length > 0
            x: UiMetrics.spaceSection
            y: blockCard.editor.nodeHeaderHeight
            width: parent.width - x * 2
            height: blockCard.editor.documentRowHeight
            DocumentLabel {
                Layout.fillWidth: true
                text: qsTr("최소 유사도")
            }
            DocumentLabel {
                objectName: "ruleDocumentSimilarity"
                text: ((blockCard.modelData.minimumSimilarity ?? blockCard.editor.defaultDocumentSimilarity) / 100).toFixed(2)
                color: blockCard.mutedText
            }
        }
        Repeater {
            model: documentBody.categories
            delegate: Item {
                id: categoryRow
                required property int index
                required property var modelData
                readonly property var keywords: (modelData.keywords || "").split(",").map(word => word.trim()).filter(word => word.length)
                x: 0
                y: blockCard.editor.nodeHeaderHeight + blockCard.editor.documentRowHeight
                    + index * blockCard.editor.documentCategoryHeight
                width: documentBody.width
                height: blockCard.editor.documentCategoryHeight
                DocumentDivider {}
                Column {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: UiMetrics.spaceSection
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: UiMetrics.spaceTiny
                    DocumentLabel {
                        width: parent.width
                        text: categoryRow.modelData.label.trim() || qsTr("이름 없는 분류")
                        elide: Text.ElideRight
                        font.weight: Font.DemiBold
                    }
                    RowLayout {
                        width: parent.width
                        spacing: UiMetrics.spaceExtraSmall
                        Repeater {
                            model: categoryRow.keywords.slice(0, 3)
                            delegate: Rectangle {
                                required property string modelData
                                implicitWidth: Math.min(keyword.implicitWidth + UiMetrics.spaceControl, categoryRow.width / 4)
                                implicitHeight: keyword.implicitHeight + UiMetrics.spaceTiny * 2
                                radius: height / 2
                                color: blockCard.theme.alternate
                                DocumentLabel {
                                    id: keyword
                                    anchors.fill: parent
                                    anchors.leftMargin: UiMetrics.spaceExtraSmall
                                    anchors.rightMargin: UiMetrics.spaceExtraSmall
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                    text: parent.modelData
                                    font.pixelSize: UiMetrics.nodeCaptionSizes[blockCard.editor.fontPresetIndex]
                                }
                            }
                        }
                        DocumentLabel {
                            visible: categoryRow.keywords.length === 0 || categoryRow.keywords.length > 3
                            text: categoryRow.keywords.length === 0 ? qsTr("키워드를 입력하세요") : "+" + (categoryRow.keywords.length - 3)
                            color: blockCard.mutedText
                            font.pixelSize: UiMetrics.nodeCaptionSizes[blockCard.editor.fontPresetIndex]
                        }
                        Item { Layout.fillWidth: true }
                    }
                }
            }
        }
        DocumentDivider { y: blockCard.editor.documentAddTop(blockCard.modelData) }
        DocumentDivider { y: blockCard.editor.documentFooterTop(blockCard.modelData) }
        RowLayout {
            x: UiMetrics.spaceSection
            y: blockCard.editor.documentFooterTop(blockCard.modelData)
            width: parent.width - x * 2
            height: blockCard.editor.documentRowHeight
            NavigationIcon {
                kind: "history"
                Layout.preferredWidth: UiMetrics.iconSize
                Layout.preferredHeight: UiMetrics.iconSize
                strokeColor: blockCard.mutedText
            }
            DocumentLabel {
                Layout.fillWidth: true
                text: qsTr("보류")
                font.weight: Font.DemiBold
            }
            DocumentLabel {
                objectName: "ruleDocumentAbstainHint"
                text: documentBody.categories.length === 0 ? qsTr("모든 문서") : qsTr("기준 미달")
                color: blockCard.mutedText
            }
        }
    }
    RuleEditorButton {
        visible: blockCard.documentAnalysis
        dark: blockCard.dark
        objectName: "ruleAddDocumentCategory_" + blockCard.modelData.id
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: UiMetrics.spaceGroup
        anchors.rightMargin: UiMetrics.spaceGroup
        y: blockCard.editor.documentAddTop(blockCard.modelData) + UiMetrics.spaceSmall
        height: blockCard.editor.documentRowHeight
        text: documentBody.categories.length === 0 ? qsTr("+ 첫 분류 추가") : qsTr("+ 분류 추가")
        onClicked: blockCard.editor.addDocumentCategory(blockCard.modelData.id)
    }
    Repeater {
        model: blockCard.documentAnalysis ? blockCard.editor.documentPorts(blockCard.modelData)
            : ["destination", "exclude", "applyDefault", "callFlow"].includes(blockCard.modelData.type) ? [] : [""]
        delegate: Rectangle {
            id: outputPort
            required property string modelData
            readonly property string portId: modelData
            x: blockCard.width - width / 2
            y: (blockCard.documentAnalysis ? blockCard.editor.documentPortY(blockCard.modelData, portId) : blockCard.height / 2) - height / 2
            width: blockCard.modelData.type === "start" ? 22 : 16
            height: width
            radius: width / 2
            color: blockCard.editor.hasOutgoing(blockCard.modelData.id, portId) ? border.color : blockCard.nodeColor
            border.width: blockCard.modelData.type === "start" ? 3 : 2
            border.color: !blockCard.documentAnalysis ? blockCard.colorFor(blockCard.modelData.type)
                : portId === "abstain" ? blockCard.mutedText : blockCard.theme.editorAccent
            MouseArea {
                objectName: "ruleOutputPort_" + blockCard.modelData.id + (outputPort.portId ? "_" + outputPort.portId : "")
                anchors.fill: parent
                anchors.margins: blockCard.documentAnalysis ? -UiMetrics.spaceExtraSmall : -12
                preventStealing: true
                onPressed: mouse => {
                    blockCard.editor.connectingFrom = blockCard.modelData.id;
                    blockCard.editor.connectingPort = outputPort.portId;
                    blockCard.editor.draggingConnection = true;
                    const screen = mapToItem(blockCard.viewport, mouse.x, mouse.y);
                    const point = blockCard.editor.worldPoint(screen.x, screen.y);
                    blockCard.editor.pointerX = point.x;
                    blockCard.editor.pointerY = point.y;
                    blockCard.repaintRequested();
                }
                onPositionChanged: mouse => {
                    if (blockCard.editor.connectingFrom === blockCard.modelData.id && blockCard.editor.draggingConnection) {
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
    }
    // 노드 종류에 맞는 기능 색상을 공통 테마에서 읽는 함수.
    function colorFor(type) {
        return theme.nodeKinds[type] || theme.nodeKinds.fallback;
    }
}
