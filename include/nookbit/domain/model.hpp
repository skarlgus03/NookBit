#pragma once

#include <nookbit/domain/result.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <memory>
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
    // 폴더는 내부 파일을 개별 분류하지 않는 하나의 이동 대상이다.
    bool directory{false};
};

struct FileFact {
    std::filesystem::path absolutePath;
    std::filesystem::path relativePath;
    std::string extension;
    FileFingerprint fingerprint;
    // 명시적으로 선택한 처리 흐름과 순회 이후의 재개 위치. 비어 있으면 기본 분류다.
    std::string processingFlowId;
    std::string processingAfterNode;
};

struct RuleDecision {
    std::string matchedRuleId;
    std::filesystem::path destinationRelativePath;
    std::string reason;
    std::optional<CollisionPolicy> collisionPolicy;
    bool excluded{false};
    bool absoluteDestination{false};
};

/// 규칙 시험 결과와 실제로 선택된 노드 ID의 순서를 표현한다.
struct RuleTrace {
    std::optional<RuleDecision> decision;
    std::vector<std::string> selectedNodeIds;
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

/// 파일 또는 폴더 중 한 대상 종류를 선택하는 조건.
enum class ItemType { File, Folder };
struct ItemTypePredicate { ItemType value{ItemType::File}; };

/// 원본 대상이 지정한 폴더 안에 있는지 경로 구성요소로 비교하는 조건.
struct SourceFolderPredicate {
    std::filesystem::path folder;
    bool recursive{true};
};

struct LogicalPredicate;
using LogicalPredicatePtr = std::shared_ptr<const LogicalPredicate>;

using AtomicRuleCondition = std::variant<ExtensionPredicate, NameContainsPredicate,
                                         SizePredicate, ModifiedAgePredicate, ItemTypePredicate, SourceFolderPredicate, LogicalPredicatePtr>;

/// 조건 노드들의 AND 또는 OR 계산만 표현하며 실행 순서는 소유하지 않는다.
struct LogicalPredicate {
    bool all{true};
    std::vector<AtomicRuleCondition> operands;
};

/// 리프 또는 전체 예외가 정한 목적지와 충돌 정책을 표현한다.
struct RuleDestination {
    std::filesystem::path relativePath;
    std::optional<CollisionPolicy> collisionPolicy;
    bool absolute{false};
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
    bool exclude{false};
};

/// 조건·순회·다른 흐름 호출을 연결하는 Qt 독립 처리 노드.
struct ProcessingNode {
    std::string id;
    std::string type;
    std::vector<std::string> next;
    std::optional<AtomicRuleCondition> condition;
    std::optional<RuleDestination> destination;
    std::string targetFlow;
    bool recursive{true};
    // 선택한 폴더를 0단계로 세는 하위 폴더 깊이 제한.
    std::optional<int> maxDepth{};
};

/// 버전이 있는 분류 규칙 묶음을 표현한다.
struct RuleSet {
    std::string id;
    RuleSetVersion version;
    std::vector<ClassificationRule> rules;
    int schemaVersion{1};
    std::vector<RuleNode> nodes;
    std::optional<RuleDestination> globalOtherwise;
    /// 이름 있는 처리 그래프. 노드가 없으면 이전 기본 프리셋으로 읽는다.
    struct FolderFlow {
        std::string id;
        std::string name;
        std::vector<ProcessingNode> nodes;
        std::string entry;
        bool folderInput{true};
    };
    std::vector<FolderFlow> folderFlows;
};

/// 대기 항목과 사용자가 명시적으로 선택한 폴더 흐름을 함께 보관한다.
struct PendingFileEntry {
    std::filesystem::path path;
    std::string folderFlowId;
    std::string ruleSetId;
    bool operator==(const PendingFileEntry&) const = default;
};

/// 미리보기에서 확정한 개별 원본의 분류 흐름 재개 위치.
struct ProcessingSource {
    std::filesystem::path source;
    std::string flowId;
    std::string afterNode;
};

/// 순수 흐름 판단 결과. 실제 순회는 스캐너에 별도로 요청한다.
struct ProcessingRoute {
    std::optional<RuleDecision> decision;
    bool traverse{false};
    std::string flowId;
    std::string nodeId;
    bool recursive{true};
    std::optional<int> maxDepth{};
};

/// 승인에 고정할 이동 방식이며 실행 실패 시 다른 방식으로 바꾸지 않는다.
enum class MoveStrategy { Rename, VerifiedCopy, MetadataRename };

/// 다른 볼륨 복사에서 현재 지원하는 메타데이터 보존 범위.
enum class PreservationPolicy { LocalNtfsBasic, LocalNtfsBasicWithZone };

/// 드라이브 문자와 별개로 승인 시 확인한 실제 볼륨을 표현한다.
struct PlanVolume {
    std::string serial;
    std::string guidPath;
    std::string filesystem;
};

/// v2 승인에 포함되는 이동 방식과 양쪽 볼륨의 불변 정보.
struct MoveContract {
    MoveStrategy strategy{MoveStrategy::Rename};
    PlanVolume sourceVolume;
    PlanVolume destinationVolume;
    PreservationPolicy preservation{PreservationPolicy::LocalNtfsBasic};
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
    std::optional<MoveContract> movement;
    std::optional<Error> failure;
};

/// 화면에서 파일별 분류 결과를 구분하는 미리보기 전용 상태.
enum class PreviewDisposition { Planned, CollisionSkipped, Unmatched, Excluded };

/// 실행 계획과 별개로 미분류·제외까지 보여주는 일시적인 파일별 판단 결과.
struct PreviewFileOutcome {
    std::filesystem::path source;
    std::optional<std::filesystem::path> destination;
    std::string matchedRuleId;
    PreviewDisposition disposition{PreviewDisposition::Unmatched};
};

struct OperationPlan {
    PlanId id;
    RuleSetVersion ruleSetVersion;
    std::string settingsRevision;
    std::string rulesRevision;
    std::string planHash;
    std::vector<MoveStep> steps;
    // 화면 표시 전용이며 승인 서명이나 SQLite 실행 기록에는 포함하지 않는다.
    std::vector<PreviewFileOutcome> previewOutcomes;
    std::vector<Error> warnings;
    PlanStatus status{PlanStatus::Draft};
    // 기존 저장 계획은 v1 그대로 읽어 승인 해시를 유지한다.
    int formatVersion{1};
    std::vector<ProcessingSource> processingSources;
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
    // 규칙 파일과 별개로 사용자가 직접 허용한 추가 목적지 폴더.
    std::vector<std::filesystem::path> allowedDestinationRoots;
};

struct SafetyInvariants final {
    static constexpr bool approvalRequired = true;
    static constexpr bool permanentDeleteAllowed = false;
    static constexpr bool silentOverwriteAllowed = false;
    static constexpr bool followReparsePoints = false;
};

} // namespace nookbit
