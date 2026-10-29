import QtQuick

// 선택적인 바탕화면 기여를 만들고 공통 테마와 외부 요청만 전달하는 호스트.
Loader {
    id: host
    required property var contribution
    property bool dark: false
    property bool attentionActionAvailable: false
    property bool initialized: false
    readonly property var summary: item ? item.summary : ({})
    signal attentionRequested()
    signal pageRequested(string pageId)
    active: !!contribution && !!contribution.source

    // 기여가 없으면 창을 만들지 않고 제공된 입력으로만 표시기를 만드는 함수.
    function loadDesktop() {
        if (!contribution || !contribution.source) { source = ""; return }
        const inputs = Object.assign({}, contribution.properties,
            {dark: dark, attentionActionAvailable: attentionActionAvailable})
        setSource(contribution.source, inputs)
    }
    // 표시기가 존재할 때만 종료 직전 위치 확정을 요청하는 함수.
    function commitState() { if (item) item.commitState() }
    // 개발용 미리보기 요청을 표시기 내부로 전달하는 함수.
    function preview() { if (item) item.preview() }

    Component.onCompleted: { initialized = true; loadDesktop() }
    onContributionChanged: if (initialized) loadDesktop()
    Binding { target: host.item; property: "dark"; value: host.dark; when: host.item !== null }
    Binding { target: host.item; property: "attentionActionAvailable"; value: host.attentionActionAvailable; when: host.item !== null }
    Connections {
        target: host.item
        // 실제 알림 처리나 파일 명령을 실행하지 않고 확인 요청만 바깥으로 전달하는 함수.
        function onAttentionRequested() { host.attentionRequested() }
        // 표시기가 지정한 안정적인 페이지 이름을 호스트에 전달하는 함수.
        function onPageRequested(pageId) { host.pageRequested(pageId) }
    }
}
