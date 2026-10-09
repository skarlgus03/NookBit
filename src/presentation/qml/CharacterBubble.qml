import QtQuick
import QtQuick.Window

// 화면 기준점만 받아 배치하며 캐릭터나 파일 작업을 알지 않는 말풍선.
Window {
    id: bubble
    objectName: "characterBubbleWindow"
    required property point anchorPoint
    required property rect screenArea
    required property string message
    property bool actionAvailable: false
    property string actionText: qsTr("확인")
    signal actionRequested()
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    title: qsTr("NookBit 말풍선")
    width: Math.min(270, Math.max(100, screenArea.width - 16))
    height: Math.min(screenArea.height - 16, Math.max(64, bodyText.implicitHeight + 34 + (actionAvailable ? 38 : 0)))
    readonly property bool flippedLeft: anchorPoint.x + width + 12 > screenArea.x + screenArea.width - 8
    readonly property bool belowAnchor: anchorPoint.y - height - 8 < screenArea.y + 8
    x: Math.round(Math.max(screenArea.x + 8, Math.min(screenArea.x + screenArea.width - width - 8,
        flippedLeft ? anchorPoint.x - width - 12 : anchorPoint.x + 12)))
    y: Math.round(Math.max(screenArea.y + 8, Math.min(screenArea.y + screenArea.height - height - 8,
        belowAnchor ? anchorPoint.y + 8 : anchorPoint.y - height - 8)))
    Rectangle {
        width: 12; height: 12
        x: Math.max(16, Math.min(bubble.width - 28, bubble.anchorPoint.x - bubble.x - 6))
        y: bubble.belowAnchor ? 5 : bubble.height - 17
        rotation: 45
        color: "#263246"
        border.color: "#718298"
    }
    Rectangle {
        x: 1; width: bubble.width - 2
        y: bubble.belowAnchor ? 10 : 0
        height: bubble.height - 10
        radius: 14
        color: "#263246"
        border.color: "#718298"
        Text {
            id: bodyText
            x: 14; y: 12; width: parent.width - 28
            text: bubble.message
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            maximumLineCount: 5
            elide: Text.ElideRight
            font.pixelSize: 13
            color: "#edf2f8"
        }
        Rectangle {
            objectName: "characterBubbleAction"
            visible: bubble.actionAvailable
            x: parent.width - width - 12; y: parent.height - height - 10
            width: 64; height: 28; radius: 8
            color: "#33475e"
            border.color: actionMouse.containsMouse ? "#b6d8c5" : "#718298"
            Text { anchors.centerIn: parent; text: bubble.actionText; color: "#edf2f8"; font.pixelSize: 12 }
            MouseArea {
                id: actionMouse
                anchors.fill: parent
                hoverEnabled: true
                // 확인 요청만 전달하고 파일 작업은 직접 실행하지 않는 함수.
                onClicked: bubble.actionRequested()
            }
        }
    }
}
