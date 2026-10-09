#pragma once
#include <QObject>

namespace nookbit {
/// 화면 닫기와 프로세스 종료를 구분하며 작업자는 소유하거나 호출하지 않는다.
class AppLifecycleController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool trayAvailable READ trayAvailable NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
public:
    /// 화면 수명의 종료 정책을 만드는 생성자.
    explicit AppLifecycleController(QObject* parent = nullptr) : QObject(parent) {}
    /// 숨긴 창을 복원할 트레이가 사용 가능한지 반환하는 함수.
    bool trayAvailable() const { return trayAvailable_; }
    /// 종료를 막아야 할 실행 중 작업이 있는지 반환하는 함수.
    bool busy() const { return busy_; }
    /// 트레이가 사라지면 숨긴 앱에 접근할 수 있도록 화면을 복원하는 함수.
    void setTrayAvailable(bool value) {
        if (trayAvailable_ == value) return;
        trayAvailable_ = value; emit changed();
        if (!value) emit showRequested();
    }
    /// 작업 중 여부만 받아 종료 가능 상태를 갱신하는 함수.
    void setBusy(bool value) {
        if (busy_ == value) return;
        busy_ = value; emit changed();
    }
    /// 트레이가 있으면 창만 숨기고 없으면 안전한 종료를 요청하는 함수.
    Q_INVOKABLE void closeWindow() {
        if (trayAvailable_) emit hideRequested();
        else requestExit();
    }
    /// 앱 화면을 다시 열도록 요청하는 함수.
    Q_INVOKABLE void openWindow() { emit showRequested(); }
    /// 실행 중 작업이 없을 때만 명시적인 프로세스 종료를 허용하는 함수.
    Q_INVOKABLE void requestExit() {
        if (busy_) { emit showRequested(); emit exitBlocked(); return; }
        emit exitRequested();
    }
signals:
    void changed();
    void showRequested();
    void hideRequested();
    void exitRequested();
    void exitBlocked();
private:
    bool trayAvailable_{false};
    bool busy_{false};
};
}
