#pragma once

#include <nookbit/domain/model.hpp>

#include <string>

namespace nookbit {

/// 계획의 모든 승인 관련 입력을 순서가 고정된 바이트 문자열로 만드는 도우미.
class PlanSignature final {
public:
    /// 사용자 설정 전체를 해시할 수 있는 고정 형식으로 직렬화하는 함수.
    [[nodiscard]] static std::string settings(const UserSettings& settings);

    /// 규칙 내용 전체를 해시할 수 있는 고정 형식으로 직렬화하는 함수.
    [[nodiscard]] static std::string rules(const RuleSet& rules);

    /// 파일 경로·지문·판단 결과·경고를 고정 형식으로 직렬화하는 함수.
    [[nodiscard]] static std::string planContents(const OperationPlan& plan);

    /// 실행 상태를 제외한 저장 계획의 불변 입력을 해시용으로 만드는 함수.
    [[nodiscard]] static std::string approvalPayload(const OperationPlan& plan);

    /// 소문자 16진 SHA-256 형식인지 검사하는 함수.
    [[nodiscard]] static bool isSha256Hex(const std::string& digest);
};

} // namespace nookbit
