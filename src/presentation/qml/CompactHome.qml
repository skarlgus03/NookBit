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
    readonly property int selectedPage: [3, 5, 6, 8, 9].includes(currentPage) ? 8 : currentPage
    signal pageRequested(int page)
    signal historyRequested()
    signal moveRequested()
    signal minimizeRequested()
    signal closeRequested()
    readonly property color foreground: dark ? "#edf2f8" : "#273344"
    color: dark ? "#1b2230" : "#f5f7fa"
    radius: UiMetrics.radiusControl
    border.color: dark ? "#3d4757" : "#e1e6ed"
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
                        Label { visible: modelData.section.length > 0; text: modelData.section; color: home.dark ? "#aab6c7" : "#748091"; font.pixelSize: UiMetrics.textCaption }
                        AppButton {
                            id: navigation
                            objectName: "homeNavigation" + modelData.page
                            readonly property bool selected: home.selectedPage === modelData.page
                            Layout.fillWidth: true
                            implicitHeight: UiMetrics.mainNavigationHeight
                            text: modelData.label
                            Accessible.name: text
                            Accessible.description: selected ? qsTr("현재 페이지") : ""
                            contentItem: RowLayout {
                                spacing: UiMetrics.spaceGroup
                                NavigationIcon {
                                    kind: modelData.icon
                                    Layout.preferredWidth: UiMetrics.navigationIconSize
                                    Layout.preferredHeight: UiMetrics.navigationIconSize
                                    strokeColor: home.foreground
                                }
                                Label { Layout.fillWidth: true; text: navigation.text; color: home.foreground; font.pixelSize: UiMetrics.textNavigation; font.weight: navigation.selected ? Font.DemiBold : Font.Normal }
                            }
                            background: Rectangle {
                                radius: UiMetrics.radiusControl
                                color: navigation.selected ? (home.dark ? "#2d3d55" : "#e6eef7") : "transparent"
                                border.width: UiMetrics.borderNormal
                                border.color: navigation.hovered || navigation.visualFocus ? "#7898bb" : home.dark ? "#3d4757" : "#d7dfe9"
                                Behavior on color { ColorAnimation { duration: UiMetrics.selectionFadeDuration } }
                            }
                            // 작업 기록은 조회 후 열고 나머지는 해당 페이지로 이동하는 함수.
                            onClicked: modelData.page === 4 ? home.historyRequested() : home.pageRequested(modelData.page)
                        }
                    }
                }
            }
        }
    }
}
