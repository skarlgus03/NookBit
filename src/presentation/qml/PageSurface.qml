import QtQuick
import QtQuick.Controls

// 페이지 내용만 소유하며 열기·닫기는 바깥의 화면 연결부에 요청하는 바탕.
Pane {
    readonly property bool opened: visible
    property bool dark: false
    readonly property var theme: UiTheme.colors(dark)
    signal openRequested
    signal closeRequested
    signal routeRequested(int page)
    background: null
    padding: 0

    // 화면 연결부에 이 페이지를 열도록 요청하는 함수.
    function open() {
        openRequested();
    }
    // 화면 연결부에 이 페이지를 접도록 요청하는 함수.
    function close() {
        closeRequested();
    }
}
