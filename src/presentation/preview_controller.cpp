#include "presentation/preview_controller.hpp"

#include "application/use_cases/create_preview_use_case.hpp"
#include "application/use_cases/freeze_preview_use_case.hpp"
#include "application/use_cases/verify_saved_plan_use_case.hpp"

#include <QDir>
#include <QFileInfo>
#include <QVariantMap>

#include <utility>

namespace nookbit {
namespace {

// 화면에서 지정한 목적지 루트를 이번 미리보기에만 제공하는 저장소.
class SessionSettingsRepository final : public ISettingsRepository {
public:
    /// 이번 미리보기의 목적지 루트를 보관하는 생성자.
    explicit SessionSettingsRepository(std::filesystem::path homeRoot) {
        settings_.homeRoot = std::move(homeRoot);
    }

    /// 저장된 목적지 루트와 기본 안전 정책을 반환하는 함수.
    [[nodiscard]] Result<UserSettings> load() const override {
        return Result<UserSettings>::success(settings_);
    }

    /// 화면 미리보기에서 설정 저장을 허용하지 않는 함수.
    [[nodiscard]] Result<void> save(const UserSettings&) override {
        return Result<void>::failure({"settings.read_only", "Preview settings are not persisted."});
    }

private:
    UserSettings settings_;
};

// Windows 경로를 Qt 문자열로 변환해 화면에 표시하는 함수.
QString displayPath(const std::filesystem::path& path) {
    return QString::fromStdWString(path.wstring());
}

} // namespace

// 화면 모델과 규칙 파일 및 보호할 앱 데이터 경로를 받는 생성자.
PreviewController::PreviewController(PendingFilesModel& pendingFiles,
                                     QString rulesPath,
                                     QString schemaPath,
                                     std::filesystem::path appDataRoot,
                                     QString defaultDestinationRoot,
                                     QString databasePath,
                                     QString migrationPath,
                                     QObject* parent)
    : QObject(parent),
      pendingFiles_(pendingFiles),
      rules_(std::move(rulesPath), std::move(schemaPath)),
      planner_(ruleEngine_, std::move(appDataRoot)),
      history_(std::move(databasePath), std::move(migrationPath)),
      defaultDestinationRoot_(std::move(defaultDestinationRoot)) {
    connect(&pendingFiles_, &QAbstractItemModel::rowsInserted, this, [this] { clear(); });
    connect(&pendingFiles_, &QAbstractItemModel::rowsRemoved, this, [this] { clear(); });
}

// 화면에 표시할 파일별 이동 초안을 반환하는 함수.
QVariantList PreviewController::steps() const { return steps_; }

// 화면에 표시할 검사 경고를 반환하는 함수.
QStringList PreviewController::warnings() const { return warnings_; }

// 최근 미리보기 결과 문구를 반환하는 함수.
QString PreviewController::message() const { return message_; }

// 시작 시 발견한 미완료 작업 안내를 반환하는 함수.
QString PreviewController::recoveryNotice() const { return recoveryNotice_; }

// 시작 시 복구 검사 결과를 화면에 전달하는 함수.
void PreviewController::setRecoveryNotice(QString notice) {
    if (recoveryNotice_ == notice) return;
    recoveryNotice_ = std::move(notice);
    emit recoveryNoticeChanged();
}

// 유효한 미리보기 초안의 존재 여부를 반환하는 함수.
bool PreviewController::hasPreview() const noexcept { return hasPreview_; }

// 현재 미리보기를 SQLite에 고정 저장했는지 반환하는 함수.
bool PreviewController::hasFrozenPlan() const noexcept { return hasFrozenPlan_; }

// 처음 제안할 정리 목적지 루트를 반환하는 함수.
QString PreviewController::defaultDestinationRoot() const { return defaultDestinationRoot_; }

// 대기 파일에 대해 실제 이동 없이 분류 미리보기를 만드는 함수.
void PreviewController::createPreview(const QString& destinationRoot) {
    clear();
    if (pendingFiles_.count() == 0) {
        showError(QStringLiteral("먼저 파일을 대기 목록에 추가해 주세요."));
        return;
    }
    const QFileInfo homeInfo(destinationRoot.trimmed());
    if (!homeInfo.isAbsolute() || QDir(homeInfo.absoluteFilePath()).isRoot()) {
        showError(QStringLiteral("정리할 위치는 드라이브 루트가 아닌 절대 경로로 지정해 주세요."));
        return;
    }

    const auto ruleSet = rules_.load();
    if (!ruleSet) {
        showError(QStringLiteral("분류 규칙을 읽지 못했어요: %1")
                      .arg(QString::fromStdString(ruleSet.error().message)));
        return;
    }

    CreatePreviewRequest request;
    request.ruleSetVersion = ruleSet.value().version;
    const auto paths = pendingFiles_.filePaths();
    request.selectedFiles.reserve(static_cast<std::size_t>(paths.size()));
    for (const auto& path : paths) {
        request.selectedFiles.emplace_back(path.toStdWString());
    }

    SessionSettingsRepository settings(std::filesystem::path(homeInfo.absoluteFilePath().toStdWString()));
    CreatePreviewUseCase useCase(settings, rules_, scanner_, planner_, &hasher_);
    const auto result = useCase.execute(request);
    if (!result) {
        showError(QStringLiteral("미리보기를 만들지 못했어요: %1")
                      .arg(QString::fromStdString(result.error().message)));
        return;
    }
    draft_ = result.value();
    previewHomeRoot_ = homeInfo.absoluteFilePath();

    for (const auto& step : result.value().steps) {
        QVariantMap row;
        row.insert(QStringLiteral("fileName"), displayPath(step.source.filename()));
        row.insert(QStringLiteral("source"), displayPath(step.source));
        row.insert(QStringLiteral("destination"), displayPath(step.destination));
        row.insert(QStringLiteral("rule"), QString::fromStdString(step.matchedRuleId));
        row.insert(QStringLiteral("status"), step.status == StepStatus::Skipped
                                                  ? QStringLiteral("건너뜀")
                                                  : QStringLiteral("예정"));
        steps_.append(row);
    }
    for (const auto& warning : result.value().warnings) {
        warnings_.append(QString::fromStdString(warning.message));
    }
    hasPreview_ = true;
    message_ = QStringLiteral("미리보기 %1건, 경고 %2건. 실제 파일은 변경하지 않았어요.")
                   .arg(steps_.size()).arg(warnings_.size());
    emit previewChanged();
}

// 현재 미리보기를 재검사한 뒤 승인 전 계획으로 고정·저장하는 함수.
void PreviewController::freezePreview() {
    if (!draft_ || !hasPreview_ || hasFrozenPlan_) {
        showError(QStringLiteral("먼저 새 미리보기를 만들어 주세요."));
        return;
    }
    SessionSettingsRepository settings(
        std::filesystem::path(previewHomeRoot_.toStdWString()));
    std::vector<std::filesystem::path> selectedFiles;
    const auto paths = pendingFiles_.filePaths();
    selectedFiles.reserve(static_cast<std::size_t>(paths.size()));
    for (const auto& path : paths) selectedFiles.emplace_back(path.toStdWString());

    FreezePreviewUseCase useCase(settings, rules_, scanner_, planner_, hasher_, history_);
    const auto result = useCase.execute(*draft_, selectedFiles);
    if (!result) {
        showError(QStringLiteral("계획을 저장하지 못했어요. 새 미리보기를 확인해 주세요: %1")
                      .arg(QString::fromStdString(result.error().message)));
        return;
    }
    draft_ = result.value();
    hasFrozenPlan_ = true;
    message_ = QStringLiteral("계획을 안전하게 저장했어요. 아직 승인하거나 파일을 이동하지 않았어요.");
    emit previewChanged();
}

// 저장된 계획을 DB에서 다시 읽고 현재 상태와 비교하는 함수.
bool PreviewController::verifySavedPlan() {
    if (!draft_ || !hasFrozenPlan_) {
        message_ = QStringLiteral("먼저 계획을 고정·저장해 주세요.");
        emit previewChanged();
        return false;
    }
    SessionSettingsRepository settings(
        std::filesystem::path(previewHomeRoot_.toStdWString()));
    std::vector<std::filesystem::path> selectedFiles;
    const auto paths = pendingFiles_.filePaths();
    selectedFiles.reserve(static_cast<std::size_t>(paths.size()));
    for (const auto& path : paths) selectedFiles.emplace_back(path.toStdWString());

    VerifySavedPlanUseCase useCase(settings, rules_, scanner_, planner_, hasher_, history_);
    const auto result = useCase.execute(draft_->id, selectedFiles);
    if (!result) {
        message_ = QStringLiteral("저장 계획 검사 실패. 새 미리보기가 필요할 수 있어요: %1")
                       .arg(QString::fromStdString(result.error().message));
        emit previewChanged();
        return false;
    }
    message_ = QStringLiteral("검사 시점에는 저장 계획과 현재 상태가 일치해요. 아직 승인·이동하지 않았어요.");
    emit previewChanged();
    return true;
}

// 대기 목록이 바뀌었을 때 낡은 미리보기를 지우는 함수.
void PreviewController::clear() {
    if (steps_.isEmpty() && warnings_.isEmpty() && message_.isEmpty() &&
        !hasPreview_ && !hasFrozenPlan_ && !draft_) return;
    steps_.clear();
    warnings_.clear();
    message_.clear();
    hasPreview_ = false;
    hasFrozenPlan_ = false;
    draft_.reset();
    previewHomeRoot_.clear();
    emit previewChanged();
}

// 실패 이유를 화면에 알리고 기존 초안을 지우는 함수.
void PreviewController::showError(const QString& message) {
    steps_.clear();
    warnings_.clear();
    hasPreview_ = false;
    hasFrozenPlan_ = false;
    draft_.reset();
    previewHomeRoot_.clear();
    message_ = message;
    emit previewChanged();
}

} // namespace nookbit
