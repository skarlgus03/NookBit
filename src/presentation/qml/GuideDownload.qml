import QtQuick
import QtQuick.Dialogs as NativeDialogs

Item {
    id: download
    required property var service
    property string guideKey: "rules"
    property string status: ""
    visible: false

    // 문서의 기본 이름을 설정하고 저장할 위치를 선택하는 창을 여는 함수.
    function saveDocument() {
        status = ""
        service.clearError()
        saveDialog.selectedFile = service.suggestedFile(guideKey)
        saveDialog.open()
    }
    // 소유 페이지를 떠날 때 저장하지 않고 파일 선택창만 닫는 함수.
    function closeDialog() { saveDialog.close() }

    NativeDialogs.FileDialog {
        id: saveDialog
        objectName: "guideSaveDialog"
        title: download.guideKey === "rules" ? qsTr("AI용 규칙 작성 문서 저장") : qsTr("캐릭터팩 제작 가이드 저장")
        fileMode: NativeDialogs.FileDialog.SaveFile
        nameFilters: [qsTr("Markdown 문서 (*.md)")]
        defaultSuffix: "md"
        onAccepted: {
            download.status = download.service.save(download.guideKey, selectedFile)
                ? qsTr("문서를 저장했어요.") : download.service.error
        }
    }
}
