import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 활성 모듈의 진입점만 모아 보여주고 기능 실행 대신 페이지 이동을 요청하는 목록.
PageSurface {
    id: page
    objectName: "allFeaturesPage"
    required property var registeredPages
    readonly property var groups: {
        const entries = [{page: 12, label: qsTr("모듈 관리"), icon: "modules", section: qsTr("앱"), order: 55}]
        for (let i = 0; i < registeredPages.length; ++i)
            if (registeredPages[i].inMenu !== false) entries.push(registeredPages[i])
        entries.sort((left, right) => left.order - right.order || left.page - right.page)
        const result = []
        for (const entry of entries) {
            const title = entry.section || qsTr("기타 기능")
            let group = result.find(item => item.title === title)
            if (!group) { group = {title: title, entries: []}; result.push(group) }
            group.entries.push(entry)
        }
        return result
    }
    background: Rectangle { color: page.theme.window }
    ScrollView {
        id: scroll
        anchors.fill: parent
        padding: UiMetrics.pageInset
        contentWidth: availableWidth
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ColumnLayout {
            width: scroll.availableWidth
            spacing: UiMetrics.settingsSectionSpacing
            Repeater {
                model: page.groups
                delegate: ColumnLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: UiMetrics.spaceControl
                    Label {
                        Layout.fillWidth: true
                        text: modelData.title
                        textFormat: Text.PlainText
                        font.pixelSize: UiMetrics.textGroupTitle
                        font.weight: UiMetrics.groupTitleWeight
                        color: page.theme.muted
                    }
                    Repeater {
                        model: modelData.entries
                        delegate: HomeMenuButton {
                            required property var modelData
                            objectName: "featureNavigation" + modelData.page
                            Layout.fillWidth: true
                            text: modelData.label
                            iconKind: modelData.icon
                            dark: page.dark
                            onClicked: page.routeRequested(modelData.page)
                        }
                    }
                }
            }
        }
    }
}
