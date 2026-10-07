#pragma once
#include <nookbit/application/ports.hpp>
#include <nookbit/application/operation_progress.hpp>
#include <QObject>
#include <QPointer>
#include <QThread>
#include <QString>
#include <functional>
#include <exception>

namespace nookbit {
/// 화면 스레드에서 요청을 접수하고 한 작업만 별도 스레드에서 실행한다.
class BackgroundTaskRunner final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString stageLabel READ stageLabel NOTIFY progressChanged)
    Q_PROPERTY(QString currentFileName READ currentFileName NOTIFY progressChanged)
    Q_PROPERTY(QString filePosition READ filePosition NOTIFY progressChanged)
    Q_PROPERTY(bool byteProgressKnown READ byteProgressKnown NOTIFY progressChanged)
    Q_PROPERTY(double byteFraction READ byteFraction NOTIFY progressChanged)
    Q_PROPERTY(QString byteProgressText READ byteProgressText NOTIFY progressChanged)
public:
    using Work = std::function<Result<void>()>;
    using Completion = std::function<void(const Result<void>&)>;
    /// 화면 수명의 단일 작업 실행기를 준비하는 함수.
    explicit BackgroundTaskRunner(QObject* parent = nullptr) : QObject(parent) {}
    /// 강제 종료 없이 작업 스레드가 끝난 뒤 자원을 해제하는 함수.
    ~BackgroundTaskRunner() override { if (thread_) { thread_->wait(); delete thread_; } }
    /// 작업 실행 및 결과 전달 대기 여부를 반환하는 함수.
    bool busy() const { return busy_; }
    /// 화면 스레드에 전달된 최신 진행 값만 반환하는 함수.
    const OperationProgress& progress() const { return progress_; }
    /// 파일명을 전체 경로 없이 화면 문자열로 변환하는 함수.
    QString currentFileName() const { return QString::fromStdString(progress_.fileName); }
    /// 현재 처리 파일 순서와 실제 기록 완료 개수를 표시하는 함수.
    QString filePosition() const {
        if (!progress_.fileCount) return {};
        if (!progress_.fileIndex) return QStringLiteral("대상 파일 %1개").arg(progress_.fileCount);
        return QStringLiteral("파일 %1 / %2개 · 완료 %3개").arg(progress_.fileIndex).arg(progress_.fileCount).arg(progress_.completedFiles);
    }
    /// 크기가 알려진 현재 복사 구간만 퍼센트 표시 대상으로 반환하는 함수.
    bool byteProgressKnown() const { return progress_.phase == OperationPhase::Copying && progress_.byteProgressKnown; }
    /// 현재 파일 복사 비율만 반환하며 검증·삭제에는 비율을 만들어내지 않는 함수.
    double byteFraction() const { return byteProgressKnown() ? static_cast<double>(progress_.copiedBytes) / static_cast<double>(progress_.totalBytes) : 0; }
    /// 복사량을 사람이 읽을 수 있는 단위와 퍼센트로 표시하는 함수.
    QString byteProgressText() const {
        if (!byteProgressKnown()) return {};
        return QStringLiteral("%1 / %2 · %3%")
            .arg(formatBytes(progress_.copiedBytes), formatBytes(progress_.totalBytes)).arg(byteFraction() * 100, 0, 'f', 1);
    }
    /// 백엔드 처리 단계를 한국어 화면 문구로만 변환하는 함수.
    QString stageLabel() const {
        switch (progress_.phase) {
        case OperationPhase::Preparing: return QStringLiteral("정리할 파일 검사 중");
        case OperationPhase::Approving: return QStringLiteral("승인 내용 검사 중");
        case OperationPhase::CheckingPlan: return QStringLiteral("정리 계획 확인 중");
        case OperationPhase::CheckingSource: return QStringLiteral("원본 파일 확인 중");
        case OperationPhase::ProtectingSource: return QStringLiteral("원본 보호·내용 검증 중");
        case OperationPhase::Copying: return QStringLiteral("복사 중");
        case OperationPhase::FlushingCopy: return QStringLiteral("복사 내용 디스크 반영 중");
        case OperationPhase::VerifyingCopy: return QStringLiteral("복사본 검증 중");
        case OperationPhase::PreservingMetadata: return QStringLiteral("파일 속성·다운로드 표식 보존 중");
        case OperationPhase::CommittingDestination: return QStringLiteral("최종 목적지 확정 중");
        case OperationPhase::VerifyingBeforeRemoval: return QStringLiteral("원본 삭제 전 안전 검사 중");
        case OperationPhase::RemovingSource: return QStringLiteral("검증된 원본 제거 중");
        case OperationPhase::Moving: return QStringLiteral("파일 이동 중");
        case OperationPhase::CheckingResult: return QStringLiteral("이동 결과 확인 중");
        case OperationPhase::SavingHistory: return QStringLiteral("작업 기록 저장 중");
        case OperationPhase::Restoring: return QStringLiteral("원래 위치로 되돌리는 중");
        case OperationPhase::Recovering: return QStringLiteral("중단 작업 확인·복구 중");
        case OperationPhase::Completed: return QStringLiteral("작업 완료");
        case OperationPhase::Failed: return QStringLiteral("작업 실패 · 결과를 확인해 주세요");
        default: return QStringLiteral("파일 작업 준비 중");
        }
    }
    /// 값으로 고정된 작업을 실행하고 결과만 화면 스레드에 전달하는 함수.
    bool submit(QObject* receiver, Work work, Completion complete) {
        if (busy_) return false;
        if (thread_) { thread_->wait(); delete thread_; thread_ = nullptr; }
        progress_ = {}; emit progressChanged();
        busy_ = true; emit busyChanged();
        QPointer<QObject> target(receiver);
        thread_ = QThread::create([this, target, work = std::move(work), complete = std::move(complete)] {
            OperationProgressScope progress([this](const OperationProgress& value) {
                // 값만 복사해 전달하고 화면 객체의 상태는 화면 스레드에서만 수정한다.
                QMetaObject::invokeMethod(this, [this, value] { progress_ = value; emit progressChanged(); }, Qt::QueuedConnection);
            });
            auto result = Result<void>::failure({"background.exception", "Background operation failed."});
            try { result = work(); }
            catch (const std::exception& error) { result = Result<void>::failure({"background.exception", error.what()}); }
            catch (...) {}
            OperationProgressScope::phase(result ? OperationPhase::Completed : OperationPhase::Failed);
            QMetaObject::invokeMethod(this, [this, target, complete, result] {
                busy_ = false; emit busyChanged();
                if (target) complete(result);
            }, Qt::QueuedConnection);
        });
        thread_->start(); return true;
    }
signals:
    void busyChanged();
    void progressChanged();
private:
    /// 표시 단위를 이진 기준으로 변환하며 바이트 크기를 잘라내지 않는 함수.
    static QString formatBytes(std::uintmax_t bytes) {
        if (bytes < 1024) return QStringLiteral("%1 B").arg(bytes);
        double value = static_cast<double>(bytes); int unit = 0;
        const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
        while (value >= 1024 && unit < 4) { value /= 1024; ++unit; }
        return QStringLiteral("%1 %2").arg(value, 0, 'f', 2).arg(QString::fromLatin1(units[unit]));
    }
    OperationProgress progress_;
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
