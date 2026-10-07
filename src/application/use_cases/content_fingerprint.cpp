#include "application/use_cases/content_fingerprint.hpp"

#include "application/use_cases/plan_signature.hpp"

#include <utility>

namespace nookbit {
namespace {

// 파일 내용 해싱 전후에 같은 파일 정보인지 확인하는 함수.
bool sameMetadata(const FileFingerprint& left, const FileFingerprint& right) {
    return left.size == right.size && left.modifiedUtcMs == right.modifiedUtcMs &&
           left.volumeId == right.volumeId && left.fileId == right.fileId;
}

// 선택한 파일 하나의 현재 지문을 읽는 함수.
Result<FileFingerprint> selectedFingerprint(IScanner& scanner,
                                            const std::filesystem::path& path) {
    auto snapshot = scanner.scan({{}, false, {}, {path}});
    if (!snapshot || snapshot.value().files.size() != 1) {
        return Result<FileFingerprint>::failure({
            "hash.file_unavailable", "Cannot inspect selected file for content hashing.",
        });
    }
    return Result<FileFingerprint>::success(snapshot.value().files.front().fingerprint);
}

} // namespace

// 같은 볼륨 빠른 계획을 재생성할 때 검사 정책만 유지하는 함수.
void inheritMetadataRenamePolicy(OperationPlan& current, const OperationPlan& reference) {
    if (current.steps.size() != reference.steps.size()) return;
    const bool metadataOnly = PlanSignature::isMetadataRenamePlan(reference);
    for (size_t i = 0; i < current.steps.size(); ++i) {
        auto& step = current.steps[i];
        if (metadataOnly && step.source == reference.steps[i].source && step.destination == reference.steps[i].destination &&
            step.movement && step.movement->strategy == MoveStrategy::Rename)
            step.movement->strategy = MoveStrategy::MetadataRename;
        if (step.source == reference.steps[i].source && step.destination == reference.steps[i].destination &&
            step.movement && reference.steps[i].movement && step.movement->strategy == MoveStrategy::VerifiedCopy &&
            reference.steps[i].movement->preservation == PreservationPolicy::LocalNtfsBasicWithZone)
            step.movement->preservation = PreservationPolicy::LocalNtfsBasicWithZone;
    }
}

// 재생성한 메타데이터가 같은 복사 단계에만 승인 기대값을 이어 붙이는 함수.
Result<void> inheritExpectedCopyHashes(OperationPlan& current, const OperationPlan& reference) {
    if (!PlanSignature::isFreshVerifiedCopyPlan(current) || !PlanSignature::isFreshVerifiedCopyPlan(reference) ||
        current.steps.size() != reference.steps.size())
        return Result<void>::failure({"verify.copy_scope", "Deferred content checks require a fresh copy-only plan."});
    for (size_t i = 0; i < current.steps.size(); ++i) {
        auto& step = current.steps[i];
        const auto& expected = reference.steps[i];
        if (step.source != expected.source || step.destination != expected.destination ||
            !sameMetadata(step.fingerprint, expected.fingerprint) || !expected.fingerprint.sha256 ||
            !PlanSignature::isSha256Hex(*expected.fingerprint.sha256))
            return Result<void>::failure({"verify.plan_stale", "Copy metadata or expected content digest changed."});
        // 현재 내용의 증거가 아니다. 보호 복사 openApproved에서 반드시 실제 내용과 비교한다.
        step.fingerprint.sha256 = expected.fingerprint.sha256;
    }
    return Result<void>::success();
}

// 이동 예정 파일의 내용 해시를 재검사해 계획에 붙이는 함수.
Result<void> attachContentHashes(OperationPlan& plan, IScanner& scanner, IPlanHasher& hasher) {
    for (auto& step : plan.steps) {
        if (step.status != StepStatus::Planned) continue;
        if (step.movement && step.movement->strategy == MoveStrategy::MetadataRename) continue;
        const auto before = selectedFingerprint(scanner, step.source);
        if (!before || !sameMetadata(step.fingerprint, before.value())) {
            return Result<void>::failure({
                "hash.file_changed", "Selected file changed before content hashing.",
            });
        }
        auto digest = hasher.sha256File(step.source);
        if (!digest) return Result<void>::failure(digest.error());
        const auto after = selectedFingerprint(scanner, step.source);
        if (!after || !sameMetadata(before.value(), after.value()) ||
            !PlanSignature::isSha256Hex(digest.value())) {
            return Result<void>::failure({
                "hash.file_changed", "Selected file changed while content was hashed.",
            });
        }
        step.fingerprint.sha256 = std::move(digest.value());
    }
    return Result<void>::success();
}

} // namespace nookbit
