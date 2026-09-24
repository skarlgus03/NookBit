#pragma once

#include <nookbit/domain/model.hpp>
#include <nookbit/domain/result.hpp>

namespace nookbit {

/// 정리 미리보기에 사용할 규칙 버전과 대기 파일 목록을 전달하는 요청.
struct CreatePreviewRequest {
    RuleSetVersion ruleSetVersion;
    std::vector<std::filesystem::path> selectedFiles;
};

/// 파일을 변경하지 않고 정리 계획을 생성하는 입력 인터페이스.
class ICreatePreviewUseCase {
public:
    /// 파생 구현을 인터페이스 포인터로 안전하게 정리하기 위한 소멸자.
    virtual ~ICreatePreviewUseCase() = default;

    /// 선택한 파일과 현재 규칙으로 정리 미리보기를 생성하는 함수.
    [[nodiscard]] virtual Result<OperationPlan> execute(
        const CreatePreviewRequest& request) = 0;
};

} // namespace nookbit
