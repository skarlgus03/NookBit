#include "presentation/watch_controller.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QUrl>

#include <algorithm>
#include <utility>

namespace nookbit {
namespace {

// 설정에서 지정한 조건 종류의 기준값을 찾는 함수.
int thresholdFor(const WatchSettings& settings, WatchConditionType type) {
    const auto found = std::find_if(settings.conditions.begin(), settings.conditions.end(),
        [type](const WatchCondition& condition) { return condition.type == type; });
    return found == settings.conditions.end() ? 0 : found->threshold;
}

// 감시 폴더의 분류 가능 파일 상태를 중복 제안 방지용 해시로 만드는 함수.
QString snapshotSignature(const std::vector<FileFact>& files) {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const auto& file : files) {
        hash.addData(QString::fromStdWString(file.absolutePath.wstring()).toUtf8());
        hash.addData("\0", 1);
        hash.addData(QByteArray::number(file.fingerprint.size));
        hash.addData("\0", 1);
        hash.addData(QByteArray::number(file.fingerprint.modifiedUtcMs));
        hash.addData("\0", 1);
    }
    return QString::fromLatin1(hash.result().toHex());
}

} // namespace

// 대기 목록과 감시 설정·제안 계약을 연결하는 생성자.
WatchController::WatchController(PendingFilesModel& pendingFiles,
                                 IWatchSettingsRepository& repository,
                                 IWatchProposalUseCase& evaluateWatchFolder,
                                 QString defaultFolder,
                                 QObject* parent)
    : QObject(parent), pendingFiles_(pendingFiles),
      repository_(repository), evaluateWatchFolder_(evaluateWatchFolder) {
    debounceTimer_.setSingleShot(true);
    debounceTimer_.setInterval(1500);
    connect(&directoryWatcher_, &QFileSystemWatcher::directoryChanged,
            this, [this] { debounceTimer_.start(); });
    connect(&debounceTimer_, &QTimer::timeout, this, &WatchController::checkNow);
    connect(&rescanTimer_, &QTimer::timeout, this, &WatchController::checkNow);
    // 대기 목록이 바뀌면 이전 제안을 닫고 최신 목록으로 조건을 다시 검사하는 함수.
    connect(&pendingFiles_, &PendingFilesModel::countChanged, this, [this] {
        const bool recheck = settings_.enabled || proposalVisible_;
        lastOfferedSignature_.clear();
        clearProposal();
        emit watchChanged();
        if (recheck) debounceTimer_.start();
    });
    loadSettings(defaultFolder);
    restartMonitoring();
}

// 현재 설정된 감시 폴더를 반환하는 함수.
QString WatchController::folder() const { return QString::fromStdWString(settings_.folder.wstring()); }
// 자동 감시 사용 여부를 반환하는 함수.
bool WatchController::enabled() const noexcept { return settings_.enabled; }
// 하위 폴더 감시 여부를 반환하는 함수.
bool WatchController::recursive() const noexcept { return settings_.recursive; }
// 정기 재검사 간격을 반환하는 함수.
int WatchController::rescanIntervalMinutes() const noexcept { return settings_.rescanIntervalMinutes; }
// 모든 조건을 충족해야 하는지 반환하는 함수.
bool WatchController::combineAll() const noexcept { return settings_.combination == WatchCombination::All; }
// 파일 수 조건 사용 여부를 반환하는 함수.
bool WatchController::countEnabled() const noexcept {
    return thresholdFor(settings_, WatchConditionType::FileCount) > 0;
}
// 파일 수 조건의 기준값을 반환하는 함수.
int WatchController::minimumFileCount() const noexcept {
    return std::max(1, thresholdFor(settings_, WatchConditionType::FileCount));
}
// 전체 크기 조건 사용 여부를 반환하는 함수.
bool WatchController::sizeEnabled() const noexcept {
    return thresholdFor(settings_, WatchConditionType::TotalSizeMiB) > 0;
}
// 전체 크기 조건의 MiB 기준값을 반환하는 함수.
int WatchController::minimumTotalMiB() const noexcept {
    return std::max(1, thresholdFor(settings_, WatchConditionType::TotalSizeMiB));
}
// 가장 오래된 파일의 경과 시간 조건 사용 여부를 반환하는 함수.
bool WatchController::ageEnabled() const noexcept {
    return thresholdFor(settings_, WatchConditionType::OldestAgeMinutes) > 0;
}
// 경과 시간 조건의 분 기준값을 반환하는 함수.
int WatchController::minimumAgeMinutes() const noexcept {
    return std::max(1, thresholdFor(settings_, WatchConditionType::OldestAgeMinutes));
}
// 제외할 파일명 패턴을 쉼표로 이어 반환하는 함수.
QString WatchController::excludePatterns() const {
    QStringList patterns;
    for (const auto& pattern : settings_.excludePatterns) {
        patterns.append(QString::fromStdString(pattern));
    }
    return patterns.join(QStringLiteral(", "));
}
// 최근 감시·검사 결과 문구를 반환하는 함수.
QString WatchController::statusMessage() const { return statusMessage_; }
// 현재 제안이 열려 있는지 반환하는 함수.
bool WatchController::proposalVisible() const noexcept { return proposalVisible_; }
// 현재 제안의 요약 문구를 반환하는 함수.
QString WatchController::proposalText() const { return proposalText_; }
// 제안에 포함된 파일의 경로를 반환하는 함수.
QStringList WatchController::proposalFiles() const { return proposedFiles_; }

// 저장된 조건을 불러오고 기본값을 준비하는 함수.
void WatchController::loadSettings(const QString& defaultFolder) {
    const auto loaded = repository_.load();
    if (loaded) {
        settings_ = loaded.value();
        statusMessage_ = settings_.enabled
            ? QStringLiteral("저장된 감시 조건을 불러왔어요.")
            : QStringLiteral("감시 조건은 저장돼 있고 자동 감시는 꺼져 있어요.");
        return;
    }
    settings_.folder = std::filesystem::path(defaultFolder.toStdWString());
    settings_.enabled = false;
    settings_.conditions = {{WatchConditionType::FileCount, 20}};
    settings_.excludePatterns = {"*.partial", "*.crdownload", "~$*"};
    statusMessage_ = loaded.error().code == "json.document.open_failed"
        ? QStringLiteral("감시를 시작하려면 폴더와 조건을 저장해 주세요.")
        : QStringLiteral("저장된 감시 설정을 읽지 못했어요. 조건을 다시 저장해 주세요: %1")
              .arg(QString::fromStdString(loaded.error().message));
}

// 설정에 맞게 폴더 변경 감지와 정기 검사를 다시 시작하는 함수.
void WatchController::restartMonitoring() {
    debounceTimer_.stop();
    rescanTimer_.stop();
    if (!directoryWatcher_.directories().isEmpty()) {
        directoryWatcher_.removePaths(directoryWatcher_.directories());
    }
    if (!settings_.enabled) return;
    const auto path = folder();
    if (QFileInfo(path).isDir()) directoryWatcher_.addPath(path);
    rescanTimer_.start(settings_.rescanIntervalMinutes * 60000);
    QTimer::singleShot(1000, this, &WatchController::checkNow);
}

// 사용자에게 표시 중인 제안을 지우는 함수.
void WatchController::clearProposal() {
    proposalVisible_ = false;
    proposalText_.clear();
    proposedFiles_.clear();
}

// 화면에서 고른 폴더와 조건을 검증해 저장하는 함수.
void WatchController::saveSettings(const QString& folder,
                                   bool enabled,
                                   bool recursive,
                                   int intervalMinutes,
                                   bool combineAll,
                                   bool countEnabled,
                                   int minimumFileCount,
                                   bool sizeEnabled,
                                   int minimumTotalMiB,
                                   bool ageEnabled,
                                   int minimumAgeMinutes,
                                   const QString& excludePatterns) {
    WatchSettings proposed;
    proposed.folder = std::filesystem::path(folder.trimmed().toStdWString());
    proposed.enabled = enabled;
    proposed.recursive = recursive;
    proposed.rescanIntervalMinutes = intervalMinutes;
    proposed.combination = combineAll ? WatchCombination::All : WatchCombination::Any;
    if (countEnabled) proposed.conditions.push_back({WatchConditionType::FileCount, minimumFileCount});
    if (sizeEnabled) proposed.conditions.push_back({WatchConditionType::TotalSizeMiB, minimumTotalMiB});
    if (ageEnabled) proposed.conditions.push_back({WatchConditionType::OldestAgeMinutes, minimumAgeMinutes});
    for (const auto& raw : excludePatterns.split(',', Qt::SkipEmptyParts)) {
        const auto pattern = raw.trimmed();
        if (!pattern.isEmpty()) proposed.excludePatterns.push_back(pattern.toStdString());
    }
    if (enabled && !QFileInfo(folder.trimmed()).isDir()) {
        statusMessage_ = QStringLiteral("자동 감시를 켜려면 존재하는 폴더를 선택해 주세요.");
        emit watchChanged();
        return;
    }
    const auto saved = repository_.save(proposed);
    if (!saved) {
        statusMessage_ = QStringLiteral("감시 조건을 저장하지 못했어요: %1")
                             .arg(QString::fromStdString(saved.error().message));
        emit watchChanged();
        return;
    }
    settings_ = std::move(proposed);
    lastOfferedSignature_.clear();
    clearProposal();
    restartMonitoring();
    statusMessage_ = settings_.enabled
        ? QStringLiteral("감시 조건을 저장하고 폴더 감시를 시작했어요. 파일은 이동하지 않아요.")
        : QStringLiteral("감시 조건을 저장했어요. 자동 감시는 꺼져 있어요.");
    emit settingsChanged();
    emit watchChanged();
}

// 저장된 조건으로 감시 폴더를 즉시 읽어 제안 여부를 판단하는 함수.
void WatchController::checkNow() {
    if (settings_.folder.empty() || settings_.conditions.empty()) {
        statusMessage_ = QStringLiteral("먼저 감시 폴더와 조건을 저장해 주세요.");
        emit watchChanged();
        return;
    }
    if (settings_.enabled && directoryWatcher_.directories().isEmpty() && QFileInfo(folder()).isDir()) {
        directoryWatcher_.addPath(folder());
    }
    const auto nowUtcMs = QDateTime::currentMSecsSinceEpoch();
    std::vector<std::filesystem::path> pendingPaths;
    for (const auto& path : pendingFiles_.filePaths()) pendingPaths.emplace_back(path.toStdWString());
    const auto result = evaluateWatchFolder_.execute(settings_, nowUtcMs, pendingPaths);
    if (!result) {
        clearProposal();
        statusMessage_ = QStringLiteral("감시 폴더를 평가하지 못했어요: %1")
                             .arg(QString::fromStdString(result.error().message));
        emit watchChanged();
        return;
    }
    const auto& evaluation = result.value().evaluation;
    const auto& eligible = result.value().eligibleFiles;
    if (!evaluation.shouldOffer) {
        clearProposal();
        statusMessage_ = QStringLiteral("조건 미충족: 분류 가능한 파일 %1개 · 총 %2 MiB · 가장 오래된 파일 %3분")
                             .arg(evaluation.fileCount)
                             .arg(evaluation.totalBytes / (1024ULL * 1024ULL))
                             .arg(evaluation.oldestAgeMinutes);
        emit watchChanged();
        return;
    }
    const auto signature = snapshotSignature(eligible);
    if (signature == lastOfferedSignature_) {
        statusMessage_ = QStringLiteral("조건 충족: 같은 파일 상태에 대해서는 이미 제안했어요.");
        emit watchChanged();
        return;
    }
    lastOfferedSignature_ = signature;
    proposedFiles_.clear();
    for (const auto& file : eligible) {
        proposedFiles_.append(QDir::fromNativeSeparators(
            QString::fromStdWString(file.absolutePath.wstring())));
    }
    proposalVisible_ = true;
    proposalText_ = QStringLiteral("분류 가능한 파일 %1개가 설정한 조건에 닿았어요. 정리 대기 목록에 담을까요? 실제 이동은 별도 승인 후에만 합니다.")
                        .arg(eligible.size());
    statusMessage_ = QStringLiteral("감시 조건이 충족돼 정리를 제안했어요. 파일은 이동하지 않았어요.");
    emit watchChanged();
}

// 현재 제안의 파일들을 이동 없이 대기 목록에 등록하는 함수.
void WatchController::addProposalToPending() {
    if (!proposalVisible_) return;
    const int before = pendingFiles_.count();
    // 등록 중 countChanged가 제안을 비우므로 경로 목록을 먼저 복사한다.
    const auto files = proposedFiles_;
    for (const auto& path : files) pendingFiles_.addUrl(QUrl::fromLocalFile(path));
    const int added = pendingFiles_.count() - before;
    clearProposal();
    statusMessage_ = QStringLiteral("파일 %1개를 대기 목록에만 담았어요. 이동하려면 미리보기와 승인이 필요해요.")
                         .arg(added);
    emit watchChanged();
}

// 현재 제안을 닫고 같은 파일 상태에서는 다시 묻지 않는 함수.
void WatchController::dismissProposal() {
    clearProposal();
    statusMessage_ = QStringLiteral("이번 제안을 닫았어요. 파일 상태가 바뀌면 다시 검사해요.");
    emit watchChanged();
}

// 바뀐 분류 규칙으로 감시 제안을 다시 계산하는 함수.
void WatchController::rulesChanged() {
    clearProposal();
    lastOfferedSignature_.clear();
    if (settings_.enabled) checkNow();
    else emit watchChanged();
}

} // namespace nookbit
