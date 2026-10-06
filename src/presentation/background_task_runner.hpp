#pragma once
#include <nookbit/application/ports.hpp>
#include <QObject>
#include <QPointer>
#include <QThread>
#include <functional>
#include <exception>

namespace nookbit {
/// 화면 스레드에서 요청을 접수하고 한 작업만 별도 스레드에서 실행한다.
class BackgroundTaskRunner final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
public:
    using Work = std::function<Result<void>()>;
    using Completion = std::function<void(const Result<void>&)>;
    /// 화면 수명의 단일 작업 실행기를 준비하는 함수.
    explicit BackgroundTaskRunner(QObject* parent = nullptr) : QObject(parent) {}
    /// 강제 종료 없이 작업 스레드가 끝난 뒤 자원을 해제하는 함수.
    ~BackgroundTaskRunner() override { if (thread_) { thread_->wait(); delete thread_; } }
    /// 작업 실행 및 결과 전달 대기 여부를 반환하는 함수.
    bool busy() const { return busy_; }
    /// 값으로 고정된 작업을 실행하고 결과만 화면 스레드에 전달하는 함수.
    bool submit(QObject* receiver, Work work, Completion complete) {
        if (busy_) return false;
        if (thread_) { thread_->wait(); delete thread_; thread_ = nullptr; }
        busy_ = true; emit busyChanged();
        QPointer<QObject> target(receiver);
        thread_ = QThread::create([this, target, work = std::move(work), complete = std::move(complete)] {
            auto result = Result<void>::failure({"background.exception", "Background operation failed."});
            try { result = work(); }
            catch (const std::exception& error) { result = Result<void>::failure({"background.exception", error.what()}); }
            catch (...) {}
            QMetaObject::invokeMethod(this, [this, target, complete, result] {
                busy_ = false; emit busyChanged();
                if (target) complete(result);
            }, Qt::QueuedConnection);
        });
        thread_->start(); return true;
    }
signals:
    void busyChanged();
private:
    QThread* thread_{nullptr};
    bool busy_{false};
};
/// 실행기를 주입하지 않은 기존 계약 시험은 동기 실행하고 앱은 비동기로 전달하는 함수.
inline void dispatchOperation(BackgroundTaskRunner* runner, QObject* receiver,
    BackgroundTaskRunner::Work work, BackgroundTaskRunner::Completion complete) {
    if (runner) runner->submit(receiver, std::move(work), std::move(complete));
    else complete(work());
}
}
