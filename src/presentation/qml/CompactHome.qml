import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 홈은 전달받은 상태 요약과 페이지 선택·창 조작만 제공한다.
Rectangle {
    id: home
    objectName: "compactHome"
    property bool dark: false
    property bool navigationEnabled: true
    property int pendingCount: 0
    property bool sortingAvailable: true
    property bool companionAvailable: true
    property bool watchEnabled: false
    property string watchFolder: ""
    property var companionDescriptor: ({})
    property var registeredPages: []
    property int currentPage: 0
    readonly property int selectedPage: [3, 6, 8, 9, 10, 12, 13].includes(currentPage) ? 8
        : navigationEntries.some(entry => entry.page === currentPage) ? currentPage : currentPage > 0 ? 14 : 0
    signal pageRequested(int page)
    signal historyRequested()
    signal moveRequested()
    signal minimizeRequested()
    signal closeRequested()
    readonly property var theme: UiTheme.colors(dark)
    readonly property color foreground: theme.text
    // 모듈이 요청한 바로가기 중 공통 개수 제한 안의 항목만 메인에 표시하는 함수형 바인딩.
    readonly property var navigationEntries: {
        const entries = []
        for (let i = 0; i < registeredPages.length; ++i)
            if (registeredPages[i].inMenu !== false && registeredPages[i].homeShortcut === true) entries.push(registeredPages[i])
        return entries.sort((left, right) => left.order - right.order || left.page - right.page).slice(0, UiMetrics.homeShortcutLimit)
    }
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
                HomeStatusCard {
                    Layout.fillWidth: true
                    Layout.bottomMargin: UiMetrics.spaceSmall
                    pendingCount: home.pendingCount
                    sortingAvailable: home.sortingAvailable
                    companionAvailable: home.companionAvailable
                    watchEnabled: home.watchEnabled
                    watchFolder: home.watchFolder
                    companionDescriptor: home.companionDescriptor
                    dark: home.dark
                    onSortRequested: home.pageRequested(7)
                }
                Repeater {
                    model: home.navigationEntries
                    delegate: ColumnLayout {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        spacing: UiMetrics.spaceControl
                        Label { visible: modelData.section.length > 0 && (index === 0 || home.navigationEntries[index - 1].section !== modelData.section); text: modelData.section; color: home.theme.homeMuted; font.pixelSize: UiMetrics.textGroupTitle; font.weight: UiMetrics.groupTitleWeight }
                        HomeMenuButton {
                            objectName: "homeNavigation" + modelData.page
                            selected: home.selectedPage === modelData.page
                            dark: home.dark
                            iconKind: modelData.icon
                            Layout.fillWidth: true
                            text: modelData.label
                            badgeText: modelData.page === 7 && home.pendingCount > 0 ? String(home.pendingCount) : ""
                            // 작업 기록은 조회 후 열고 나머지는 해당 페이지로 이동하는 함수.
                            onClicked: modelData.page === 4 ? home.historyRequested() : home.pageRequested(modelData.page)
                        }
                    }
                }
            }
        }
        ColumnLayout {
            objectName: "homeNavigationFooter"
            Layout.fillWidth: true
            enabled: home.navigationEnabled
            spacing: UiMetrics.spaceExtraSmall
            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: UiMetrics.homeFooterDividerSpacing
                Layout.bottomMargin: UiMetrics.homeFooterDividerSpacing
                implicitHeight: UiMetrics.homeFooterDividerThickness
                color: home.theme.homeFooterDivider
            }
            HomeMenuButton {
                objectName: "homeNavigation14"
                Layout.fillWidth: true
                text: qsTr("전체 기능 보기")
                iconKind: "modules"
                selected: home.selectedPage === 14
                dark: home.dark
                onClicked: home.pageRequested(14)
            }
            HomeMenuButton {
                objectName: "homeNavigation8"
                Layout.fillWidth: true
                text: qsTr("설정")
                iconKind: "settings"
                selected: home.selectedPage === 8
                dark: home.dark
                onClicked: home.pageRequested(8)
            }
        }
    }
}
