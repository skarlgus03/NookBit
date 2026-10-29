import QtQuick

// 선택적인 기능 확인창을 명시적인 입력으로 만들고 공통 표시 상태만 전달하는 호스트.
Loader {
    id: host
    required property var contribution
    required property Item overlayHost
    property bool busy: false
    property bool dark: false
    property bool initialized: false
    readonly property bool opened: item ? item.opened : false
    signal accepted()
    active: !!contribution && !!contribution.source
    // 기능이 없으면 창을 만들지 않고 제공된 입력으로만 확인창 표시기를 만드는 함수.
    function loadDialog() {
        if (!contribution || !contribution.source) { source = ""; return }
        setSource(contribution.source, Object.assign({}, contribution.properties,
            {overlayHost: overlayHost, busy: busy, dark: dark}))
    }
    // 기능이 존재할 때만 창 열기 요청을 전달하는 함수.
    function open() { if (item) item.open() }
    // 창 닫기는 기능의 실제 명령을 호출하지 않는 함수.
    function close() { if (item) item.close() }
    Component.onCompleted: { initialized = true; loadDialog() }
    onContributionChanged: if (initialized) loadDialog()
    Binding { target: host.item; property: "busy"; value: host.busy; when: host.item !== null }
    Binding { target: host.item; property: "dark"; value: host.dark; when: host.item !== null }
    Connections {
        target: host.item
        // 기능 화면이 보고한 수락 완료만 바깥으로 전달하는 함수.
        function onAccepted() { host.accepted() }
    }
}
