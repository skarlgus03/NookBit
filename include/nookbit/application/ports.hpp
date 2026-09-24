#pragma once

#include <nookbit/application/events.hpp>
#include <nookbit/domain/model.hpp>
#include <nookbit/domain/result.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nookbit {

struct ScanRequest {
    std::vector<std::filesystem::path> roots;
    bool recursive{false};
    std::vector<std::string> excludePatterns;
    std::vector<std::filesystem::path> selectedFiles;
};

struct ScanSnapshot {
    std::string id;
    std::vector<FileFact> files;
    std::vector<Error> warnings;
};

class IScanner {
public:
    /// 구현체를 인터페이스 포인터로 안전하게 정리하는 소멸자.
    virtual ~IScanner() = default;

    /// 선택 파일 또는 감시 폴더에서 파일 정보를 변경 없이 수집하는 함수.
    [[nodiscard]] virtual Result<ScanSnapshot> scan(const ScanRequest& request) = 0;
};

class IRuleEngine {
public:
    /// 구현체를 인터페이스 포인터로 안전하게 정리하는 소멸자.
    virtual ~IRuleEngine() = default;

    /// 파일을 현재 규칙과 비교해 일치 결과 또는 미분류 결과를 반환하는 함수.
    [[nodiscard]] virtual Result<std::optional<RuleDecision>> decide(
        const FileFact& file,
        const RuleSet& rules,
        MultiMatchPolicy multiMatchPolicy,
        std::int64_t nowUtcMs) const = 0;
};

class IPlanner {
public:
    /// 구현체를 인터페이스 포인터로 안전하게 정리하는 소멸자.
    virtual ~IPlanner() = default;

    /// 스캔 결과와 실제 규칙으로 이동 없는 미리보기 계획을 만드는 함수.
    [[nodiscard]] virtual Result<OperationPlan> buildPlan(
        const ScanSnapshot& snapshot,
        const UserSettings& settings,
        const RuleSet& rules) = 0;
};

class IExecutor {
public:
    /// 구현체를 인터페이스 포인터로 안전하게 정리하는 소멸자.
    virtual ~IExecutor() = default;
    /// 승인된 계획의 선택 파일을 검증하고 실행하는 함수.
    [[nodiscard]] virtual Result<void> execute(
        const OperationPlan& plan,
        const ApprovalRecord& approval,
        const std::vector<std::filesystem::path>& selectedFiles) = 0;
    /// 완료된 계획의 파일을 원래 위치로 되돌리는 함수.
    [[nodiscard]] virtual Result<void> rollback(const PlanId& planId) = 0;
};

class IHistoryRepository {
public:
    /// 구현체를 인터페이스 포인터로 안전하게 정리하는 소멸자.
    virtual ~IHistoryRepository() = default;
    /// 계획을 저장하는 함수.
    [[nodiscard]] virtual Result<void> savePlan(const OperationPlan& plan) = 0;
    /// 명시적으로 승인된 계획을 기록하는 함수.
    [[nodiscard]] virtual Result<void> saveApproval(const ApprovalRecord& approval) = 0;
    /// 실행 단계 상태와 이벤트를 한 트랜잭션에 기록하는 함수.
    [[nodiscard]] virtual Result<void> appendEvent(const DomainEvent& event) = 0;
    /// 실행 또는 복구가 필요한 계획들을 읽는 함수.
    [[nodiscard]] virtual Result<std::vector<OperationPlan>> incompletePlans() const = 0;
    /// 완료된 계획까지 포함해 ID 하나로 읽는 함수.
    [[nodiscard]] virtual Result<OperationPlan> loadPlan(const PlanId& planId) const = 0;
};

/// 계획 스냅샷의 변조 검출용 SHA-256을 제공하는 포트.
class IPlanHasher {
public:
    /// 구현체를 인터페이스 포인터로 안전하게 정리하는 소멸자.
    virtual ~IPlanHasher() = default;

    /// 입력 바이트의 소문자 16진 SHA-256을 반환하는 함수.
    [[nodiscard]] virtual std::string sha256(std::string_view bytes) const = 0;
};

class ISettingsRepository {
public:
    virtual ~ISettingsRepository() = default;
    [[nodiscard]] virtual Result<UserSettings> load() const = 0;
    [[nodiscard]] virtual Result<void> save(const UserSettings& settings) = 0;
};

/// 분류 규칙 묶음을 불러오는 저장소 계약.
class IRuleSetRepository {
public:
    /// 구현체를 인터페이스 포인터로 안전하게 정리하는 소멸자.
    virtual ~IRuleSetRepository() = default;

    /// 현재 분류 규칙 묶음을 읽어 반환하는 함수.
    [[nodiscard]] virtual Result<RuleSet> load() const = 0;
};

} // namespace nookbit
