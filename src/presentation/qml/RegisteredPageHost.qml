import QtQuick

// 등록된 화면을 명시적인 입력으로 생성하고 공통 테마·페이지 전환만 연결한다.
Loader {
    id: host
    required property var registration
    required property var guideService
    property Item overlayHost: parent
    property bool dark: false
    property bool selected: false
    property bool initialized: false
    signal pageRequested(int page)
    signal closeRequested(int page)
    signal navigationRequested(int page)
    active: !!registration && registration.page > 0
    visible: selected

    // 기능이 제공한 입력과 호스트 공통 서비스를 합쳐 페이지를 생성하는 함수.
    function loadPage() {
        if (!registration || !(registration.page > 0) || !registration.source) { source = ""; return }
        const inputs = Object.assign({}, registration.properties,
            {guideService: guideService, overlayHost: overlayHost, dark: dark, visible: selected})
        setSource(registration.source, inputs)
    }
    Component.onCompleted: { initialized = true; loadPage() }
    onRegistrationChanged: if (initialized) loadPage()
    Binding { target: host.item; property: "dark"; value: host.dark; when: host.item !== null }
    Binding { target: host.item; property: "visible"; value: host.selected; when: host.item !== null }
    Connections {
        target: host.item
        // 페이지의 열기 요청을 해당 등록 번호로만 전달하는 함수.
        function onOpenRequested() { host.pageRequested(host.registration.page) }
        // 페이지의 닫기 요청을 해당 등록 번호로만 전달하는 함수.
        function onCloseRequested() { host.closeRequested(host.registration.page) }
        // 한 기능의 화면이 요청한 다른 페이지 이동을 호스트에 전달하는 함수.
        function onRouteRequested(page) { host.navigationRequested(page) }
    }
}
