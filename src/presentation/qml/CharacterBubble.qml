import QtQuick
import QtQuick.Window

// 공통 말풍선 표시 부품을 바탕화면의 독립 창에 담는 창.
Window {
    id: bubble
    objectName: "characterBubbleWindow"
    required property point anchorPoint
    required property rect screenArea
    required property string message
    property var configuration: ({})
    property bool dark: true
    property bool actionAvailable: false
    property string actionText: qsTr("확인")
    signal actionRequested()
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    title: qsTr("NookBit 말풍선")
    width: view.width
    height: view.height
    x: view.placementX
    y: view.placementY
    readonly property bool customReady: view.customReady
    readonly property bool customSkin: view.customSkin
    readonly property point effectivePivot: view.effectivePivot
    readonly property bool flippedLeft: view.flippedLeft
    readonly property bool belowAnchor: view.belowAnchor
    readonly property var textArea: view.textArea
    readonly property var actionArea: view.actionArea
    CharacterBubbleView {
        id: view
        anchorPoint: bubble.anchorPoint
        screenArea: bubble.screenArea
        message: bubble.message
        configuration: bubble.configuration
        dark: bubble.dark
        actionAvailable: bubble.actionAvailable
        actionText: bubble.actionText
        onActionRequested: bubble.actionRequested()
    }
}
