#pragma once
#include <nookbit/application/pending_queue.hpp>
#include "presentation/pending_files_model.hpp"
#include <QThread>
#include <utility>

namespace nookbit {
/// 감시 등 외부 기능의 요청을 기존 대기 목록 정책에 연결하는 조립부 어댑터.
/// 모델 소유 스레드에서 호출하며 모델은 어댑터보다 오래 살아 있어야 한다.
class PendingQueueAdapter final : public IPendingPathsQuery, public IEnqueuePendingPaths {
public:
    /// 조립부가 소유하는 대기 목록을 연결하는 생성자.
    explicit PendingQueueAdapter(PendingFilesModel& model) : model_(model) {}

    /// 화면 자료형을 경로 값으로 변환해 조회 요청에 응답하는 함수.
    Result<std::vector<std::filesystem::path>> pendingPaths() const override {
        if (QThread::currentThread() != model_.thread())
            return Result<std::vector<std::filesystem::path>>::failure({"pending.wrong_thread", "Pending queue requires its owning thread."});
        std::vector<std::filesystem::path> paths;
        for (const auto& path : model_.filePaths()) paths.emplace_back(path.toStdWString());
        return Result<std::vector<std::filesystem::path>>::success(std::move(paths));
    }

    /// 기존 중복·중첩·존재 검사를 재사용하고 각 등록의 확정 결과를 반환하는 함수.
    Result<PendingEnqueueResult> enqueuePaths(const std::vector<std::filesystem::path>& paths) override {
        if (QThread::currentThread() != model_.thread())
            return Result<PendingEnqueueResult>::failure({"pending.wrong_thread", "Pending queue requires its owning thread."});
        PendingEnqueueResult result;
        for (const auto& path : paths) {
            const auto added = model_.addPath(path);
            if (added) result.added.push_back(added.value());
            else result.rejected.push_back({path, added.error()});
        }
        return Result<PendingEnqueueResult>::success(std::move(result));
    }
private:
    PendingFilesModel& model_;
};
}
