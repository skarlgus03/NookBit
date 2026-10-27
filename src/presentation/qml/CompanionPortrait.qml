import QtQuick

// 원본 프레임·피벗·미러를 존중하며 현재 팩의 정지 그림을 상자 안에 맞추는 미리보기.
Rectangle {
    id: portrait
    objectName: "homeCompanionPortrait"
    property var descriptor: ({})
    property bool dark: false
    readonly property var theme: UiTheme.colors(dark)
    implicitWidth: UiMetrics.homePortraitSize
    implicitHeight: UiMetrics.homePortraitSize
    radius: UiMetrics.radiusCard
    color: theme.editorTint
    clip: true

    CharacterSprite {
        id: sprite
        objectName: "homeCompanionSprite"
        anchors.fill: parent
        descriptor: portrait.descriptor
        playing: false
        readonly property real frameWidth: metadataValid ? renderedStep.region.width / renderedStep.pixelsPerUnit : 0
        readonly property real frameHeight: metadataValid ? renderedStep.region.height / renderedStep.pixelsPerUnit : 0
        userScale: metadataValid ? Math.max(0.001, Math.min((width - UiMetrics.spaceExtraSmall * 2) / frameWidth,
                                                          (height - UiMetrics.spaceExtraSmall * 2) / frameHeight)) : 1
        anchor: metadataValid
            ? Qt.point((width - frameWidth * userScale) / 2
                       + (renderedMirrorX ? frameWidth - renderedStep.pivot.x : renderedStep.pivot.x) * userScale,
                       (height - frameHeight * userScale) / 2 + renderedStep.pivot.y * userScale)
            : Qt.point(width / 2, height / 2)
    }
    NavigationIcon {
        anchors.centerIn: parent
        width: UiMetrics.navigationIconSize
        height: UiMetrics.navigationIconSize
        kind: "companion"
        strokeColor: portrait.theme.editorAccent
        visible: !sprite.ready && !sprite.retainingFrame
    }
}
