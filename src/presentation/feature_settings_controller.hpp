#pragma once
#include "app/feature_startup.hpp"
#include <QObject>
#include <QVariantList>
#include <functional>

namespace nookbit {
/// 이번 실행·저장값·화면 초안을 구분하며 런타임을 생성하거나 끄지 않는 화면 창구.
class FeatureSettingsController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList modules READ modules NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(bool canSave READ canSave NOTIFY changed)
    Q_PROPERTY(bool restartRequired READ restartRequired NOTIFY changed)
    Q_PROPERTY(bool sessionOverride READ sessionOverride CONSTANT)
    Q_PROPERTY(QString status READ status NOTIFY changed)
public:
    using Save = std::function<Result<void>(FeatureSelection)>;
    /// 확정된 실행 상태와 저장 창구를 주입받아 초안을 만드는 생성자.
    FeatureSettingsController(FeatureSelection saved, FeatureSelection active, bool sessionOverride,
        Save save, QString loadError = {}, QObject* parent = nullptr);
    /// 각 모듈의 초안·다음 실행 상태·현재 상태·의존성 이유를 반환하는 함수.
    QVariantList modules() const;
    /// 초안이 마지막 저장값과 다른지 반환하는 함수.
    bool dirty() const { return draft_ != saved_; }
    /// 변경 또는 읽기 오류 복구를 명시적으로 저장할 수 있는지 반환하는 함수.
    bool canSave() const { return dirty() || invalidSaved_; }
    /// 마지막 저장값으로 다시 실행하면 활성 기능이 달라지는지 반환하는 함수.
    bool restartRequired() const { return resolveFeatureStartup(saved_).active != active_; }
    /// 이번 실행에만 적용된 비활성 시작 옵션이 있는지 반환하는 함수.
    bool sessionOverride() const { return sessionOverride_; }
    /// 저장 결과나 입력 오류를 반환하는 함수.
    QString status() const { return status_; }
    /// 안정적인 식별자로 한 모듈의 희망 상태만 바꾸는 함수.
    Q_INVOKABLE void setEnabled(const QString& id, bool enabled);
    /// 실행 상태는 유지하고 다음 실행의 선택만 저장하는 함수.
    Q_INVOKABLE bool save();
    /// 저장하지 않은 초안만 마지막 저장값으로 되돌리는 함수.
    Q_INVOKABLE void discard();
signals:
    void changed();
private:
    FeatureSelection saved_, active_, draft_;
    bool sessionOverride_, invalidSaved_;
    Save save_;
    QString status_;
};
}
