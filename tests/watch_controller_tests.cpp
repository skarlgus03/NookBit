#include "presentation/pending_files_model.hpp"
#include "presentation/watch_controller.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <iostream>

namespace {

int failures = 0;

// 예상 결과와 다르면 테스트 실패를 기록하는 함수.
void expect(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

// 프로젝트 파일의 절대 경로를 만드는 함수.
QString projectPath(const char* relativePath) {
    return QString::fromUtf8(NOOKBIT_SOURCE_DIR) + QStringLiteral("/") +
           QString::fromUtf8(relativePath);
}

// 감시 테스트용 파일을 만드는 함수.
bool writeFile(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write("photo bytes") == 11;
}

// 조건 충족 제안은 대기 등록만 하고 같은 상태에서는 반복되지 않는지 검사하는 함수.
void testProposalAndPendingOnly() {
    QTemporaryDir directory;
    expect(directory.isValid(), "watch-controller directory should be available");
    if (!directory.isValid()) return;
    const auto folder = directory.path();
    const auto first = directory.filePath(QStringLiteral("first.jpg"));
    const auto second = directory.filePath(QStringLiteral("second.jpg"));
    const auto third = directory.filePath(QStringLiteral("third.jpg"));
    const auto settingsPath = directory.filePath(QStringLiteral("watch.json"));
    expect(writeFile(first), "first watched file should be written");
    nookbit::PendingFilesModel pending;
    nookbit::WatchController controller(
        pending,
        projectPath("resources/rules/default.example.json"),
        projectPath("schemas/rules.schema.json"),
        settingsPath,
        projectPath("schemas/watch.schema.json"),
        folder);
    controller.saveSettings(folder, true, false, 10, false,
                            true, 2, false, 1, false, 1, QStringLiteral("*.partial"));
    expect(controller.enabled() && controller.minimumFileCount() == 2 &&
               QFile::exists(settingsPath), "editable watch settings should be saved");
    controller.checkNow();
    expect(!controller.proposalVisible(), "one file should not meet a two-file threshold");
    expect(writeFile(second), "second watched file should be written");
    controller.checkNow();
    expect(controller.proposalVisible() && controller.proposalFiles().size() == 2 &&
               pending.count() == 0, "matching folder should propose without queueing files");
    controller.dismissProposal();
    controller.checkNow();
    expect(!controller.proposalVisible(), "dismissed unchanged proposal should not reappear");
    expect(writeFile(third), "third watched file should be written");
    controller.checkNow();
    expect(controller.proposalVisible() && controller.proposalFiles().size() == 3,
           "new file state should allow a new proposal");
    controller.addProposalToPending();
    expect(pending.count() == 3 && QFile::exists(first) && QFile::exists(second) &&
               QFile::exists(third) && !controller.proposalVisible(),
           "accepting a proposal should register paths without moving files");
    nookbit::PendingFilesModel restartedPending;
    nookbit::WatchController restarted(
        restartedPending,
        projectPath("resources/rules/default.example.json"),
        projectPath("schemas/rules.schema.json"),
        settingsPath,
        projectPath("schemas/watch.schema.json"),
        QString());
    expect(restarted.enabled() && restarted.folder() == folder &&
               restarted.minimumFileCount() == 2 && restarted.rescanIntervalMinutes() == 10,
           "watch settings should reload after restarting the controller");
}

} // namespace

// 폴더 감시 컨트롤러 테스트를 실행하는 함수.
int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    testProposalAndPendingOnly();
    if (failures == 0) {
        std::cout << "All watch controller tests passed.\n";
        return 0;
    }
    std::cerr << failures << " watch controller assertion(s) failed.\n";
    return 1;
}
