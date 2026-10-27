import QtQuick
import QtQuick.Controls

// 파일을 읽지 않고 확장자의 종류와 공통 의미색을 표시하는 배지.
Rectangle {
    id: badge
    property string fileName: ""
    property bool dark: false
    readonly property var theme: UiTheme.colors(dark)
    readonly property string extension: {
        const name = fileName.replace(/\\/g, "/").split("/").pop()
        const dot = name.lastIndexOf(".")
        return dot > 0 ? name.slice(dot + 1).toLowerCase() : ""
    }
    readonly property string category: categoryFor(extension)
    readonly property var swatch: theme.fileTypes[category]
    implicitWidth: UiMetrics.historyTypeTileSize
    implicitHeight: UiMetrics.historyTypeTileSize
    color: swatch.background
    radius: UiMetrics.radiusControl
    Accessible.role: Accessible.StaticText
    Accessible.name: extension.length > 0 ? extension.toUpperCase() : qsTr("파일")

    // 표시 전용 확장자를 종류로 묶으며 실제 분류 규칙과 독립적으로 판단하는 함수.
    function categoryFor(suffix) {
        if (["png", "jpg", "jpeg", "gif", "webp", "bmp", "svg", "tif", "tiff", "ico", "heic", "heif", "avif"].includes(suffix)) return "image"
        if (["pdf", "doc", "docx", "hwp", "hwpx", "txt", "rtf", "odt", "md", "ppt", "pptx", "odp", "xls", "xlsx", "csv", "ods", "epub"].includes(suffix)) return "document"
        if (["zip", "7z", "rar", "tar", "gz", "tgz", "bz2", "xz", "zst", "jar"].includes(suffix)) return "archive"
        if (["mp4", "mkv", "avi", "mov", "webm", "wmv", "m4v", "mpeg", "mpg"].includes(suffix)) return "video"
        if (["mp3", "wav", "flac", "aac", "ogg", "m4a", "opus", "wma"].includes(suffix)) return "audio"
        return "other"
    }
    Label {
        anchors.centerIn: parent
        width: parent.width - UiMetrics.spaceTiny * 2
        text: badge.Accessible.name
        textFormat: Text.PlainText
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
        color: badge.swatch.foreground
        font.pixelSize: UiMetrics.textBody
        font.bold: true
    }
}
