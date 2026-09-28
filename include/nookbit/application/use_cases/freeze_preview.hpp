#pragma once

#include <nookbit/domain/model.hpp>
#include <nookbit/domain/result.hpp>

namespace nookbit {

/// 미리보기의 현재 입력을 재검사한 뒤 계획을 저장하는 계약.
class IFreezePreviewUseCase {
public:
    /// 구현체를 인터페이스 포인터로 안전하게 정리하는 소멸자.
    virtual ~IFreezePreviewUseCase() = default;

    /// 초안·선택 파일·이번 설정을 검증하고 계획을 저장하는 함수.
    [[nodiscard]] virtual Result<OperationPlan> execute(
        const OperationPlan& draft,
        const std::vector<std::filesystem::path>& selectedFiles,
        const UserSettings& settings) = 0;
};

} // namespace nookbit
