#include "presentation/pending_files_model.hpp"
#include "app/pending_queue_adapter.hpp"
#include "infrastructure/configuration/json_pending_files_repository.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QDir>
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
    expect(model.count() == 1, "a parent folder must not overlap its already queued child");

    model.removeAt(0);
    expect(model.count() == 0, "remove should clear only the pending entry");
    expect(QFile::exists(path), "removing an entry must leave the original file intact");
    model.addUrl(QUrl::fromLocalFile(directory.path()));
    expect(model.count() == 1 && model.data(model.index(0), nookbit::PendingFilesModel::DirectoryRole).toBool(),
           "a folder is one typed pending target");
    model.addUrl(QUrl::fromLocalFile(path));
    expect(model.count() == 1, "a child must not be added after its parent folder");
}

// 여러 종류의 대기 항목을 비워도 원본과 폴더 내부가 보존되고 알림이 한 번만 발생하는지 검사하는 함수.
void testClearPendingQueue() {
    QTemporaryDir directory;
    if (!directory.isValid()) { expect(false, "clear fixture should exist"); return; }
    const auto source = directory.filePath("original.txt");
    const auto folder = directory.filePath("bundle");
    expect(QDir().mkpath(folder), "clear folder fixture");
    const auto nested = QDir(folder).filePath("nested.txt");
    for (const auto& path : {source, nested}) {
        QFile file(path);
        expect(file.open(QIODevice::WriteOnly) && file.write("original content") == 16, "clear source fixture");
    }
    nookbit::PendingFilesModel model;
    model.restorePaths({source, folder, directory.filePath("missing.txt")});
    expect(model.count() == 3 && model.unavailableCount() == 1, "clear includes files, folders and unavailable entries");
    int resets{}, changes{};
    QObject::connect(&model, &QAbstractItemModel::modelReset, &model, [&] { ++resets; });
    QObject::connect(&model, &nookbit::PendingFilesModel::countChanged, &model, [&] { ++changes; });
    model.clear();
    expect(model.count() == 0 && model.filePaths().isEmpty() && model.unavailableCount() == 0 && resets == 1 && changes == 1,
        "bulk clear empties the queue with one invalidation and one persistence notification");
    model.clear();
    expect(resets == 1 && changes == 1, "clearing an empty queue has no extra notifications or writes");
    for (const auto& path : {source, nested}) {
        QFile file(path);
        expect(file.open(QIODevice::ReadOnly) && file.readAll() == "original content", "clearing preserves original files and folder contents");
    }
}

// 목록을 재시작 복원하고 없는 파일·중복·빈 목록을 안전하게 처리하는지 검사하는 함수.
void testPendingPersistence() {
    QTemporaryDir directory;
    expect(directory.isValid(), "persistence fixture should exist");
    if (!directory.isValid()) return;
    const auto source = directory.filePath(QStringLiteral("사진.jpg"));
    const auto missing = directory.filePath(QStringLiteral("missing.txt"));
    QFile file(source);
    expect(file.open(QIODevice::WriteOnly), "source should open");
    if (!file.isOpen()) return;
    file.write("original");
    file.close();
    const auto storage = directory.filePath(QStringLiteral("state/pending-files.json"));
    nookbit::JsonPendingFilesRepository repository(storage);
    expect(repository.load().hasValue() && repository.load().value().empty(),
           "first launch should restore an empty list");
    const std::vector<std::filesystem::path> paths{
        std::filesystem::path(source.toStdWString()),
        std::filesystem::path(missing.toStdWString()),
        std::filesystem::path(source.toStdWString()),
    };
    expect(repository.save(paths).hasValue(), "pending snapshot should save");
    nookbit::JsonPendingFilesRepository reopened(storage);
    const auto loaded = reopened.load();
    expect(loaded.hasValue() && loaded.value().size() == 3, "new repository should read saved paths");
    if (!loaded) return;
    QStringList restored;
    for (const auto& path : loaded.value()) restored.append(QString::fromStdWString(path.wstring()));
    nookbit::PendingFilesModel model;
    model.restorePaths(restored);
    expect(model.count() == 2 && model.unavailableCount() == 1,
           "restoration should deduplicate while preserving missing entries");
    expect(model.data(model.index(1), nookbit::PendingFilesModel::AvailableRole).toBool() == false,
           "missing file should be marked unavailable");
    expect(QFile::exists(source), "restoration must not move files");
    model.restorePaths(restored);
    expect(model.count() == 2, "restoration should not append duplicate snapshots");
    QFile reappeared(missing);
    expect(reappeared.open(QIODevice::WriteOnly), "missing fixture should reappear");
    reappeared.close();
    model.refreshAvailability();
    expect(model.unavailableCount() == 0, "reappearing file should become available");
    expect(QFile::remove(missing), "fixture should disappear again");
    model.refreshAvailability();
    expect(model.unavailableCount() == 1, "deleted file should become unavailable");
    // 목록 변경 신호에만 저장을 연결해 제거한 파일이 다시 복원되지 않는지 검사하는 함수.
    QObject::connect(&model, &nookbit::PendingFilesModel::countChanged, &model, [&] {
        std::vector<std::filesystem::path> snapshot;
        for (const auto& path : model.filePaths()) snapshot.emplace_back(path.toStdWString());
        expect(repository.save(snapshot).hasValue(), "changed list should save");
    });
    model.removeAt(1);
    expect(repository.load().hasValue() && repository.load().value().size() == 1,
           "removed missing entry must not return on restart");
    model.clear();
    expect(repository.load().hasValue() && repository.load().value().empty(),
           "cleared or fully moved list should remain empty after restart");
    model.addUrl(QUrl::fromLocalFile(source));
    expect(repository.load().hasValue() && repository.load().value().size() == 1,
           "new registration should persist without waiting for shutdown");
    model.setStorageNotice(QStringLiteral("save failed"));
    model.addUrl(QUrl::fromLocalFile(source));
    expect(model.storageNotice() == QStringLiteral("save failed"),
           "registration messages must not hide storage errors");
}

// 깨진 문서·지원하지 않는 버전·상대 경로를 거부하고 원본 저장 파일을 보존하는지 검사하는 함수.
void testPendingPersistenceFailures() {
    QTemporaryDir directory;
    if (!directory.isValid()) { expect(false, "failure fixture should exist"); return; }
    const auto storage = directory.filePath(QStringLiteral("pending-files.json"));
    nookbit::JsonPendingFilesRepository repository(storage);
    for (const auto& bytes : {QByteArray("broken"), QByteArray("{\"schemaVersion\":2,\"paths\":[]}"),
                             QByteArray("{\"schemaVersion\":1,\"paths\":[\"relative.txt\"]}"),
                             QByteArray("{\"schemaVersion\":1,\"paths\":[12]}")}) {
        QFile file(storage);
        expect(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "invalid fixture should open");
        file.write(bytes);
        file.close();
        expect(!repository.load(), "invalid pending document should fail as a whole");
        expect(file.open(QIODevice::ReadOnly) && file.readAll() == bytes,
               "failed restoration must not modify the existing document");
    }
    expect(repository.save({std::filesystem::path(L"relative.txt")}).hasValue() == false,
           "invalid snapshot must not overwrite storage");
    nookbit::JsonPendingFilesRepository blocked(directory.path());
    expect(!blocked.save({}), "writing to a directory must report failure");
}

// 외부 요청도 기존 등록 검사를 사용하고 경로별 결과와 독립적인 조회 값을 반환하는지 검사하는 함수.
void testPendingQueueAdapter() {
    QTemporaryDir directory;
    if (!directory.isValid()) { expect(false, "adapter fixture"); return; }
    QDir root(directory.path());
    expect(root.mkpath("group") && root.mkpath("bundle/empty"), "adapter folder fixtures");
    const auto first = std::filesystem::path(directory.filePath("group/first.txt").toStdWString());
    const auto second = std::filesystem::path(directory.filePath("second.txt").toStdWString());
    const auto bundle = std::filesystem::path(directory.filePath("bundle").toStdWString());
    for (const auto& path : {first, second}) {
        QFile file(QString::fromStdWString(path.wstring()));
        expect(file.open(QIODevice::WriteOnly) && file.write("source") == 6, "adapter source fixture");
    }
    nookbit::PendingFilesModel pending;
    nookbit::PendingQueueAdapter adapter(pending);
    const auto result = adapter.enqueuePaths({first, first, first.parent_path(),
        std::filesystem::path(directory.filePath("missing.txt").toStdWString()), second, bundle, L"relative.txt"});
    expect(result && result.value().added.size() == 3 && result.value().rejected.size() == 4,
        "batch records partial success without losing later valid paths");
    if (!result || result.value().rejected.size() != 4) return;
    expect(result.value().rejected[0].error.code == "pending.duplicate" &&
        result.value().rejected[1].error.code == "pending.overlap" &&
        result.value().rejected[2].error.code == "pending.path_unavailable" &&
        result.value().rejected[3].error.code == "pending.path_invalid", "rejection reasons remain associated with each input");
    auto snapshot = adapter.pendingPaths();
    expect(snapshot && snapshot.value() == result.value().added, "query returns normalized admitted paths");
    if (snapshot) snapshot.value().clear();
    expect(pending.count() == 3 && std::filesystem::exists(first) && std::filesystem::exists(second) &&
        std::filesystem::is_directory(bundle / "empty"), "query values cannot mutate the queue and enqueue never moves sources");
}
} // namespace

// 대기 목록 모델 테스트를 실행하는 함수.
int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    testPendingFilesModel();
    testClearPendingQueue();
    testPendingPersistence();
    testPendingPersistenceFailures();
    testPendingQueueAdapter();
    if (failures == 0) {
        std::cout << "All pending-files-model tests passed.\n";
        return 0;
    }
    std::cerr << failures << " pending-files-model assertion(s) failed.\n";
    return 1;
}
