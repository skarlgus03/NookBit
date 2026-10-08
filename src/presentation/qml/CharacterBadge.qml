import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 상태 연결을 확인하는 임시 캐릭터 표시. 최종 그림은 이 컴포넌트에서 교체한다.
Rectangle {
    id: badge
    required property string stateKey
    required property string message
    property bool dark: false
    implicitHeight: 132
    radius: 12
    color: dark ? "#263246" : "#edf2e9"
    border.color: dark ? "#46566e" : "#cfdbcb"
    Accessible.name: message

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 5
        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            width: 56; height: 48; radius: 17
            color: badge.stateKey === "working" ? "#91b5db"
                 : badge.stateKey === "calling" ? "#e9bf7c" : "#a6c1a2"
            Row {
                anchors.centerIn: parent
                spacing: 12
                Repeater {
                    model: 2
                    Rectangle {
                        width: 7
                        height: badge.stateKey === "resting" ? 2 : 8
                        radius: 3
                        color: "#283b3a"
                    }
                }
            }
            Text {
                anchors.left: parent.right
                anchors.top: parent.top
                text: badge.stateKey === "resting" ? "z" : badge.stateKey === "calling" ? "!" : "…"
                color: badge.dark ? "#e6edf7" : "#374b3b"
                font.bold: true
            }
        }
        Label {
            Layout.fillWidth: true
            text: badge.stateKey === "working" ? qsTr("작업 중")
                : badge.stateKey === "calling" ? qsTr("말 걸기") : qsTr("휴식")
            horizontalAlignment: Text.AlignHCenter
            color: badge.dark ? "#edf2f8" : "#253428"
            font.bold: true
        }
        Label {
            Layout.fillWidth: true
            text: badge.message
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            font.pixelSize: 11
            color: badge.dark ? "#bdcce0" : "#536854"
        }
    }
}
