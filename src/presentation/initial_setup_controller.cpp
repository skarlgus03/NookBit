#include "presentation/initial_setup_controller.hpp"
#include <QDir>
#include <QFileInfo>
#include <QScopedValueRollback>
#include <QThread>

namespace nookbit {
// 최초 안내의 완료 여부와 현재 값을 준비하는 생성자.
InitialSetupController::InitialSetupController(QSettings& settings, bool persist, InitialSetupDefaults defaults, Apply apply, QObject* parent)
    : QObject(parent), settings_(settings), persist_(persist),
      required_(persist && !settings.value(QStringLiteral("onboarding/completed"), false).toBool()),
      defaults_(std::move(defaults)), apply_(std::move(apply)) {}
// 입력 오류를 표시하고 완료 표식은 바꾸지 않는 함수.
bool InitialSetupController::fail(const QString& message) {
    message_ = message; emit messageChanged(); return false;
}
// 명시적인 시작 또는 건너뛰기만 저장하며 저장 오류 시 안내를 유지하는 함수.
bool InitialSetupController::markCompleted() {
    if (persist_) {
        settings_.setValue(QStringLiteral("onboarding/completed"), true);
        settings_.sync();
        if (settings_.status() != QSettings::NoError) {
            settings_.remove(QStringLiteral("onboarding/completed"));
            return fail(QStringLiteral("첫 실행 완료 기록을 저장하지 못했어요. 설정 저장 권한을 확인해 주세요."));
        }
    }
    required_ = false; message_.clear(); emit messageChanged(); emit requiredChanged(); return true;
}
// 모든 화면 입력을 먼저 검사하고 기능 소유자에게만 실제 설정 적용을 요청하는 함수.
bool InitialSetupController::finish(const QString& destination, bool importRules, const QUrl& rulesFile, bool showCompanion) {
    if (QThread::currentThread() != thread() || !required_ || applying_) return false;
    QScopedValueRollback guard(applying_, true);
    const auto clean = QDir::cleanPath(QDir::fromNativeSeparators(destination.trimmed()));
    const QFileInfo folder(clean);
    if (organizerAvailable() && (destination.trimmed().isEmpty() || !folder.isAbsolute() || QDir(clean).isRoot()
        || (folder.exists() && !folder.isDir())))
        return fail(QStringLiteral("드라이브 루트가 아닌 정리 폴더의 전체 경로를 입력해 주세요."));
    if (organizerAvailable() && importRules && (rulesFile.isEmpty() || !rulesFile.isLocalFile()))
        return fail(QStringLiteral("불러올 로컬 JSON 규칙 파일을 선택해 주세요."));
    if (!apply_) return fail(QStringLiteral("초기 설정을 적용할 수 없어요."));
    const auto result = apply_({organizerAvailable() ? clean : QString(), organizerAvailable() && importRules ? rulesFile : QUrl(),
        companionAvailable() ? showCompanion : defaults_.companionShown});
    if (!result) return fail(QString::fromStdString(result.error().message));
    return markCompleted();
}
// 건너뛰기는 적용 콜백을 호출하지 않는 함수.
bool InitialSetupController::skip() {
    if (QThread::currentThread() != thread() || !required_ || applying_) return false;
    return markCompleted();
}
// 선택한 로컬 파일·폴더 주소를 일반 경로로 변환하는 함수.
QString InitialSetupController::folderPath(const QUrl& url) const {
    return url.isLocalFile() ? QDir::toNativeSeparators(QDir::cleanPath(url.toLocalFile())) : QString();
}
// 아직 없는 기본 정리 폴더는 기존 상위 폴더에서 선택을 시작하는 함수.
QUrl InitialSetupController::folderUrl(const QString& path) const {
    QFileInfo info(QDir::fromNativeSeparators(path));
    if (!info.isAbsolute()) return {};
    while (!info.isDir() && !QDir(info.absoluteFilePath()).isRoot()) info.setFile(info.absolutePath());
    return QUrl::fromLocalFile(info.absoluteFilePath());
}
}
