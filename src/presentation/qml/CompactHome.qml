import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T

// 홈은 기능 실행 없이 페이지 선택과 창 조작만 제공한다.
Rectangle {
    id: home
    objectName: "compactHome"
    property bool dark: false
    property bool navigationEnabled: true
    property int currentPage: 0
    readonly property int selectedPage: [3, 5, 6, 8, 9, 10].includes(currentPage) ? 8 : currentPage
    signal pageRequested(int page)
    signal historyRequested()
    signal moveRequested()
    signal minimizeRequested()
    signal closeRequested()
    readonly property var theme: UiTheme.colors(dark)
    readonly property color foreground: theme.text
    color: theme.home
    radius: UiMetrics.radiusControl
    border.color: theme.border
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: UiMetrics.homeInset
        spacing: UiMetrics.spaceGroup
        RowLayout {
            Layout.fillWidth: true
            Item {
                Layout.fillWidth: true; implicitHeight: UiMetrics.controlHeight
                Text { anchors.verticalCenter: parent.verticalCenter; text: "NookBit"; font.pixelSize: UiMetrics.textAppTitle; font.bold: true; color: home.foreground }
                MouseArea {
                    objectName: "homeWindowDragArea"
                    anchors.fill: parent; cursorShape: Qt.SizeAllCursor
                    // 제목 영역에서 본체 창 이동을 요청하는 함수.
                    onPressed: home.moveRequested()
                }
            }
            AppButton { text: "−"; implicitWidth: UiMetrics.windowButtonSize; implicitHeight: UiMetrics.windowButtonSize; flat: true; Accessible.name: qsTr("최소화"); onClicked: home.minimizeRequested() }
            AppButton { text: "×"; implicitWidth: UiMetrics.windowButtonSize; implicitHeight: UiMetrics.windowButtonSize; flat: true; Accessible.name: qsTr("트레이로 숨기기"); onClicked: home.closeRequested() }
        }
        ScrollView {
            id: menuScroll
            objectName: "homeNavigationMenu"
            enabled: home.navigationEnabled
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                width: menuScroll.availableWidth
                spacing: UiMetrics.spaceGroup
                Repeater {
                    model: [
                        {label: qsTr("파일 정리"), page: 7, icon: "folder", section: qsTr("파일 관리")},
                        {label: qsTr("분류 규칙"), page: 1, icon: "rules", section: ""},
                        {label: qsTr("작업 기록"), page: 4, icon: "history", section: ""},
                        {label: qsTr("설정"), page: 8, icon: "settings", section: qsTr("앱")}
                    ]
                    delegate: ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: UiMetrics.spaceControl
                        Label { visible: modelData.section.length > 0; text: modelData.section; color: home.theme.homeMuted; font.pixelSize: UiMetrics.textGroupTitle; font.weight: UiMetrics.groupTitleWeight }
                        HomeMenuButton {
                            objectName: "homeNavigation" + modelData.page
                            selected: home.selectedPage === modelData.page
                            dark: home.dark
                            iconKind: modelData.icon
                            Layout.fillWidth: true
                            text: modelData.label
                            // 작업 기록은 조회 후 열고 나머지는 해당 페이지로 이동하는 함수.
                            onClicked: modelData.page === 4 ? home.historyRequested() : home.pageRequested(modelData.page)
                        }
                    }
                }
            }
        }
    }
}
