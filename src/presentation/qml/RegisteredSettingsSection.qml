import QtQuick

// 기능의 선택적인 설정 영역을 생성하고 공통 테마·슬롯·화면 이동만 연결한다.
Loader {
    id: host
    required property var contribution
    property bool dark: false
    property Component leadingEntry
    property bool initialized: false
    signal routeRequested(int page)
    active: !!contribution && !!contribution.source
    visible: active && item !== null && item.hasContent

    // 기능 입력과 호스트의 공통 표시 서비스로 설정 영역을 생성하는 함수.
    function loadSection() {
        if (!contribution || !contribution.source) { source = ""; return }
        setSource(contribution.source, Object.assign({}, contribution.properties, {dark: dark, leadingEntry: leadingEntry}))
    }
    Component.onCompleted: { initialized = true; loadSection() }
    onContributionChanged: if (initialized) loadSection()
    Binding { target: host.item; property: "dark"; value: host.dark; when: host.item !== null }
    Connections {
        target: host.item
        // 설정 영역의 요청은 파일 작업 없이 바깥의 페이지 이동으로만 전달하는 함수.
        function onRouteRequested(page) { host.routeRequested(page) }
    }
}
