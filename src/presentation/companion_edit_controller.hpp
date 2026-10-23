#pragma once
#include <nookbit/domain/result.hpp>
#include <QObject>
#include <QVariantMap>
#include <functional>

namespace nookbit {
/// 편집값 검증·저장·실제 적용을 조율하며 화면이나 캐릭터 구현을 소유하지 않는다.
class CompanionEditController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap applied READ applied NOTIFY appliedChanged)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)
public:
    using Check = std::function<Result<void>(const QVariantMap&)>;
    using Load = std::function<Result<QVariantMap>(const QString&)>;
    using Save = std::function<Result<void>(const QString&, const QVariantMap&)>;
    /// 검증·적용·보관 창구를 주입하는 생성자.
    CompanionEditController(Check validate, Check apply, Load load, Save save, QObject* parent = nullptr);
    /// 마지막으로 저장하고 적용한 편집값을 반환하는 함수.
    QVariantMap applied() const { return applied_; }
    /// 적용 또는 읽기 오류 안내를 반환하는 함수.
    QString status() const { return status_; }
    /// 선택한 팩에 개인 설정을 저장할 수 있는지 반환하는 함수.
    bool available() const { return !key_.isEmpty(); }
    /// 선택한 팩의 저장값을 읽고 검증하여 실제 재생에 복원하는 함수.
    void loadPack(const QString& key);
    /// 저장 성공 후 실제 재생을 교체하고 실패 시 기존 적용값을 유지하는 함수.
    Q_INVOKABLE bool applyChanges(const QVariantMap& edits);
signals:
    void appliedChanged();
    void changed();
private:
    Check validate_, apply_;
    Load load_;
    Save save_;
    QString key_, status_;
    QVariantMap applied_;
};
}
