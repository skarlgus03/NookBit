#pragma once

#include <nookbit/domain/result.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <variant>
#include <string>
#include <vector>

namespace nookbit {

using PlanId = std::string;
using StepId = std::string;
using RuleSetVersion = std::string;

enum class CollisionPolicy {
    Rename,
    Skip,
    Abort,
};

enum class MultiMatchPolicy {
    FirstMatch,
    Abort,
};

enum class UnmatchedPolicy {
    LeaveInPlace,
    Abort,
};

enum class PlanStatus {
    Draft,
    Planned,
    AwaitingApproval,
    Approved,
    Executing,
    Completed,
    RollingBack,
    RolledBack,
    NeedsRecovery,
    Failed,
    Cancelled,
};

enum class StepStatus {
    Planned,
    Skipped,
    IntentRecorded,
    Applied,
    RollbackIntentRecorded,
    RolledBack,
    NeedsRecovery,
    Failed,
};

struct FileFingerprint {
    std::uintmax_t size{0};
    std::int64_t modifiedUtcMs{0};
    std::optional<std::string> volumeId;
    std::optional<std::string> fileId;
    std::optional<std::string> sha256;
};

struct FileFact {
    std::filesystem::path absolutePath;
    std::filesystem::path relativePath;
    std::string extension;
    FileFingerprint fingerprint;
};

struct RuleDecision {
    std::string matchedRuleId;
    std::filesystem::path destinationRelativePath;
    std::string reason;
    std::optional<CollisionPolicy> collisionPolicy;
};

/// JSON 분류 규칙 하나를 코어에서 사용할 수 있게 표현한다.
struct ClassificationRule {
    std::string id;
    bool enabled{false};
    int priority{0};
    std::vector<std::string> extensions;
    std::vector<std::string> fileNameGlobs;
    std::optional<std::uintmax_t> minimumBytes;
    std::optional<std::uintmax_t> maximumBytes;
    std::optional<int> minimumAgeDays;
    std::optional<int> maximumAgeDays;
    std::filesystem::path destinationRelativePath;
    std::optional<CollisionPolicy> collisionPolicy;
};

/// 트리 노드 하나에서 검사할 파일 조건을 표현한다.
struct RuleCondition {
    std::vector<std::string> extensions;
    std::vector<std::string> fileNameGlobs;
    std::optional<std::uintmax_t> minimumBytes;
    std::optional<std::uintmax_t> maximumBytes;
    std::optional<std::uintmax_t> greaterThanBytes;
    std::optional<int> minimumAgeDays;
    std::optional<int> maximumAgeDays;
};

/// v3 노드에서 한 종류의 확장자 조건을 표현한다.
struct ExtensionPredicate {
    std::vector<std::string> values;
};

/// v3 노드에서 파일명에 포함될 문자열을 표현한다.
struct NameContainsPredicate {
    std::string text;
};

enum class SizeComparison { GreaterThan, AtLeast, LessThan, AtMost };

/// v3 노드에서 파일 크기 비교 하나를 표현한다.
struct SizePredicate {
    SizeComparison comparison{SizeComparison::GreaterThan};
    std::uintmax_t bytes{0};
};

enum class AgeComparison { AtLeast, LessThan };

/// v3 노드에서 수정 후 경과일 비교 하나를 표현한다.
struct ModifiedAgePredicate {
    AgeComparison comparison{AgeComparison::AtLeast};
    int days{0};
};

/// v3 노드는 이 네 조건 중 정확히 하나만 가진다.
using AtomicRuleCondition = std::variant<ExtensionPredicate, NameContainsPredicate,
                                         SizePredicate, ModifiedAgePredicate>;

/// 리프 또는 전체 예외가 정한 목적지와 충돌 정책을 표현한다.
struct RuleDestination {
    std::filesystem::path relativePath;
    std::optional<CollisionPolicy> collisionPolicy;
};

/// 순서 있는 조건 가지와 같은 높이의 else 가지를 표현한다.
struct RuleNode {
    std::string id;
    bool enabled{true};
    bool otherwise{false};
    RuleCondition condition;
    std::optional<AtomicRuleCondition> atomicCondition;
    std::optional<RuleDestination> destination;
    std::vector<RuleNode> children;
};

/// 버전이 있는 분류 규칙 묶음을 표현한다.
struct RuleSet {
    std::string id;
    RuleSetVersion version;
    std::vector<ClassificationRule> rules;
    int schemaVersion{1};
    std::vector<RuleNode> nodes;
    std::optional<RuleDestination> globalOtherwise;
};

struct MoveStep {
    StepId id;
    std::filesystem::path source;
    std::filesystem::path destination;
    FileFingerprint fingerprint;
    std::string matchedRuleId;
    std::string reason;
    CollisionPolicy collisionPolicy{CollisionPolicy::Rename};
    StepStatus status{StepStatus::Planned};
};

struct OperationPlan {
    PlanId id;
    RuleSetVersion ruleSetVersion;
    std::string settingsRevision;
    std::string rulesRevision;
    std::string planHash;
    std::vector<MoveStep> steps;
    std::vector<Error> warnings;
    PlanStatus status{PlanStatus::Draft};
};

/// 저장된 작업 목록에 보여줄 최소한의 이력 정보.
struct PlanHistoryEntry {
    PlanId id;
    std::string lastActivityUtc;
    PlanStatus status{PlanStatus::Draft};
    int fileCount{0};
    int appliedCount{0};
};

struct ApprovalRecord {
    PlanId planId;
    std::string planHash;
    std::chrono::system_clock::time_point approvedAt;
};

struct UserSettings {
    std::filesystem::path homeRoot;
    std::vector<std::filesystem::path> watchRoots;
    std::vector<std::string> excludePatterns;
    CollisionPolicy defaultCollisionPolicy{CollisionPolicy::Rename};
    MultiMatchPolicy multiMatchPolicy{MultiMatchPolicy::FirstMatch};
    UnmatchedPolicy unmatchedPolicy{UnmatchedPolicy::LeaveInPlace};
    bool recursiveScan{false};
};

struct SafetyInvariants final {
    static constexpr bool approvalRequired = true;
    static constexpr bool permanentDeleteAllowed = false;
    static constexpr bool silentOverwriteAllowed = false;
    static constexpr bool followReparsePoints = false;
};

} // namespace nookbit
