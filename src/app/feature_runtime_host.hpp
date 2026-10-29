#pragma once
#include "app/feature_startup.hpp"
#include <QObject>
#include <memory>

class QSettings;
class QGuiApplication;
namespace nookbit {
class UiPageRegistry;
class FileOrganizerRuntime;
class FolderWatchRuntime;
class NotificationRuntime;
class CompanionRuntime;
struct FeatureRuntimeConfig {
    QString appData;
    QString defaultDestination;
    QString applicationDir;
    QString watchFolder;
    bool verifyUi{false};
};

/// 활성 기능만 생성하고 선택적인 기능 사이 연결과 종료 순서를 소유하는 조립부.
/// 설정·앱·페이지 목록은 더 오래, QML과 트레이 기여는 더 짧게 살아 있어야 한다.
class FeatureRuntimeHost final : public QObject {
    Q_OBJECT
public:
    /// 이미 의존성을 해석한 계획을 받아 필요한 기능만 구성하는 생성자.
    FeatureRuntimeHost(FeatureStartupPlan plan, FeatureRuntimeConfig config, QSettings& preferences,
        QGuiApplication& application, UiPageRegistry& pages);
    /// 연결을 먼저 끊고 기능 작업이 끝난 뒤 의존 순서대로 해제하는 소멸자.
    ~FeatureRuntimeHost() override;
    /// 생성·등록·기능 연결이 성공했는지 반환하는 함수.
    const Result<void>& startupResult() const;
    /// 정리 또는 팩 가져오기가 아직 진행 중인지 합쳐 반환하는 함수.
    bool busy() const;
    /// 활성 정리 기능을 빌려주며 비활성 상태에는 nullptr를 반환하는 함수.
    FileOrganizerRuntime* organizer() const;
    /// 활성 감시 기능을 빌려주며 비활성 상태에는 nullptr를 반환하는 함수.
    FolderWatchRuntime* watch() const;
    /// 활성 알림 기능을 빌려주며 비활성 상태에는 nullptr를 반환하는 함수.
    NotificationRuntime* notifications() const;
    /// 활성 컴패니언을 빌려주며 비활성 상태에는 nullptr를 반환하는 함수.
    CompanionRuntime* companion() const;
signals:
    void busyChanged();
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
