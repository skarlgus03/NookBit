#include <nookbit/application/inspect_interrupted_plan_use_case.hpp>
#include "application/use_cases/plan_signature.hpp"
#include <iostream>
#include <map>

namespace {
using namespace nookbit;
int failures = 0;
// 실패한 조건을 누적해 전체 검사 결과를 반환하도록 돕는 함수.
void expect(bool value, const char* message) {
    if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
// 운영체제에 맞는 가상 절대 경로를 만드는 함수.
std::filesystem::path path(const char* relative) {
#ifdef _WIN32
    return std::filesystem::path("C:/virtual") / relative;
#else
    return std::filesystem::path("/virtual") / relative;
#endif
}

struct History final : IHistoryRepository {
    OperationPlan plan;
    PlanStatus origin{PlanStatus::Executing};
    std::vector<OperationPlan> listed;
    int writes{0};
    // 의도치 않은 계획 저장을 세는 함수.
    Result<void> savePlan(const OperationPlan&) override { ++writes; return Result<void>::success(); }
    // 의도치 않은 승인 저장을 세는 함수.
    Result<void> saveApproval(const ApprovalRecord&) override { ++writes; return Result<void>::success(); }
    // 의도치 않은 상태 갱신을 세는 함수.
    Result<void> appendEvent(const DomainEvent&) override { ++writes; return Result<void>::success(); }
    // 준비된 작업 목록을 반환하는 함수.
    Result<std::vector<OperationPlan>> incompletePlans() const override { return Result<std::vector<OperationPlan>>::success(listed); }
    // 준비된 작업 하나를 읽는 함수.
    Result<OperationPlan> loadPlan(const PlanId&) const override { return Result<OperationPlan>::success(plan); }
    // 이 시험에 필요 없는 최근 목록을 반환하는 함수.
    Result<std::vector<PlanHistoryEntry>> recentPlans(int) const override { return Result<std::vector<PlanHistoryEntry>>::success({}); }
    // 중단 직전의 실행 방향을 반환하는 함수.
    Result<PlanStatus> interruptedStatus(const PlanId&) const override { return Result<PlanStatus>::success(origin); }
};
struct Journal final : ITransferJournal {
    std::vector<TransferAttempt> attempts;
    int writes{0};
    // 의도치 않은 전송 시작 기록을 세는 함수.
    Result<void> begin(const TransferAttempt&) override { ++writes; return Result<void>::success(); }
    // 의도치 않은 전송 상태 갱신을 세는 함수.
    Result<void> advance(const std::string&, const TransferUpdate&) override { ++writes; return Result<void>::success(); }
    // ID가 일치하는 시험 증거만 읽는 함수.
    Result<TransferAttempt> load(const std::string& id) const override {
        for (const auto& item : attempts) if (item.id == id) return Result<TransferAttempt>::success(item);
        return Result<TransferAttempt>::failure({"test.missing", "Attempt missing"});
    }
    // 지정한 단계의 시험 증거만 읽는 함수.
    Result<std::vector<TransferAttempt>> forStep(const StepId& id) const override {
        std::vector<TransferAttempt> result;
        for (const auto& item : attempts) if (item.stepId == id) result.push_back(item);
        return Result<std::vector<TransferAttempt>>::success(result);
    }
};
struct Files final : ITransferFileInspector {
    std::map<std::filesystem::path, TransferFileObservation> values;
    int reads{0};
    // 등록한 관찰값만 반환하고 파일 시스템에는 접근하지 않는 함수.
    TransferFileObservation inspect(const std::filesystem::path& file) override { ++reads; return values.at(file); }
};
struct Hasher final : IPlanHasher {
    std::string expected;
    // 준비된 승인 바이트와 정확히 같을 때만 기존 해시를 반환하는 함수.
    std::string sha256(std::string_view bytes) const override { return std::string(64, bytes == expected ? 'a' : 'b'); }
    // 파일 해시 조회를 이 시험에서 거부하는 함수.
    Result<std::string> sha256File(const std::filesystem::path&) const override { return Result<std::string>::failure({"test.unused", "Unused"}); }
};
struct Volumes final : ITransferVolumeInspector {
    bool replaced{false};
    // 가상 경로별 승인 볼륨을 읽기 전용으로 반환하는 함수.
    Result<TransferVolume> inspectVolume(const std::filesystem::path& file) override {
        const bool source = file.parent_path().filename() == "incoming" && file.filename() != "s1";
        return Result<TransferVolume>::success({replaced ? "9" : source ? "1" : "2",
            source ? L"\\\\?\\Volume{aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa}\\" : L"\\\\?\\Volume{bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb}\\", L"NTFS", true});
    }
};

struct Fixture {
    History history;
    Journal journal;
    Files files;
    Hasher hasher;
    Volumes volumes;
    // 가상 혼합 계획과 온전한 원본 관찰값을 준비하는 함수.
    Fixture() {
        auto& plan = history.plan;
        plan.id = "plan"; plan.formatVersion = 2; plan.status = PlanStatus::Executing;
        plan.ruleSetVersion = "rules"; plan.rulesRevision = plan.settingsRevision = std::string(64, 'c');
        const PlanVolume a{"1", "\\\\?\\Volume{aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa}\\", "NTFS"};
        const PlanVolume b{"2", "\\\\?\\Volume{bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb}\\", "NTFS"};
        for (int i = 0; i < 3; ++i) {
            MoveStep step;
            step.id = "s" + std::to_string(i); step.source = path("incoming") / step.id; step.destination = path("target") / step.id;
            step.fingerprint = {10, 20, i == 1 ? "2" : "1", step.id, std::string(64, 'd')};
            step.movement = MoveContract{i == 1 ? MoveStrategy::Rename : MoveStrategy::VerifiedCopy, i == 1 ? b : a, b, PreservationPolicy::LocalNtfsBasic};
            plan.steps.push_back(step);
            present(step.source, step.fingerprint); missing(step.destination, "2");
        }
        seal();
    }
    // 바뀐 불변 입력에 맞는 시험 서명을 고정하는 함수.
    void seal() { hasher.expected = PlanSignature::approvalPayload(history.plan); history.plan.planHash = std::string(64, 'a'); }
    // 특정 가상 객체의 존재를 등록하는 함수.
    void present(const std::filesystem::path& file, const FileFingerprint& fp) { files.values[file] = {TransferPathPresence::Present, fp.volumeId, fp, {}}; }
    // 조회 가능한 올바른 볼륨의 부재를 등록하는 함수.
    void missing(const std::filesystem::path& file, const std::string& volume) { files.values[file] = {TransferPathPresence::Missing, volume, {}, {}}; }
    // 원본 지문에 연결되는 정방향 전송 증거를 만드는 함수.
    TransferAttempt forward(int i, TransferPhase phase) {
        const auto& step = history.plan.steps[i];
        TransferAttempt attempt;
        attempt.id = "f" + step.id; attempt.planId = history.plan.id; attempt.stepId = step.id;
        attempt.source = step.source; attempt.destination = step.destination; attempt.staging = step.destination.parent_path() / (step.id + ".tmp");
        attempt.sourceFingerprint = step.fingerprint; attempt.phase = phase;
        missing(attempt.staging, "2");
        return attempt;
    }
    // 기록까지 완료된 복사 결과를 준비하는 함수.
    void movedCopy(int i) {
        auto attempt = forward(i, TransferPhase::Completed);
        auto final = attempt.sourceFingerprint; final.volumeId = "2"; final.fileId = "new-" + attempt.id;
        attempt.destinationFingerprint = final; attempt.stagingFingerprint = final;
        missing(attempt.source, "1"); present(attempt.destination, final);
        history.plan.steps[i].status = StepStatus::Applied;
        journal.attempts.push_back(attempt);
    }
    // 진단을 실행하고 성공 여부와 무변경 계약을 확인하는 함수.
    InterruptedPlanInspection inspect() {
        InspectInterruptedPlanUseCase useCase(history, journal, files, hasher, &volumes);
        const auto report = useCase.execute(history.plan.id);
        expect(report.hasValue(), "inspection should return a report");
        expect(history.writes == 0 && journal.writes == 0, "inspection must not write any state");
        return report ? report.value() : InterruptedPlanInspection{};
    }
};

// 완료·대기·부분 복사를 같은 작업에서 분리하고 자동 완료하지 않는지 검사하는 함수.
void testMixedProgress() {
    Fixture f; f.movedCopy(0);
    auto partial = f.forward(2, TransferPhase::StagingCreated);
    partial.stagingFingerprint = FileFingerprint{0, 20, "2", "partial", {}};
    auto actual = *partial.stagingFingerprint; actual.size = 3; actual.sha256 = std::string(64, 'd');
    f.present(partial.staging, actual); f.journal.attempts.push_back(partial);
    f.history.plan.steps[2].status = StepStatus::IntentRecorded;
    const auto report = f.inspect();
    expect(report.moved == 1 && report.pending == 1 && report.needsAttention == 1, "mixed progress counts must reflect actual outcomes");
    expect(report.resume.readiness == RecoveryReadiness::NeedsStepRecovery && report.rollback.readiness == RecoveryReadiness::NeedsStepRecovery,
        "partial copy needs explicit per-step recovery before choosing a whole-operation action");
    expect(report.steps[2].transfer && report.steps[2].transfer->finding == TransferRecoveryFinding::PartialCopy, "partial-copy explanation must be preserved");
    expect(f.history.plan.status == PlanStatus::Executing && f.journal.attempts.back().phase == TransferPhase::StagingCreated, "inspection must preserve unresolved records");
}

// 이름 변경 직후 결과 기록 누락을 관찰하되 DB는 수정하지 않는지 검사하는 함수.
void testRenameAndChoices() {
    Fixture f;
    auto& step = f.history.plan.steps[1]; step.status = StepStatus::IntentRecorded;
    f.missing(step.source, "2"); f.present(step.destination, step.fingerprint);
    const auto report = f.inspect();
    expect(report.moved == 1 && report.pending == 2 && report.resume.readiness == RecoveryReadiness::NeedsRevalidation &&
        report.rollback.readiness == RecoveryReadiness::NeedsRevalidation, "intact mixed results should offer revalidation candidates, not automatic execution");
    expect(step.status == StepStatus::IntentRecorded, "unrecorded rename must not be automatically marked applied");
    Fixture untouched;
    expect(untouched.inspect().rollback.readiness == RecoveryReadiness::NotApplicable, "nothing moved means there is nothing to undo");
}

// 충돌·동일 내용 다른 객체·조회 실패·잘못된 볼륨을 복구 가능으로 오인하지 않는지 검사하는 함수.
void testUnsafeFiles() {
    for (int kind = 0; kind < 4; ++kind) {
        Fixture f;
        auto& step = f.history.plan.steps[1];
        auto occupant = step.fingerprint; occupant.fileId = "other-object";
        if (kind == 0) f.present(step.destination, occupant);
        if (kind == 1) f.present(step.source, occupant);
        if (kind == 2) f.files.values[step.source] = {TransferPathPresence::Unavailable, {}, {}, Error{"test.denied", "Access denied"}};
        if (kind == 3) f.missing(step.destination, "9");
        const auto report = f.inspect();
        expect(report.needsAttention == 1 && report.resume.readiness == RecoveryReadiness::Blocked && report.rollback.readiness == RecoveryReadiness::Blocked,
            "uncertain file state must block both action candidates");
        expect(!report.steps[1].reasons.empty(), "blocked file needs a reason");
    }
}

// 잘못된 서명·볼륨·중단 방향을 가진 계획은 파일을 읽기 전에 차단하는지 검사하는 함수.
void testUntrustedPlans() {
    for (int kind = 0; kind < 4; ++kind) {
        Fixture f;
        if (kind == 0) f.history.plan.planHash = std::string(64, 'e');
        if (kind == 1) f.volumes.replaced = true;
        if (kind == 2) { f.history.plan.status = PlanStatus::NeedsRecovery; f.history.origin = PlanStatus::Completed; }
        if (kind == 3) { f.history.plan.steps[0].fingerprint.fileId.reset(); f.seal(); }
        const auto report = f.inspect();
        expect(report.resume.readiness == RecoveryReadiness::Blocked && report.rollback.readiness == RecoveryReadiness::Blocked && f.files.reads == 0,
            "untrusted global evidence must block before reading paths");
    }
    Fixture missing;
    InspectInterruptedPlanUseCase noVolumes(missing.history, missing.journal, missing.files, missing.hasher);
    const auto report = noVolumes.execute(missing.history.plan.id);
    expect(report && report.value().resume.readiness == RecoveryReadiness::Blocked && missing.files.reads == 0,
        "v2 startup without volume verification must not inspect or offer recovery");
}

// 되돌리기 중에는 정방향 재개를 제안하지 않고 복원 증거를 별도로 세는지 검사하는 함수.
void testReverseEvidence() {
    Fixture f; f.movedCopy(0); f.history.plan.status = PlanStatus::NeedsRecovery; f.history.origin = PlanStatus::RollingBack;
    const auto forward = f.journal.attempts[0];
    TransferAttempt reverse;
    reverse.id = "reverse"; reverse.planId = forward.planId; reverse.stepId = forward.stepId; reverse.direction = TransferDirection::Rollback;
    reverse.source = forward.destination; reverse.destination = forward.source; reverse.staging = reverse.destination.parent_path() / "reverse.tmp";
    reverse.sourceFingerprint = *forward.destinationFingerprint; reverse.phase = TransferPhase::Completed;
    auto restored = forward.sourceFingerprint; restored.fileId = "restored-object";
    reverse.destinationFingerprint = restored; reverse.stagingFingerprint = restored;
    f.missing(reverse.source, "2"); f.missing(reverse.staging, "1"); f.present(reverse.destination, restored);
    f.journal.attempts.push_back(reverse); f.history.plan.steps[0].status = StepStatus::RolledBack;
    const auto report = f.inspect();
    expect(report.restored == 1 && report.pending == 2 && report.resume.readiness == RecoveryReadiness::Blocked,
        "restored copy is not pending or forward-completed and undo must not switch directions");
    expect(report.rollback.readiness == RecoveryReadiness::NotApplicable, "fully restored movement has no remaining file to undo");
    f.journal.attempts.back().sourceFingerprint.fileId = "forged-origin";
    expect(f.inspect().rollback.readiness == RecoveryReadiness::Blocked, "reverse must be tied to the recorded forward object");
    auto& partial = f.journal.attempts.back();
    partial.sourceFingerprint = *forward.destinationFingerprint; partial.phase = TransferPhase::StagingCreated;
    partial.destinationFingerprint.reset(); partial.stagingFingerprint = FileFingerprint{0, 20, "1", "reverse-partial", {}};
    auto actual = *partial.stagingFingerprint; actual.size = 3; actual.sha256 = std::string(64, 'd');
    f.present(partial.source, partial.sourceFingerprint); f.present(partial.staging, actual); f.missing(partial.destination, "1");
    f.history.plan.steps[0].status = StepStatus::RollbackIntentRecorded;
    const auto interruptedUndo = f.inspect();
    expect(interruptedUndo.needsAttention == 1 && interruptedUndo.resume.readiness == RecoveryReadiness::Blocked &&
        interruptedUndo.rollback.readiness == RecoveryReadiness::NeedsStepRecovery, "partial reverse must preserve undo direction and require per-step recovery");
}

// 중복 시도와 미기록 제거 결과가 정상 완료로 합쳐지지 않는지 검사하는 함수.
void testTransferBoundaries() {
    Fixture f; f.movedCopy(0);
    f.journal.attempts[0].phase = TransferPhase::SourceRemoved;
    auto report = f.inspect();
    expect(report.moved == 0 && report.needsAttention == 1 && report.resume.readiness == RecoveryReadiness::NeedsStepRecovery,
        "file removal with missing completion record still needs explicit record recovery");
    auto duplicate = f.journal.attempts[0]; duplicate.id = "duplicate"; f.journal.attempts.push_back(duplicate);
    expect(f.inspect().resume.readiness == RecoveryReadiness::Blocked, "ambiguous attempts must block whole-plan candidates");
    Fixture mismatch; mismatch.movedCopy(0); mismatch.history.plan.steps[0].status = StepStatus::Planned;
    expect(mismatch.inspect().resume.readiness == RecoveryReadiness::Blocked, "tracked transfer must not contradict pending step records");
    Fixture undoUnknown; undoUnknown.history.plan.status = PlanStatus::NeedsRecovery; undoUnknown.history.origin = PlanStatus::RollingBack;
    undoUnknown.history.plan.steps[1].status = StepStatus::NeedsRecovery;
    expect(undoUnknown.inspect().rollback.readiness == RecoveryReadiness::Blocked, "unknown pending/restored rename must not be guessed after undo interruption");
    Fixture allMoved; allMoved.movedCopy(0); allMoved.movedCopy(2);
    auto& rename = allMoved.history.plan.steps[1]; rename.status = StepStatus::Applied;
    allMoved.missing(rename.source, "2"); allMoved.present(rename.destination, rename.fingerprint);
    const auto finished = allMoved.inspect();
    expect(finished.moved == 3 && finished.pending == 0 && finished.resume.readiness == RecoveryReadiness::NeedsRevalidation &&
        !finished.resume.reasons.empty() && allMoved.history.plan.status == PlanStatus::Executing,
        "all moved files still require explicit completion-record confirmation, not silent completion");
}

// 시작 조회가 승인 대기·초안·완료 작업을 제외하고 미해결 상태를 보존하는지 검사하는 함수.
void testStartupFilter() {
    Fixture f;
    for (const auto state : {PlanStatus::Draft, PlanStatus::Planned, PlanStatus::Approved, PlanStatus::Completed, PlanStatus::Executing}) {
        auto copy = f.history.plan; copy.status = state; f.history.listed.push_back(copy);
    }
    InspectInterruptedPlanUseCase useCase(f.history, f.journal, f.files, f.hasher, &f.volumes);
    const auto reports = useCase.inspectAll();
    expect(reports && reports.value().size() == 1 && reports.value()[0].pending == 3, "startup must only inspect interrupted work");
    expect(f.history.writes == 0 && f.journal.writes == 0, "startup must not silently finalize or retry work");
    f.history.plan.status = PlanStatus::Approved;
    const auto changed = useCase.inspectAll();
    expect(changed && changed.value().empty(), "work that stopped being interrupted during startup listing must be skipped");
    expect(!useCase.execute(f.history.plan.id), "normal approved work is not an interrupted operation");
}
}

// 읽기 전용 전체 작업 검사 시나리오를 실행하는 함수.
int main() {
    testMixedProgress(); testRenameAndChoices(); testUnsafeFiles(); testUntrustedPlans();
    testReverseEvidence(); testTransferBoundaries(); testStartupFilter();
    return failures ? 1 : 0;
}
