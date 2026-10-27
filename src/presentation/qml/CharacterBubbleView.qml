import QtQuick
import QtQuick.Window

// 화면 기준점만 받아 배치하며 캐릭터나 파일 작업을 알지 않는 말풍선.
Item {
    id: bubble
    objectName: "characterBubbleView"
    required property point anchorPoint
    required property rect screenArea
    required property string message
    property var configuration: ({})
    property bool dark: true
    readonly property var theme: UiTheme.colors(dark)
    property bool constrainToArea: true
    readonly property color surfaceColor: theme.bubbleSurface
    readonly property color edgeColor: theme.bubbleBorder
    property bool actionAvailable: false
    property string actionText: qsTr("확인")
    signal actionRequested()
    readonly property var skin: configuration.skin || ({kind: "default"})
    readonly property bool customSkin: skin.kind === "image"
    readonly property bool customReady: customSkin && customImage.status === Image.Ready
        && customImage.sourceSize.width === skin.imageWidth && customImage.sourceSize.height === skin.imageHeight
    readonly property var pivot: configuration.pivot || ({x: 0.1, y: 1})
    width: Math.max(1, Math.min(customSkin ? skin.width : UiMetrics.bubbleMaximumWidth,
        constrainToArea ? screenArea.width - 2 * UiMetrics.bubbleScreenMargin : Infinity))
    height: Math.max(1, Math.min(constrainToArea ? screenArea.height - 2 * UiMetrics.bubbleScreenMargin : Infinity, customSkin ? skin.height
        : Math.max(64, Math.min(bodyText.implicitHeight, UiMetrics.bubbleTextSize * 1.5 * UiMetrics.bubbleMaximumLines) + 34 + (actionAvailable ? 38 : 0)))
    )
    readonly property bool flippedLeft: constrainToArea && overflow(anchorPoint.x - (1 - pivot.x) * width, width, screenArea.x, screenArea.width)
        < overflow(anchorPoint.x - pivot.x * width, width, screenArea.x, screenArea.width)
    readonly property bool belowAnchor: constrainToArea && overflow(anchorPoint.y - (1 - pivot.y) * height, height, screenArea.y, screenArea.height)
        < overflow(anchorPoint.y - pivot.y * height, height, screenArea.y, screenArea.height)
    readonly property point effectivePivot: Qt.point((flippedLeft ? 1 - pivot.x : pivot.x) * width,
                                                    (belowAnchor ? 1 - pivot.y : pivot.y) * height)
    readonly property real placementX: Math.round(constrainToArea ? Math.max(screenArea.x + 8, Math.min(screenArea.x + screenArea.width - width - 8,
        anchorPoint.x - effectivePivot.x)) : anchorPoint.x - effectivePivot.x)
    readonly property real placementY: Math.round(constrainToArea ? Math.max(screenArea.y + 8, Math.min(screenArea.y + screenArea.height - height - 8,
        anchorPoint.y - effectivePivot.y)) : anchorPoint.y - effectivePivot.y)
    // 화면 밖으로 나가는 길이를 계산해 더 안전한 피벗 방향을 선택하는 함수.
    function overflow(start, length, screenStart, screenLength) {
        return Math.max(0, screenStart + 8 - start) + Math.max(0, start + length - (screenStart + screenLength - 8))
    }
    // 배경만 뒤집고 글과 버튼은 읽을 수 있는 방향으로 배치하는 함수.
    function contentRect(rect) {
        const sx = width / skin.width, sy = height / skin.height
        return {x: (flippedLeft ? skin.width - rect.x - rect.width : rect.x) * sx,
            y: (belowAnchor ? skin.height - rect.y - rect.height : rect.y) * sy,
            width: rect.width * sx, height: rect.height * sy}
    }
    readonly property var textArea: customSkin ? contentRect(skin.textRect)
        : ({x: UiMetrics.bubblePadding, y: (belowAnchor ? 10 : 0) + UiMetrics.bubblePadding, width: width - 2 * UiMetrics.bubblePadding, height: height - 34 - (actionAvailable ? 38 : 0)})
    readonly property var actionArea: customSkin ? contentRect(skin.actionRect)
        : ({x: width - 76, y: height - (belowAnchor ? 0 : 10) - 38, width: 64, height: 28})
    Canvas {
        id: tail
        anchors.fill: parent
        visible: !bubble.customReady
        // 기본 말풍선 꼬리 끝을 자체 피벗에 맞춰 그리는 함수.
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            const px = bubble.effectivePivot.x, py = bubble.effectivePivot.y
            const base = bubble.belowAnchor ? 14 : height - 14
            const center = Math.max(12, Math.min(width - 12, px))
            ctx.beginPath(); ctx.moveTo(px, py); ctx.lineTo(center - 8, base)
            ctx.lineTo(center + 8, base); ctx.closePath()
            ctx.fillStyle = bubble.surfaceColor; ctx.strokeStyle = bubble.edgeColor; ctx.fill(); ctx.stroke()
        }
        Connections {
            target: bubble
            // 피벗이나 방향이 바뀌면 기본 꼬리를 다시 그리는 함수.
            function onEffectivePivotChanged() { tail.requestPaint() }
            // 앱 테마 변경을 기본 말풍선 꼬리에도 적용하는 함수.
            function onDarkChanged() { tail.requestPaint() }
        }
    }
    Rectangle {
        visible: !bubble.customReady
        x: 1; width: bubble.width - 2
        y: bubble.belowAnchor ? 10 : 0
        height: bubble.height - 10
        radius: 14
        color: bubble.surfaceColor
        border.color: bubble.edgeColor
    }
    Image {
        id: customImage
        objectName: "characterBubbleBackground"
        anchors.fill: parent
        source: bubble.customSkin ? bubble.skin.source : ""
        visible: bubble.customReady
        asynchronous: true
        smooth: true
        transform: Scale {
            origin.x: bubble.width / 2; origin.y: bubble.height / 2
            xScale: bubble.flippedLeft ? -1 : 1; yScale: bubble.belowAnchor ? -1 : 1
        }
    }
    Item {
        x: bubble.textArea.x; y: bubble.textArea.y
        width: bubble.textArea.width; height: Math.max(0, bubble.textArea.height)
        clip: true
        Text {
            id: bodyText
            objectName: "characterBubbleText"
            width: parent.width
            text: bubble.message
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            maximumLineCount: UiMetrics.bubbleMaximumLines
            elide: Text.ElideRight
            font.pixelSize: UiMetrics.bubbleTextSize
            color: bubble.customSkin ? UiTheme.dark.text : bubble.theme.text
        }
    }
    Rectangle {
        objectName: "characterBubbleAction"
        visible: bubble.actionAvailable
        x: bubble.actionArea.x; y: bubble.actionArea.y
        width: bubble.actionArea.width; height: bubble.actionArea.height; radius: 8
        color: bubble.theme.bubbleAction
        border.color: actionMouse.containsMouse ? bubble.theme.buttonFocus : bubble.theme.bubbleBorder
        Text { anchors.centerIn: parent; text: bubble.actionText; color: bubble.theme.accentText; font.pixelSize: UiMetrics.bubbleTextSize }
        MouseArea {
            id: actionMouse
            anchors.fill: parent
            hoverEnabled: true
            // 확인 요청만 전달하고 파일 작업은 직접 실행하지 않는 함수.
            onClicked: bubble.actionRequested()
        }
    }
}
