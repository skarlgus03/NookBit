#pragma once
#include <nookbit/application/image_pack_library.hpp>
#include <QObject>
#include <QVariantList>
#include <QUrl>
#include <QPointF>
#include <functional>

namespace nookbit {
class BackgroundTaskRunner;
/// 팩 목록·선택·개인 배율만 담당하고 캐릭터 및 파일 작업을 직접 소유하지 않는다.
class CharacterSettingsController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList packs READ packs NOTIFY changed)
    Q_PROPERTY(QString selectedKey READ selectedKey NOTIFY changed)
    Q_PROPERTY(double userScale READ userScale NOTIFY scaleChanged)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QPointF pivotOffset READ pivotOffset NOTIFY placementChanged)
    Q_PROPERTY(QPointF bubbleOffset READ bubbleOffset NOTIFY placementChanged)
public:
    using Apply = std::function<Result<void>(ImagePack)>;
    /// 보관소·팩 적용 창구·임포트 실행기를 주입하는 생성자.
    CharacterSettingsController(IImagePackLibrary& library, Apply apply, BackgroundTaskRunner& runner, QObject* parent = nullptr);
    /// 현재 선택 목록을 반환하는 함수.
    QVariantList packs() const { return packs_; }
    /// 성공적으로 적용한 팩 식별자를 반환하는 함수.
    QString selectedKey() const { return selectedKey_; }
    /// 팩과 분리된 사용자 표시 배율을 반환하는 함수.
    double userScale() const { return scale_; }
    /// 프레임별 기준점에 더하는 개인 보정값을 반환하는 함수.
    QPointF pivotOffset() const { return pivotOffset_; }
    /// 팩 말풍선 부착점에 더하는 개인 보정값을 반환하는 함수.
    QPointF bubbleOffset() const { return bubbleOffset_; }
    /// 유한하고 제한된 두 좌표를 함께 적용하는 함수.
    Q_INVOKABLE void setPlacement(QPointF pivot, QPointF bubble);
    /// 가져오기 결과 및 오류 안내를 반환하는 함수.
    QString status() const { return status_; }
    /// 임포트 작업 중인지 반환하는 함수.
    bool busy() const;
    /// 팩 목록을 다시 읽는 함수.
    Q_INVOKABLE void refresh();
    /// 재검증에 성공한 팩만 표시 설정에 적용하는 함수.
    Q_INVOKABLE bool selectPack(const QString& key);
    /// 실제 로컬 폴더만 백그라운드 임포트하는 함수.
    Q_INVOKABLE void importFolder(const QUrl& folder);
    /// 유효한 개인 배율만 적용하는 함수.
    Q_INVOKABLE void setUserScale(double value);
signals:
    void changed();
    void scaleChanged();
    void placementChanged();
    void selected(const QString& key);
private:
    IImagePackLibrary& library_;
    Apply apply_;
    BackgroundTaskRunner& runner_;
    QVariantList packs_;
    QString selectedKey_, status_;
    double scale_{0.5};
    QPointF pivotOffset_, bubbleOffset_;
};
}
