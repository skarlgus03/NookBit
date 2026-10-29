import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 적용된 조건은 입력받고 화면 초안과 조회 요청만 관리하는 기록 검색 도구.
ColumnLayout {
    id: bar
    property var state: ({})
    property bool busy: false
    property bool dark: false
    signal searchRequested(string text, string period, string result, string order, string from, string to)
    signal resetRequested()
    spacing: UiMetrics.spaceControl
    // 확정된 조회 조건으로 화면 입력값을 복원하는 함수.
    function restore() {
        keyword.text = state.text || ""
        periodChoice.currentIndex = Math.max(0, periodChoice.indexOfValue(state.period || "all"))
        resultChoice.currentIndex = Math.max(0, resultChoice.indexOfValue(state.result || "all"))
        orderChoice.currentIndex = Math.max(0, orderChoice.indexOfValue(state.order || "newest"))
        fromInput.text = state.from || ""
        toInput.text = state.to || ""
    }
    // 현재 입력값을 한 번에 전달하고 실제 조회는 외부 창구에 맡기는 함수.
    function search() {
        bar.searchRequested(keyword.text, periodChoice.currentValue, resultChoice.currentValue,
                            orderChoice.currentValue, fromInput.text, toInput.text)
    }
    onStateChanged: Qt.callLater(restore)
    Component.onCompleted: restore()
    enabled: !busy
    RowLayout {
        Layout.fillWidth: true
        spacing: UiMetrics.spaceControl
        AppTextField {
            id: keyword
            objectName: "historySearchInput"
            Layout.fillWidth: true
            maximumLength: 1024
            placeholderText: qsTr("파일명 또는 경로 검색")
            Accessible.name: qsTr("작업 기록 검색어")
            onAccepted: bar.search()
        }
        AppButton {
            objectName: "historySearchButton"
            text: qsTr("검색")
            dark: bar.dark
            primary: true
            onClicked: bar.search()
        }
        AppButton {
            objectName: "historyResetButton"
            text: qsTr("초기화")
            dark: bar.dark
            onClicked: { bar.resetRequested(); Qt.callLater(bar.restore) }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: UiMetrics.spaceControl
        AppComboBox {
            id: periodChoice
            objectName: "historyPeriodFilter"
            Layout.fillWidth: true
            textRole: "label"; valueRole: "id"
            model: [{id:"all",label:qsTr("전체 기간")}, {id:"today",label:qsTr("오늘")},
                {id:"7days",label:qsTr("최근 7일")}, {id:"30days",label:qsTr("최근 30일")}, {id:"custom",label:qsTr("직접 선택")}]
            Accessible.name: qsTr("기록 기간")
            onActivated: {
                if (currentValue === "custom") {
                    const today = Qt.formatDate(new Date(), "yyyy-MM-dd")
                    if (!fromInput.text.length) fromInput.text = today
                    if (!toInput.text.length) toInput.text = today
                }
                bar.search()
            }
        }
        AppComboBox {
            id: resultChoice
            objectName: "historyResultFilter"
            Layout.fillWidth: true
            textRole: "label"; valueRole: "id"
            model: [{id:"all",label:qsTr("전체 결과")}, {id:"completed",label:qsTr("이동 완료")},
                {id:"rolledBack",label:qsTr("되돌림 완료")}, {id:"problem",label:qsTr("문제 있음")}]
            Accessible.name: qsTr("작업 결과")
            onActivated: bar.search()
        }
        AppComboBox {
            id: orderChoice
            objectName: "historySortOrder"
            Layout.fillWidth: true
            textRole: "label"; valueRole: "id"
            model: [{id:"newest",label:qsTr("최신순")}, {id:"oldest",label:qsTr("오래된순")}]
            Accessible.name: qsTr("기록 정렬")
            onActivated: bar.search()
        }
    }
    RowLayout {
        Layout.fillWidth: true
        visible: periodChoice.currentValue === "custom"
        spacing: UiMetrics.spaceControl
        AppTextField {
            id: fromInput
            objectName: "historyFromDate"
            Layout.fillWidth: true
            placeholderText: qsTr("시작일 YYYY-MM-DD")
            maximumLength: 10
            Accessible.name: qsTr("시작일")
            onAccepted: bar.search()
        }
        Label { text: "~"; color: UiTheme.colors(bar.dark).muted }
        AppTextField {
            id: toInput
            objectName: "historyToDate"
            Layout.fillWidth: true
            placeholderText: qsTr("종료일 YYYY-MM-DD")
            maximumLength: 10
            Accessible.name: qsTr("종료일")
            onAccepted: bar.search()
        }
        Label { text: qsTr("종료일 포함"); color: UiTheme.colors(bar.dark).muted; font.pixelSize: UiMetrics.textBody }
    }
}
