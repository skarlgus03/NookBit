#include "presentation/companion_edit_controller.hpp"
#include <utility>

namespace nookbit {
// 편집 처리에 필요한 창구만 보관하는 생성자.
CompanionEditController::CompanionEditController(Check validate, Check apply, Load load, Save save, QObject* parent)
    : QObject(parent), validate_(std::move(validate)), apply_(std::move(apply)), load_(std::move(load)), save_(std::move(save)) {}

// 원본 팩이 선택된 직후 해당 팩에 저장된 편집값만 복원하는 함수.
void CompanionEditController::loadPack(const QString& key) {
    key_ = key;
    applied_.clear();
    status_.clear();
    if (!key.isEmpty()) {
        const auto loaded = load_(key);
        if (!loaded) status_ = QString::fromStdString(loaded.error().message);
        else {
            const auto checked = validate_(loaded.value());
            if (!checked) status_ = QString::fromStdString(checked.error().message);
            else {
                const auto result = apply_(loaded.value());
                if (result) applied_ = loaded.value();
                else status_ = QString::fromStdString(result.error().message);
            }
        }
    }
    emit appliedChanged();
    emit changed();
}

// 검증과 저장이 성공한 값만 공개하고 오류가 나면 초안을 그대로 남기는 함수.
bool CompanionEditController::applyChanges(const QVariantMap& edits) {
    if (key_.isEmpty()) return false;
    QString rollbackError;
    auto result = validate_(edits);
    if (result) result = save_(key_, edits);
    if (result) {
        result = apply_(edits);
        if (!result) {
            const auto restored = save_(key_, applied_);
            if (!restored) rollbackError = QStringLiteral(" 이전 저장값 복원도 실패했어요: ") + QString::fromStdString(restored.error().message);
        }
    }
    if (!result) {
        status_ = QString::fromStdString(result.error().message) + rollbackError;
        emit changed();
        return false;
    }
    applied_ = edits;
    status_ = QStringLiteral("편집 내용을 저장하고 적용했어요.");
    emit appliedChanged();
    emit changed();
    return true;
}
}
