import QtQuick
import QtQuick.Dialogs as NativeDialogs

Item {
    id: download
    property string guideKey: "rules"
    property string status: ""
    visible: false

    // 문서의 기본 이름을 설정하고 저장할 위치를 선택하는 창을 여는 함수.
    function saveDocument() {
        status = ""
        userGuides.clearError()
        saveDialog.selectedFile = userGuides.suggestedFile(guideKey)
        saveDialog.open()
    }

    NativeDialogs.FileDialog {
        id: saveDialog
        objectName: "guideSaveDialog"
        title: download.guideKey === "rules" ? qsTr("AI용 규칙 작성 문서 저장") : qsTr("캐릭터팩 제작 가이드 저장")
        fileMode: NativeDialogs.FileDialog.SaveFile
        nameFilters: [qsTr("Markdown 문서 (*.md)")]
        defaultSuffix: "md"
        onAccepted: {
            download.status = userGuides.save(download.guideKey, selectedFile)
                ? qsTr("문서를 저장했어요.") : userGuides.error
        }
    }
}
