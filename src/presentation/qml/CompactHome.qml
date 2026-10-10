import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T

// 홈은 기능 실행 없이 페이지 선택과 창 조작만 제공한다.
Rectangle {
    id: home
    objectName: "compactHome"
    property bool dark: false
    signal pageRequested(int page)
    signal historyRequested()
    signal moveRequested()
    signal minimizeRequested()
    signal closeRequested()
    readonly property color foreground: dark ? "#edf2f8" : "#273344"
    color: dark ? "#1b2230" : "#f5f7fa"
    radius: 8
    border.color: dark ? "#3d4757" : "#e1e6ed"
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            Item {
                Layout.fillWidth: true; implicitHeight: 38
                Text { anchors.verticalCenter: parent.verticalCenter; text: "NookBit"; font.pixelSize: 25; font.bold: true; color: home.foreground }
                MouseArea {
                    anchors.fill: parent; cursorShape: Qt.SizeAllCursor
                    // 제목 영역에서 본체 창 이동을 요청하는 함수.
                    onPressed: home.moveRequested()
                }
            }
            Button { text: "−"; implicitWidth: 32; implicitHeight: 32; flat: true; Accessible.name: qsTr("최소화"); onClicked: home.minimizeRequested() }
            Button { text: "×"; implicitWidth: 32; implicitHeight: 32; flat: true; Accessible.name: qsTr("트레이로 숨기기"); onClicked: home.closeRequested() }
        }
        Label { text: qsTr("필요한 페이지를 열어보세요."); color: dark ? "#aab6c7" : "#748091"; font.pixelSize: 14 }
        Item { Layout.preferredHeight: 14 }
        Repeater {
            model: [{label: qsTr("파일 정리"), page: 7}, {label: qsTr("분류 규칙"), page: 1}, {label: qsTr("작업 기록"), page: 4}, {label: qsTr("설정"), page: 8}]
            delegate: Button {
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: 48
                text: modelData.label + "  ›"
                // 작업 기록은 새로 조회하고 나머지는 선택한 페이지를 여는 함수.
                onClicked: modelData.page === 4 ? home.historyRequested() : home.pageRequested(modelData.page)
            }
        }
        Item { Layout.fillHeight: true }
    }
}
