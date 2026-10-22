#include "presentation/character_settings_controller.hpp"
#include "presentation/background_task_runner.hpp"
#include <cmath>
#include <memory>

namespace nookbit {
// 잘못된 개인 좌표를 거부하고 두 기준점 보정을 한 번에 적용하는 함수.
void CharacterSettingsController::setPlacement(QPointF pivot, QPointF bubble) {
    for (double value : {pivot.x(), pivot.y(), bubble.x(), bubble.y()})
        if (!std::isfinite(value) || std::abs(value) > 1024) return;
    if (pivot == pivotOffset_ && bubble == bubbleOffset_) return;
    pivotOffset_ = pivot; bubbleOffset_ = bubble;
    emit placementChanged();
}
// 생성 대신 주입받은 보관소와 적용 창구를 보관하는 생성자.
CharacterSettingsController::CharacterSettingsController(IImagePackLibrary& library, Apply apply, BackgroundTaskRunner& runner, QObject* parent)
    : QObject(parent), library_(library), apply_(std::move(apply)), runner_(runner) {
    // 실행기 상태를 설정 창의 잠금 여부로 전달하는 함수.
    connect(&runner_, &BackgroundTaskRunner::busyChanged, this, &CharacterSettingsController::changed);
}
// 임포트 실행기의 현재 상태를 반환하는 함수.
bool CharacterSettingsController::busy() const { return runner_.busy(); }
// 유효한 팩 목록과 식별자를 화면 값으로 변환하는 함수.
void CharacterSettingsController::refresh() {
    if (busy()) return;
    const auto result = library_.list();
    if (!result) { status_ = QString::fromStdString(result.error().message); emit changed(); return; }
    packs_.clear();
    for (const auto& item : result.value()) packs_.append(QVariantMap{
        {"key", QString::fromStdString(item.key)},
        {"label", QString::fromStdString(item.name + " · " + item.version)}});
    emit changed();
}
// 읽기·적용 성공 후에만 선택 키를 교체하는 함수.
bool CharacterSettingsController::selectPack(const QString& key) {
    if (busy()) return false;
    auto pack = library_.load(key.toStdString());
    if (!pack) { status_ = QString::fromStdString(pack.error().message); emit changed(); return false; }
    const auto applied = apply_(std::move(pack.value()));
    if (!applied) { status_ = QString::fromStdString(applied.error().message); emit changed(); return false; }
    selectedKey_ = key;
    status_ = QStringLiteral("캐릭터 팩을 적용했어요.");
    emit selected(key); emit changed(); return true;
}
// 화면을 막지 않고 보관소에 가져온 뒤 성공한 팩을 선택하는 함수.
void CharacterSettingsController::importFolder(const QUrl& folder) {
    if (busy()) return;
    if (!folder.isLocalFile()) { status_ = QStringLiteral("로컬 ImagePack 폴더를 선택해 주세요."); emit changed(); return; }
    const std::filesystem::path source(folder.toLocalFile().toStdWString());
    const auto imported = std::make_shared<std::string>();
    auto* library = &library_;
    status_ = QStringLiteral("캐릭터 팩을 검증하고 가져오는 중…");
    // 보관소 작업 결과의 식별자만 화면 스레드로 전달하는 함수.
    runner_.submit(this, [library, source, imported] {
        auto result = library->importFolder(source);
        if (!result) return Result<void>::failure(result.error());
        *imported = result.value().key;
        return Result<void>::success();
    }, [this, imported](const Result<void>& result) {
        // 성공 시 목록과 선택을 갱신하고 실패 시 기존 선택을 보존하는 함수.
        if (result) { refresh(); selectPack(QString::fromStdString(*imported)); }
        else { status_ = QString::fromStdString(result.error().message); emit changed(); }
    });
    emit changed();
}
// 유한한 10~200% 배율만 적용하고 팩 데이터는 변경하지 않는 함수.
void CharacterSettingsController::setUserScale(double value) {
    if (!std::isfinite(value) || value < 0.1 || value > 2.0 || scale_ == value) return;
    scale_ = value; emit scaleChanged();
}
}
