import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 상태 연결을 확인하는 임시 캐릭터 표시. 최종 그림은 이 컴포넌트에서 교체한다.
Rectangle {
    id: badge
    required property string stateKey
    required property string message
    property bool dark: false
    readonly property var theme: UiTheme.colors(dark)
    property var animation: ({})
    property bool playbackEnabled: true
    property real userScale: 0.5
    property point userPivotOffset: Qt.point(0, 0)
    implicitHeight: 156
    radius: 12
    color: theme.surface
    border.color: theme.border
    Accessible.name: message

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: UiMetrics.spaceCompact
        spacing: UiMetrics.spaceSmall
        Item {
            Layout.alignment: Qt.AlignHCenter
            Layout.minimumHeight: 64
            width: 64; height: 64
            CharacterSprite {
                id: sprite
                objectName: "characterSprite"
                anchors.fill: parent
                descriptor: badge.animation
                playing: badge.playbackEnabled && badge.visible
                userScale: badge.userScale
                userPivotOffset: badge.userPivotOffset
            }
            Rectangle {
                anchors.centerIn: parent
                visible: !sprite.ready
                width: 56; height: 48; radius: 17
                color: badge.stateKey === "working" ? badge.theme.editorAccent
                     : badge.stateKey === "calling" ? badge.theme.warning : badge.theme.success
                Row {
                    anchors.centerIn: parent
                    spacing: UiMetrics.spaceGroup
                    Repeater {
                        model: 2
                        Rectangle {
                            width: 7
                            height: badge.stateKey === "resting" ? 2 : 8
                            radius: 3
                            color: UiTheme.light.text
                        }
                    }
                }
                Text {
                    anchors.left: parent.right
                    anchors.top: parent.top
                    text: badge.stateKey === "resting" ? "z" : badge.stateKey === "calling" ? "!" : "…"
                    color: badge.theme.text
                    font.bold: true
                }
            }
        }
        Label {
            Layout.fillWidth: true
            text: badge.stateKey === "working" ? qsTr("작업 중")
                : badge.stateKey === "calling" ? qsTr("말 걸기") : qsTr("휴식")
            horizontalAlignment: Text.AlignHCenter
            color: badge.theme.text
            font.bold: true
        }
        Label {
            Layout.fillWidth: true
            text: sprite.error.length > 0 ? sprite.error : badge.message
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            font.pixelSize: UiMetrics.textCompact
            color: badge.theme.muted
        }
    }
}
