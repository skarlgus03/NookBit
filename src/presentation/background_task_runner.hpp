#pragma once
#include <nookbit/application/ports.hpp>
#include <nookbit/application/operation_progress.hpp>
#include <QObject>
#include <QPointer>
#include <QThread>
#include <QString>
#include <functional>
#include <exception>
#include <atomic>

namespace nookbit {
/// 화면 스레드에서 요청을 접수하고 한 작업만 별도 스레드에서 실행한다.
class BackgroundTaskRunner final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool fileWorkActive READ fileWorkActive NOTIFY busyChanged)
    Q_PROPERTY(QString stageLabel READ stageLabel NOTIFY progressChanged)
    Q_PROPERTY(QString currentFileName READ currentFileName NOTIFY progressChanged)
    Q_PROPERTY(QString filePosition READ filePosition NOTIFY progressChanged)
    Q_PROPERTY(bool byteProgressKnown READ byteProgressKnown NOTIFY progressChanged)
    Q_PROPERTY(bool copyPhaseActive READ copyPhaseActive NOTIFY progressChanged)
    Q_PROPERTY(double byteFraction READ byteFraction NOTIFY progressChanged)
    Q_PROPERTY(QString byteProgressText READ byteProgressText NOTIFY progressChanged)
    Q_PROPERTY(bool canCancelCopy READ canCancelCopy NOTIFY progressChanged)
    Q_PROPERTY(bool copyCancellationRequested READ copyCancellationRequested NOTIFY progressChanged)
    Q_PROPERTY(bool copyCancelled READ copyCancelled NOTIFY progressChanged)
    enum class CopyCancellation { Closed, Available, Requested };
public:
    enum class TaskKind { Inspection, FileWork, Sort };
    using Work = std::function<Result<void>()>;
    using Completion = std::function<void(const Result<void>&)>;
    /// 화면 수명의 단일 작업 실행기를 준비하는 함수.
    explicit BackgroundTaskRunner(QObject* parent = nullptr) : QObject(parent) {}
    /// 강제 종료 없이 작업 스레드가 끝난 뒤 자원을 해제하는 함수.
    ~BackgroundTaskRunner() override { if (thread_) { thread_->wait(); delete thread_; } }
    /// 작업 실행 및 결과 전달 대기 여부를 반환하는 함수.
    bool busy() const { return busy_; }
    /// 읽기 전용 검사와 구분해 실제 파일 작업 요청의 실행 여부를 반환하는 함수.
    bool fileWorkActive() const { return busy_ && taskKind_ != TaskKind::Inspection; }
    /// 복사와 복사본 검증·확정에 해당하는 진행 단계인지 반환하는 함수.
    bool copyPhaseActive() const {
        switch (progress_.phase) {
        case OperationPhase::Copying:
        case OperationPhase::FlushingCopy:
        case OperationPhase::VerifyingCopy:
        case OperationPhase::PreservingMetadata:
        case OperationPhase::CommittingDestination:
            return true;
        default:
            return false;
        }
    }
    /// 현재 복사 구간의 취소 버튼을 표시할지 반환하는 함수.
    bool canCancelCopy() const {
        return busy_ && progress_.canCancelCopy && cancellation_.load() == CopyCancellation::Available;
    }
    /// 취소 요청을 받은 뒤 안전하게 멈추는 중인지 알려주는 함수.
    bool copyCancellationRequested() const { return busy_ && cancellation_.load() == CopyCancellation::Requested; }
    /// 가장 최근 작업이 복사 취소로 끝났는지 반환하는 함수.
    bool copyCancelled() const { return progress_.phase == OperationPhase::Cancelled; }
    /// 작업이 아직 확정 전일 때만 복사 취소를 요청하는 함수.
    Q_INVOKABLE void requestCopyCancellation() {
        if (!canCancelCopy()) return;
        auto available = CopyCancellation::Available;
        if (cancellation_.compare_exchange_strong(available, CopyCancellation::Requested)) emit progressChanged();
    }
    /// 화면 스레드에 전달된 최신 진행 값만 반환하는 함수.
    const OperationProgress& progress() const { return progress_; }
    /// 파일명을 전체 경로 없이 화면 문자열로 변환하는 함수.
    QString currentFileName() const { return QString::fromStdString(progress_.fileName); }
    /// 현재 처리 파일 순서와 실제 기록 완료 개수를 표시하는 함수.
    QString filePosition() const {
        if (!progress_.fileCount) return {};
        if (!progress_.fileIndex) return tr("대상 파일 %1개").arg(progress_.fileCount);
        return tr("파일 %1 / %2개 · 완료 %3개").arg(progress_.fileIndex).arg(progress_.fileCount).arg(progress_.completedFiles);
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
        if (copyCancellationRequested()) return tr("복사를 멈추고 있어요");
        switch (progress_.phase) {
        case OperationPhase::Preparing: return tr("정리할 파일 검사 중");
        case OperationPhase::Approving: return tr("승인 내용 검사 중");
        case OperationPhase::CheckingPlan: return tr("정리 계획 확인 중");
        case OperationPhase::CheckingSource: return tr("원본 파일 확인 중");
        case OperationPhase::ProtectingSource: return tr("원본 보호·내용 검증 중");
        case OperationPhase::Copying: return tr("복사 중");
        case OperationPhase::FlushingCopy: return tr("복사 내용 디스크 반영 중");
        case OperationPhase::VerifyingCopy: return tr("복사본 검증 중");
        case OperationPhase::PreservingMetadata: return tr("파일 속성·다운로드 표식 보존 중");
        case OperationPhase::CommittingDestination: return tr("최종 목적지 확정 중");
        case OperationPhase::VerifyingBeforeRemoval: return tr("원본 삭제 전 안전 검사 중");
        case OperationPhase::RemovingSource: return tr("검증된 원본 제거 중");
        case OperationPhase::Moving: return tr("파일 이동 중");
        case OperationPhase::CheckingResult: return tr("이동 결과 확인 중");
        case OperationPhase::SavingHistory: return tr("작업 기록 저장 중");
        case OperationPhase::Restoring: return tr("원래 위치로 되돌리는 중");
        case OperationPhase::Recovering: return tr("중단 작업 확인·복구 중");
        case OperationPhase::Completed: return tr("작업 완료");
        case OperationPhase::Cancelled: return tr("복사 취소됨");
        case OperationPhase::Failed: return tr("작업 실패 · 결과를 확인해 주세요");
        default: return tr("파일 작업 준비 중");
        }
    }
    /// 값으로 고정된 작업을 실행하고 결과만 화면 스레드에 전달하는 함수.
    bool submit(QObject* receiver, Work work, Completion complete, TaskKind kind = TaskKind::Inspection) {
        if (busy_) return false;
        if (thread_) { thread_->wait(); delete thread_; thread_ = nullptr; }
        cancellation_.store(CopyCancellation::Closed);
        progress_ = {}; emit progressChanged();
        taskKind_ = kind;
        busy_ = true; emit busyChanged();
        QPointer<QObject> target(receiver);
        thread_ = QThread::create([this, target, work = std::move(work), complete = std::move(complete), kind] {
            std::function<bool()> cancelled;
            std::function<void(bool)> copyBoundary;
            if (kind == TaskKind::Sort) {
                cancelled = [this] { return cancellation_.load() == CopyCancellation::Requested; };
                // 작업 스레드가 확정 경계를 닫아 늦게 도착한 클릭을 차단하는 함수.
                copyBoundary = [this](bool allowed) {
                    auto previous = allowed ? CopyCancellation::Closed : CopyCancellation::Available;
                    cancellation_.compare_exchange_strong(previous, allowed ? CopyCancellation::Available : CopyCancellation::Closed);
                };
            }
            OperationProgressScope progress([this](const OperationProgress& value) {
                // 값만 복사해 전달하고 화면 객체의 상태는 화면 스레드에서만 수정한다.
                QMetaObject::invokeMethod(this, [this, value] { progress_ = value; emit progressChanged(); }, Qt::QueuedConnection);
            }, std::move(cancelled), std::move(copyBoundary));
            auto result = Result<void>::failure({"background.exception", "Background operation failed."});
            try { result = work(); }
            catch (const std::exception& error) { result = Result<void>::failure({"background.exception", error.what()}); }
            catch (...) {}
            OperationProgressScope::allowCopyCancellation(false);
            OperationProgressScope::phase(result ? OperationPhase::Completed
                : result.error().code == "transfer.cancelled" ? OperationPhase::Cancelled : OperationPhase::Failed);
            QMetaObject::invokeMethod(this, [this, target, complete, result, kind] {
                cancellation_.store(CopyCancellation::Closed);
                busy_ = false; emit busyChanged(); emit progressChanged();
                if (kind != TaskKind::Inspection) emit fileWorkFinished(bool(result));
                if (target) complete(result);
            }, Qt::QueuedConnection);
        });
        thread_->start(); return true;
    }
signals:
    void busyChanged();
    void progressChanged();
    void fileWorkFinished(bool succeeded);
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
    std::atomic<CopyCancellation> cancellation_{CopyCancellation::Closed};
    TaskKind taskKind_{TaskKind::Inspection};
};
/// 실행기를 주입하지 않은 기존 계약 시험은 동기 실행하고 앱은 비동기로 전달하는 함수.
inline void dispatchOperation(BackgroundTaskRunner* runner, QObject* receiver,
    BackgroundTaskRunner::Work work, BackgroundTaskRunner::Completion complete,
    BackgroundTaskRunner::TaskKind kind = BackgroundTaskRunner::TaskKind::Inspection) {
    if (runner) runner->submit(receiver, std::move(work), std::move(complete), kind);
    else complete(work());
}
}
