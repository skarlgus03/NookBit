#include "presentation/pending_files_model.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <iostream>
#include <string>

namespace {

int failures = 0;

// 조건이 거짓이면 테스트 실패를 기록하는 함수.
void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

// 등록·중복 방지·제외가 원본 파일을 건드리지 않는지 검사하는 함수.
void testPendingFilesModel() {
    QTemporaryDir directory;
    expect(directory.isValid(), "temporary directory should be available");
    if (!directory.isValid()) return;
    const auto path = directory.filePath(QStringLiteral("report.pdf"));
    QFile file(path);
    expect(file.open(QIODevice::WriteOnly), "fixture file should open");
    if (!file.isOpen()) return;
    file.write("original content");
    file.close();

    nookbit::PendingFilesModel model;
    model.addUrl(QUrl::fromLocalFile(path));
    expect(model.count() == 1, "dropped file should enter the pending list");
    expect(model.data(model.index(0), nookbit::PendingFilesModel::FileNameRole)
               .toString() == QStringLiteral("report.pdf"),
           "file name should be shown in the list");
    expect(QFile::exists(path), "adding a file must not move or delete the original");

    model.addUrl(QUrl::fromLocalFile(path));
    expect(model.count() == 1, "dropping the same file twice should not duplicate it");
    model.addUrl(QUrl(QStringLiteral("https://example.com/file.pdf")));
    expect(model.count() == 1, "remote URLs should not enter the pending list");
    model.addUrl(QUrl::fromLocalFile(directory.path()));
    expect(model.count() == 1, "a folder should not enter the file-only pending list");

    model.removeAt(0);
    expect(model.count() == 0, "remove should clear only the pending entry");
    expect(QFile::exists(path), "removing an entry must leave the original file intact");
}

} // namespace

// 대기 목록 모델 테스트를 실행하는 함수.
int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    testPendingFilesModel();
    if (failures == 0) {
        std::cout << "All pending-files-model tests passed.\n";
        return 0;
    }
    std::cerr << failures << " pending-files-model assertion(s) failed.\n";
    return 1;
}
