#include <nookbit/application/inspect_interrupted_plan_use_case.hpp>
#include <nookbit/application/recover_interrupted_plan_use_case.hpp>
#include <nookbit/application/cleanup_preserved_staging_use_case.hpp>
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
    bool failRecovery{false};
    std::function<void()> onRecovery;
    std::vector<PreservedTransfer> preserved;
    std::optional<PreservedCleanupRecord> cleanup;
    bool failCleanupIntent{false}, failCleanupRemoved{false};
    // 승인된 정리 지문과 별도 완료 여부를 반환하는 함수.
    Result<std::optional<PreservedCleanupRecord>> preservedCleanup(const PlanId&, const std::string&) const override {
        return Result<std::optional<PreservedCleanupRecord>>::success(cleanup);
    }
    // 정리 감사 실패에는 원래 의도를 유지하고 성공만 기록하는 함수.
    Result<void> recordPreservedCleanup(const PreservedCleanupRecord& record) override {
        if (record.removed ? failCleanupRemoved : failCleanupIntent)
            return Result<void>::failure({"test.cleanup_audit", "Injected cleanup audit failure"});
        cleanup = record; ++writes; return Result<void>::success();
    }
    // 보존한 임시 파일의 추적 정보를 반환하는 함수.
    Result<std::vector<PreservedTransfer>> preservedTransfers(const PlanId&) const override { return Result<std::vector<PreservedTransfer>>::success(preserved); }
    // 보존 추적이 남은 작업만 시작 조회에 포함하는 함수.
    Result<std::vector<PlanId>> preservedTransferPlans() const override {
        return Result<std::vector<PlanId>>::success(preserved.empty() || (cleanup && cleanup->removed) ? std::vector<PlanId>{} : std::vector<PlanId>{plan.id});
    }
    // 시험용 원자적 재시작을 반영하고 요청 실패에는 원래 상태를 보존하는 함수.
    Result<void> beginRecovery(const RecoveryRestart& restart) override {
        if (failRecovery) return Result<void>::failure({"test.atomic_failure", "Injected restart failure"});
        ++writes;
        plan.status = restart.request.action == RecoveryAction::Resume ? PlanStatus::Executing : PlanStatus::RollingBack;
        for (size_t i = 0; i < plan.steps.size(); ++i) plan.steps[i].status = restart.steps[i].restored;
        origin = plan.status;
        preserved = restart.preserved;
        if (onRecovery) onRecovery();
        return Result<void>::success();
    }
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

struct Settings final : ISettingsRepository {
    UserSettings value;
    // 선택 시점의 설정을 반환하는 함수.
    Result<UserSettings> load() const override { return Result<UserSettings>::success(value); }
    // 테스트에서 사용하지 않는 설정 저장을 거부하는 함수.
    Result<void> save(const UserSettings&) override { return Result<void>::failure({"test.unused", "Unused"}); }
};
struct Rules final : IRuleSetRepository {
    RuleSet value;
    // 선택 시점의 규칙을 반환하는 함수.
    Result<RuleSet> load() const override { return Result<RuleSet>::success(value); }
};
struct Runner final : IRecoveredPlanRunner {
    int executions{0};
    bool supported{true};
    // 외부 실행 서비스 미연결을 조회 단계에서 재현하는 함수.
    Result<void> verifyRecoverySupport(const OperationPlan&, RecoveryAction) override {
        return supported ? Result<void>::success() : Result<void>::failure({"test.unsupported", "Recovery services missing"});
    }
    // 실제 파일 이동 없이 전체 작업 실행 요청 횟수만 세는 함수.
    Result<void> runRecoveredPlan(const OperationPlan&, RecoveryAction) override { ++executions; return Result<void>::success(); }
};

struct Resolver final : IInterruptedTransferResolver {
    Fixture& fixture;
    bool supported{true}, fail{false}, finishPlan{false};
    int executions{0};
    std::function<void()> afterResume;
    // 시험 전용 기록과 관찰값을 외부에서 빌려 받는 함수.
    explicit Resolver(Fixture& value) : fixture(value) {}
    // 지원하지 않는 개별 복구 경계를 읽기 전용으로 재현하는 함수.
    Result<void> verifyResume(const TransferRecoveryInspection& report) override {
        return supported && report.attempt.direction == TransferDirection::Forward && report.finding == TransferRecoveryFinding::PartialCopy ?
            Result<void>::success() : Result<void>::failure({"test.unsupported_transfer", "Unsupported finding"});
    }
    // 실제 파일 작업 없이 완료된 단일 시도와 그 관찰값을 재현하는 함수.
    Result<void> resume(const std::string& id) override {
        ++executions;
        if (fail) return Result<void>::failure({"test.resolve_failed", "Injected resolver failure"});
        auto& attempt = fixture.journal.attempts[0];
        if (attempt.id != id) return Result<void>::failure({"test.wrong_attempt", "Wrong attempt"});
        auto final = *attempt.stagingFingerprint;
        final.size = attempt.sourceFingerprint.size; final.sha256 = attempt.sourceFingerprint.sha256;
        attempt.stagingFingerprint = final; attempt.destinationFingerprint = final; attempt.phase = TransferPhase::Completed;
        fixture.missing(attempt.source, "1"); fixture.missing(attempt.staging, "2"); fixture.present(attempt.destination, final);
        fixture.history.plan.steps[0].status = StepStatus::Applied; ++fixture.journal.writes;
        if (finishPlan) fixture.history.plan.status = PlanStatus::Completed;
        if (afterResume) afterResume();
        return Result<void>::success();
    }
};

// 개별 복구가 선택 감사 후에만 실행되고 남은 전체 작업 또는 마지막 완료로 연결되는지 검사하는 함수.
void testTransferToWholeResume() {
    for (int scenario = 0; scenario < 9; ++scenario) {
        Fixture f;
        if (scenario == 7) f.history.plan.steps.resize(1);
        auto attempt = f.forward(0, TransferPhase::StagingCreated);
        attempt.stagingFingerprint = FileFingerprint{0, 20, "2", "partial", {}};
        auto actual = *attempt.stagingFingerprint; actual.size = 3; actual.sha256 = std::string(64, 'd');
        f.present(attempt.staging, actual); f.journal.attempts.push_back(attempt); f.history.plan.steps[0].status = StepStatus::IntentRecorded;
        Settings settings; Rules rules; rules.value.version = "rules"; Runner runner; Resolver resolver(f);
        f.history.plan.settingsRevision = f.history.plan.rulesRevision = std::string(64, 'b'); f.seal();
        InspectInterruptedPlanUseCase inspector(f.history, f.journal, f.files, f.hasher, &f.volumes);
        RecoverInterruptedPlanUseCase recover(inspector, f.history, settings, rules, f.hasher, runner, &resolver);
        RecoveryRequest request{f.history.plan.id, f.history.plan.planHash, scenario == 1 ? RecoveryAction::Rollback : RecoveryAction::Resume};
        if (scenario == 2) f.history.failRecovery = true;
        if (scenario == 3) resolver.fail = true;
        if (scenario == 4) {
            // 선택 저장 중 대기 파일을 교체해 개별 복구 시작 전에 차단하는 함수.
            f.history.onRecovery = [&] { auto changed = f.history.plan.steps[1].fingerprint; changed.fileId = "other";
                f.present(f.history.plan.steps[1].source, changed); };
        }
        if (scenario == 5) resolver.afterResume = [&] { rules.value.version = "changed"; };
        if (scenario == 6) resolver.supported = false;
        if (scenario == 7) resolver.finishPlan = true;
        if (scenario == 8) {
            // 선택 저장 중 승인 내용을 바꾸면 새 경로를 읽거나 복구하기 전에 차단하는 함수.
            f.history.onRecovery = [&] { f.history.plan.steps[1].destination = path("different-target/file"); f.seal(); };
        }
        const auto result = recover.execute(request);
        if (scenario == 0) expect(result && resolver.executions == 1 && runner.executions == 1 && f.history.writes == 1,
            "one explicit choice must resolve the tracked partial then request whole-plan execution");
        else if (scenario == 7) expect(result && resolver.executions == 1 && runner.executions == 0 && f.history.plan.status == PlanStatus::Completed,
            "last transfer completing the whole plan must be verified without restarting a completed runner");
        else if (scenario == 1) expect(result && runner.executions == 1 && resolver.executions == 0 &&
            f.history.preserved.size() == 1 && f.journal.writes == 0, "partial undo must preserve rather than finish the forward copy");
        else expect(!result && runner.executions == 0 && resolver.executions == ((scenario == 3 || scenario == 5) ? 1 : 0) &&
            f.history.writes == ((scenario == 3 || scenario == 4 || scenario == 5 || scenario == 8) ? 1 : 0),
            "undo, failed audit, changed file/configuration, unsupported route or resolver failure must stop safely");
    }
}

// 복사 시작 전·부분 역전송은 새 실행이나 감사 없이 명시적 선택을 차단하는 함수.
void testUnsupportedRecoveryEdges() {
    for (bool reverse : {false, true}) {
        Fixture f;
        if (reverse) {
            f.movedCopy(0);
            const auto forward = f.journal.attempts[0];
            TransferAttempt undo;
            undo.id = "partial-undo"; undo.planId = forward.planId; undo.stepId = forward.stepId; undo.direction = TransferDirection::Rollback;
            undo.source = forward.destination; undo.destination = forward.source; undo.staging = forward.source.parent_path() / "reverse.tmp";
            undo.sourceFingerprint = *forward.destinationFingerprint; undo.phase = TransferPhase::StagingCreated;
            undo.stagingFingerprint = FileFingerprint{0, 20, "1", "partial-reverse", {}};
            auto actual = *undo.stagingFingerprint; actual.size = 3; actual.sha256 = std::string(64, 'd');
            f.present(undo.staging, actual); f.journal.attempts.push_back(undo);
            f.history.plan.status = PlanStatus::RollingBack; f.history.origin = PlanStatus::RollingBack;
            f.history.plan.steps[0].status = StepStatus::RollbackIntentRecorded;
        } else {
            f.journal.attempts.push_back(f.forward(0, TransferPhase::Prepared));
            f.history.plan.steps[0].status = StepStatus::IntentRecorded;
        }
        Settings settings; Rules rules; rules.value.version = "rules"; Runner runner; Resolver resolver(f);
        f.history.plan.settingsRevision = f.history.plan.rulesRevision = std::string(64, 'b'); f.seal();
        InspectInterruptedPlanUseCase inspector(f.history, f.journal, f.files, f.hasher, &f.volumes);
        RecoverInterruptedPlanUseCase recovery(inspector, f.history, settings, rules, f.hasher, runner, &resolver);
        expect(recovery.execute({f.history.plan.id, f.history.plan.planHash, RecoveryAction::Later}).hasValue(), "unsupported edge can be deferred");
        for (const auto action : {RecoveryAction::Resume, RecoveryAction::Rollback})
            expect(!recovery.execute({f.history.plan.id, f.history.plan.planHash, action}), "unsupported transfer boundary must block both guessed choices");
        expect(f.history.writes == 0 && f.journal.writes == 0 && runner.executions == 0 && resolver.executions == 0,
            "unsupported recovery edges never mutate audit or invoke file workers");
    }
}

// 보존된 부분 파일이 되돌리기 완료 후에도 조회되고 변경되면 경고하는지 검사하는 함수.
void testPreservedStartup() {
    Fixture f;
    auto attempt = f.forward(0, TransferPhase::Cancelled);
    attempt.stagingFingerprint = FileFingerprint{0, 20, "2", "partial", {}};
    auto actual = *attempt.stagingFingerprint; actual.size = 3; actual.sha256 = std::string(64, 'd');
    f.present(attempt.staging, actual); f.journal.attempts.push_back(attempt);
    f.history.plan.status = PlanStatus::RolledBack;
    f.history.preserved = {{attempt.id, attempt.stepId, attempt.staging}};
    InspectInterruptedPlanUseCase inspector(f.history, f.journal, f.files, f.hasher, &f.volumes);
    const auto reports = inspector.inspectAll();
    expect(reports && reports.value().size() == 1 && reports.value()[0].preserved == 1 && reports.value()[0].pending == 2 &&
        reports.value()[0].resume.readiness == RecoveryReadiness::Blocked, "finished undo must retain a staging-review reminder without permitting forward execution");
    expect(f.history.writes == 0 && f.journal.writes == 0, "staging reminder must be read-only");
    auto changed = actual; changed.fileId = "replacement"; f.present(attempt.staging, changed);
    const auto replaced = inspector.inspectAll();
    expect(replaced && replaced.value()[0].needsAttention == 1 && replaced.value()[0].preservedTransfers.size() == 1,
        "same-name replacement must remain registered but not be treated as the owned partial file");
    f.journal.attempts.clear();
    const auto missing = inspector.inspectAll();
    expect(missing && missing.value()[0].needsAttention == 1, "lost journal evidence must not silently erase preservation tracking");
}

// 선택·서명·설정·차단·원자적 기록 실패·기록 중 파일 변경을 검사하는 함수.
void testExplicitChoices() {
    for (int scenario = 0; scenario < 9; ++scenario) {
        Fixture f; f.movedCopy(0);
        Settings settings; Rules rules; rules.value.version = "rules";
        f.history.plan.settingsRevision = f.history.plan.rulesRevision = std::string(64, 'b'); f.seal();
        Runner runner;
        InspectInterruptedPlanUseCase inspector(f.history, f.journal, f.files, f.hasher, &f.volumes);
        RecoverInterruptedPlanUseCase recover(inspector, f.history, settings, rules, f.hasher, runner);
        RecoveryRequest request{f.history.plan.id, f.history.plan.planHash, scenario == 1 ? RecoveryAction::Rollback : RecoveryAction::Resume};
        if (scenario == 2) request.action = RecoveryAction::Later;
        if (scenario == 3) request.planHash = std::string(64, 'f');
        if (scenario == 4) rules.value.version = "changed";
        if (scenario == 5) f.history.failRecovery = true;
        if (scenario == 6) runner.supported = false;
        if (scenario == 7) {
            // 선택 저장 경계에 같은 내용 다른 객체를 삽입하는 함수.
            f.history.onRecovery = [&] { auto changed = f.history.plan.steps[1].fingerprint; changed.fileId = "new-occupant";
                f.present(f.history.plan.steps[1].source, changed); };
        }
        if (scenario == 8) { f.history.plan.status = PlanStatus::NeedsRecovery; f.history.origin = PlanStatus::RollingBack; }
        const auto result = recover.execute(request);
        if (scenario < 2) {
            expect(result && runner.executions == 1 && f.history.writes == 1, "explicit intact choice should record one restart and request execution");
            expect(f.history.plan.status == (scenario == 1 ? PlanStatus::RollingBack : PlanStatus::Executing) &&
                f.history.plan.steps[0].status == StepStatus::Applied && f.history.plan.steps[1].status == StepStatus::Planned,
                "restart must retain applied files and pending order");
        } else if (scenario == 2) expect(result && runner.executions == 0 && f.history.writes == 0 && f.files.reads == 0,
            "later must neither inspect files nor change records");
        else expect(!result && runner.executions == 0 && f.history.writes == (scenario == 7 ? 1 : 0),
            "stale choice, configuration, unsupported service, failed audit or changed files must stop before execution");
    }
    Fixture partial;
    auto attempt = partial.forward(0, TransferPhase::StagingCreated);
    attempt.stagingFingerprint = FileFingerprint{0, 20, "2", "partial", {}};
    auto actual = *attempt.stagingFingerprint; actual.size = 3; actual.sha256 = std::string(64, 'd');
    partial.present(attempt.staging, actual); partial.journal.attempts.push_back(attempt);
    partial.history.plan.steps[0].status = StepStatus::IntentRecorded;
    Settings settings; Rules rules; rules.value.version = "rules"; Runner runner;
    partial.history.plan.settingsRevision = partial.history.plan.rulesRevision = std::string(64, 'b'); partial.seal();
    InspectInterruptedPlanUseCase inspector(partial.history, partial.journal, partial.files, partial.hasher, &partial.volumes);
    RecoverInterruptedPlanUseCase recover(inspector, partial.history, settings, rules, partial.hasher, runner);
    const auto result = recover.execute({partial.history.plan.id, partial.history.plan.planHash, RecoveryAction::Resume});
    expect(!result && result.error().code == "recovery.step_resolution_required" && partial.history.writes == 0 && runner.executions == 0,
        "partial transfer must be preserved for explicit per-step resolution, not silently completed");
}

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

// 메모리 관찰값만 제거하며 감사 의도가 먼저 기록되었는지 확인하는 정리 대역.
class PartialCleanup final : public IPreservedStagingCleanup {
    History& history_;
    Files& files_;
    class Session final : public IStagingCleanupSession {
        History& history_;
        Files& files_;
        std::filesystem::path path_;
        FileFingerprint expected_;
        bool prior_;
    public:
        // 가상 파일 관찰과 승인 당시 지문을 빌려 받는 함수.
        Session(History& history, Files& files, std::filesystem::path path, FileFingerprint expected, bool prior)
            : history_(history), files_(files), path_(std::move(path)), expected_(std::move(expected)), prior_(prior) {}
        // 같은 가상 객체 또는 이전 의도 뒤의 부재만 허용하는 함수.
        Result<void> verify() override {
            const auto observed = files_.inspect(path_);
            if (observed.presence == TransferPathPresence::Missing && prior_) return Result<void>::success();
            if (!observed.fingerprint || !TransferValidation::sameObject(expected_, *observed.fingerprint) ||
                !TransferValidation::sameContent(expected_, *observed.fingerprint)) return Result<void>::failure({"test.changed", "Changed staging"});
            return Result<void>::success();
        }
        // 의도 감사가 선행했을 때만 가상 임시 객체의 부재를 반영하는 함수.
        Result<void> removeVerifiedStaging() override {
            expect(history_.cleanup && !history_.cleanup->removed, "cleanup intent must precede removal");
            const auto checked = verify(); if (!checked) return checked;
            files_.values[path_] = {TransferPathPresence::Missing, expected_.volumeId, {}, {}};
            return Result<void>::success();
        }
    };
public:
    int opens{0};
    // 파일 작업 없이 정리 대상 관찰과 감사 저장소를 받는 함수.
    PartialCleanup(History& history, Files& files) : history_(history), files_(files) {}
    // 삭제 시뮬레이션용 세션만 반환하는 함수.
    Result<std::unique_ptr<IStagingCleanupSession>> openPreserved(const TransferAttempt& attempt, const MoveContract&,
        const FileFingerprint& expected, bool prior) override {
        ++opens;
        return Result<std::unique_ptr<IStagingCleanupSession>>::success(std::make_unique<Session>(history_, files_, attempt.staging, expected, prior));
    }
};

// 승인 누락·오래된 지문·감사 실패와 삭제 뒤 기록 재시도를 자동 검사하는 함수.
void testPartialCleanupApproval() {
    for (int scenario = 0; scenario < 8; ++scenario) {
        Fixture f;
        f.history.plan.status = PlanStatus::RolledBack; f.history.origin = PlanStatus::RollingBack;
        auto attempt = f.forward(0, TransferPhase::Cancelled);
        attempt.stagingFingerprint = FileFingerprint{0, 21, "2", "stage", {}};
        f.journal.attempts.push_back(attempt);
        f.history.preserved.push_back({attempt.id, attempt.stepId, attempt.staging});
        const FileFingerprint partial{4, 22, "2", "stage", std::string(64, 'e')};
        f.present(attempt.staging, partial);
        PartialCleanup worker(f.history, f.files);
        CleanupPreservedStagingUseCase cleanup(f.history, f.journal, f.files, worker, f.hasher);
        const auto preview = cleanup.inspect(f.history.plan.id, attempt.id);
        expect(preview && f.history.writes == 0 && worker.opens == 0, "cleanup inspection is read-only");
        if (!preview) continue;
        PreservedCleanupRequest request{preview.value(), scenario != 1};
        if (scenario == 2) request.expected.planHash = std::string(64, 'b');
        if (scenario == 3) f.files.values[attempt.staging].fingerprint->sha256 = std::string(64, 'f');
        if (scenario == 4) f.history.failCleanupIntent = true;
        if (scenario == 5 || scenario == 6) f.history.failCleanupRemoved = true;
        if (scenario == 7) f.history.preserved.clear();
        const auto result = cleanup.execute(request);
        if (scenario == 0) {
            expect(result && f.history.cleanup && f.history.cleanup->removed && f.history.writes == 2 &&
                !cleanup.execute(request), "cleanup completes once with separate intent/result audits");
        } else if (scenario == 5 || scenario == 6) {
            expect(!result && f.history.cleanup && !f.history.cleanup->removed &&
                f.files.values[attempt.staging].presence == TransferPathPresence::Missing, "delete result failure retains durable intent");
            f.history.failCleanupRemoved = false;
            if (scenario == 6) {
                auto replacement = partial; replacement.fileId = "replacement"; f.present(attempt.staging, replacement);
                expect(!cleanup.execute(request) && !f.history.cleanup->removed, "cleanup retry preserves a same-name replacement");
            } else expect(cleanup.execute(request).hasValue() && f.history.cleanup->removed, "record-only cleanup retry needs no second deletion");
        } else expect(!result && f.history.writes == 0 && f.files.values[attempt.staging].presence == TransferPathPresence::Present,
            "unapproved, stale, unaudited or unregistered cleanup cannot delete");
        expect(f.history.plan.status == PlanStatus::RolledBack && f.journal.writes == 0 &&
            f.files.values[attempt.source].fingerprint->fileId == attempt.sourceFingerprint.fileId, "cleanup does not change movement or original");
    }
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
    testExplicitChoices();
    testTransferToWholeResume();
    testPreservedStartup();
    testPartialCleanupApproval();
    testUnsupportedRecoveryEdges();
    return failures ? 1 : 0;
}
