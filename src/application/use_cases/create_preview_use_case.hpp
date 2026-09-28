#pragma once

#include <nookbit/application/ports.hpp>
#include <nookbit/application/use_cases/create_preview.hpp>

namespace nookbit {

/// 대기 파일 검사와 계획 생성을 순서대로 조율하는 유스케이스.
class CreatePreviewUseCase final : public ICreatePreviewUseCase {
public:
    /// 미리보기 생성에 필요한 외부 기능을 주입받는 생성자.
    CreatePreviewUseCase(
        ISettingsRepository& settings,
        IRuleSetRepository& rules,
        IScanner& scanner,
        IPlanner& planner,
        IPlanHasher* hasher = nullptr);

    /// 요청에 포함된 목적지를 사용하도록 외부 일꾼만 받는 생성자.
    CreatePreviewUseCase(
        IRuleSetRepository& rules,
        IScanner& scanner,
        IPlanner& planner,
        IPlanHasher* hasher = nullptr);

    /// 선택한 파일과 요청 목적지·규칙으로 정리 미리보기를 생성하는 함수.
    [[nodiscard]] Result<OperationPlan> execute(
        const CreatePreviewRequest& request) override;

private:
    ISettingsRepository* settings_;
    IRuleSetRepository& rules_;
    IScanner& scanner_;
    IPlanner& planner_;
    IPlanHasher* hasher_;
};

} // namespace nookbit
