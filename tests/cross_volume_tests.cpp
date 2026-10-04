#include <nookbit/application/commit_transfer_copy_use_case.hpp>
#include <nookbit/application/finalize_transfer_use_case.hpp>
#include <nookbit/application/prepare_transfer_plan_use_case.hpp>
#include <nookbit/application/rollback_transfer_use_case.hpp>
#include "application/use_cases/plan_signature.hpp"
#include "infrastructure/cryptography/qt_plan_hasher.hpp"
#include "infrastructure/filesystem/filesystem_scanner.hpp"
#include "infrastructure/filesystem/file_identity.hpp"
#include "infrastructure/filesystem/operation_mutex.hpp"
#include "infrastructure/filesystem/windows_staged_copy.hpp"
#include "infrastructure/filesystem/windows_transfer_file_inspector.hpp"
#include "infrastructure/filesystem/windows_transfer_recovery.hpp"
#include "infrastructure/filesystem/windows_transfer_source_removal.hpp"
#include "infrastructure/persistence_sqlite/sqlite_history_repository.hpp"
#include "infrastructure/persistence_sqlite/sqlite_transfer_journal.hpp"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTemporaryDir>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace nookbit;

// 실패한 계약의 이유를 예외로 전달해 해당 시나리오를 즉시 멈추는 함수.
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
// 실패한 포트의 실제 오류를 잃지 않고 결과를 꺼내는 함수.
template<class T> T take(Result<T> result) {
    if (!result) throw std::runtime_error(result.error().code + ": " + result.error().message);
    return std::move(result.value());
}
// 기록 포트의 성공 여부를 확인하는 함수.
void saved(Result<void> result) {
    if (!result) throw std::runtime_error(result.error().code + ": " + result.error().message);
}
// 테스트가 소유한 새 경로만 생성하고 기존 파일 덮어쓰기를 금지하는 함수.
void createFile(const std::filesystem::path& path, const std::string& contents) {
    require(!std::filesystem::exists(path), "Test file already exists");
    std::ofstream file(path, std::ios::binary);
    file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    file.close();
    require(!file.fail(), "Cannot create owned test file");
}
// 실제 Windows 객체 ID와 전체 SHA-256을 읽는 함수.
FileFingerprint fingerprint(const std::filesystem::path& path) {
    FilesystemScanner scanner;
    QtPlanHasher hasher;
    return take(scanFingerprint(scanner, path, &hasher));
}

// 파일 작업을 바꾸지 않고 명시한 결과 기록 한 번만 실패시키는 테스트 대역.
class FaultJournal final : public ITransferJournal {
    ITransferJournal& inner_;
public:
    std::optional<TransferPhase> failAt;
    // 실제 SQLite 저널을 빌려 받는 함수.
    explicit FaultJournal(ITransferJournal& inner) : inner_(inner) {}
    // 새 시도 기록을 전달하는 함수.
    Result<void> begin(const TransferAttempt& attempt) override { return inner_.begin(attempt); }
    // 실제 파일 작업 뒤 지정한 기록 경계에서 한 번만 실패하는 함수.
    Result<void> advance(const std::string& id, const TransferUpdate& update) override {
        if (failAt == update.nextPhase) {
            failAt.reset();
            return Result<void>::failure({"test.record_interrupted", "Injected result record interruption"});
        }
        return inner_.advance(id, update);
    }
    // 시도 증거를 복원하는 함수.
    Result<TransferAttempt> load(const std::string& id) const override { return inner_.load(id); }
    // 같은 단계의 전송 시도를 조회하는 함수.
    Result<std::vector<TransferAttempt>> forStep(const StepId& id) const override { return inner_.forStep(id); }
    // 원자적 정방향 완료를 전달하는 함수.
    Result<void> completeForward(const std::string& id) override { return inner_.completeForward(id); }
    // 원자적 역방향 완료를 전달하는 함수.
    Result<void> completeRollback(const std::string& id) override { return inner_.completeRollback(id); }
};

// 한 시나리오의 새 파일·새 승인 계획·새 DB만 소유하는 테스트 환경.
struct Fixture {
    std::filesystem::path sourceRoot, targetRoot;
    QString database;
    QString migration{QString::fromUtf8(NOOKBIT_SOURCE_DIR) + "/src/infrastructure/persistence_sqlite/migrations/001_initial.sql"};
    SqliteHistoryRepository history;
    SqliteTransferJournal journal;
    FaultJournal faults;
    WindowsStagedCopy copy;
    WindowsTransferSourceRemoval removal;
    WindowsTransferFileInspector files;
    QtPlanHasher hasher;
    OperationPlan plan;
    TransferAttempt attempt;
    // 전용 폴더 안의 이진 파일과 실제 서로 다른 볼륨의 v2 승인 계약을 만드는 함수.
    Fixture(const QString& sourceRun, const QString& targetRun, const QString& name)
        : sourceRoot((QDir(sourceRun).filePath(name)).toStdWString()),
          targetRoot((QDir(targetRun).filePath(name)).toStdWString()),
          database(QDir(targetRun).filePath(name + "/history.sqlite")),
          history(database, migration), journal(database, migration), faults(journal) {
        require(std::filesystem::create_directory(sourceRoot) && std::filesystem::create_directory(targetRoot), "Cannot create unique scenario folders");
        std::string contents(4 * 1024 * 1024 + 37, '\0');
        for (size_t i = 0; i < contents.size(); ++i) contents[i] = static_cast<char>((i * 31 + i / 257) % 256);
        attempt = {};
        attempt.id = "forward"; attempt.planId = "plan"; attempt.stepId = "step";
        attempt.source = sourceRoot / L"테스트 파일.bin";
        attempt.destination = targetRoot / "new-parent" / "nested" / L"정리 파일.bin";
        attempt.staging = attempt.destination.parent_path() / "stage.tmp";
        createFile(attempt.source, contents);
        attempt.sourceFingerprint = fingerprint(attempt.source);
        OperationPlan draft;
        draft.id = attempt.planId; draft.ruleSetVersion = "cross-volume-test";
        draft.settingsRevision = std::string(64, 'b'); draft.rulesRevision = std::string(64, 'c');
        MoveStep step;
        step.id = attempt.stepId; step.source = attempt.source; step.destination = attempt.destination;
        step.fingerprint = attempt.sourceFingerprint; step.matchedRuleId = "test-rule";
        draft.steps.push_back(step);
        PrepareTransferPlanUseCase prepare(copy);
        plan = take(prepare.execute(draft));
        require(plan.formatVersion == 2 && plan.steps[0].movement->strategy == MoveStrategy::VerifiedCopy &&
            plan.steps[0].movement->sourceVolume.guidPath != plan.steps[0].movement->destinationVolume.guidPath,
            "Real distinct-volume VerifiedCopy contract required; no forced strategy is allowed");
        plan.status = PlanStatus::Planned;
        plan.planHash = hasher.sha256(PlanSignature::approvalPayload(plan));
        saved(history.savePlan(plan));
        saved(history.saveApproval({plan.id, plan.planHash, std::chrono::system_clock::now()}));
        saved(history.appendEvent(PlanStatusChanged{plan.id, PlanStatus::Executing}));
        saved(history.appendEvent(StepStatusChanged{plan.id, step.id, StepStatus::IntentRecorded}));
        saved(prepare.verify(take(history.loadPlan(plan.id))));
    }
    // 승인된 실제 볼륨 계약으로 새 복사 시도를 수행하는 함수.
    Result<TransferAttempt> commit(const std::function<bool()>& cancelled = {}) {
        CommitTransferCopyUseCase useCase(copy, faults);
        return useCase.execute(attempt, cancelled, &*plan.steps[0].movement);
    }
    // 별도 원본 제거와 계획 완료를 요청하는 함수.
    Result<TransferAttempt> finish() {
        FinalizeTransferUseCase useCase(faults, removal, history, hasher);
        return useCase.execute(attempt.id);
    }
    // 전체 이동 결과와 서로 다른 원본/복사 객체를 확인하는 함수.
    void completed() {
        const auto state = take(journal.load(attempt.id));
        const auto savedPlan = take(history.loadPlan(plan.id));
        require(state.phase == TransferPhase::Completed && savedPlan.status == PlanStatus::Completed &&
            savedPlan.steps[0].status == StepStatus::Applied, "Atomic movement completion missing");
        require(!std::filesystem::exists(attempt.source) && !std::filesystem::exists(attempt.staging), "Original or stage remains after completion");
        const auto actual = fingerprint(attempt.destination);
        require(TransferValidation::sameContent(attempt.sourceFingerprint, actual) &&
            !TransferValidation::sameObject(attempt.sourceFingerprint, actual) &&
            state.destinationFingerprint && sameFingerprint(*state.destinationFingerprint, actual), "Final identity or binary contents differ");
    }
    // 정상 되돌리기로 원래 볼륨·경로·내용·기록을 확인하는 함수.
    void rollback() {
        RollbackTransferUseCase undo(journal, history, copy, removal, files, hasher);
        const auto result = take(undo.execute(attempt.id, "reverse", sourceRoot / "reverse-stage.tmp"));
        const auto savedPlan = take(history.loadPlan(plan.id));
        require(result.direction == TransferDirection::Rollback && result.phase == TransferPhase::Completed &&
            savedPlan.status == PlanStatus::RolledBack && savedPlan.steps[0].status == StepStatus::RolledBack, "Rollback records differ");
        require(!std::filesystem::exists(attempt.destination) && TransferValidation::sameContent(attempt.sourceFingerprint, fingerprint(attempt.source)),
            "Rollback location or full contents differ");
    }
};

// 실제 두 볼륨에서 성공·충돌·중단 재개·기록 복구의 한 시나리오를 검사하는 함수.
void scenario(const QString& sourceRun, const QString& targetRun, int index) {
    Fixture fixture(sourceRun, targetRun, QString("case-%1").arg(index));
    if (index == 1) {
        std::filesystem::create_directories(fixture.attempt.destination.parent_path());
        createFile(fixture.attempt.destination, "owned collision sentinel");
        const auto sentinel = fingerprint(fixture.attempt.destination);
        require(!fixture.commit(), "Existing final should reject transfer");
        require(sameFingerprint(sentinel, fingerprint(fixture.attempt.destination)) &&
            sameFingerprint(fixture.attempt.sourceFingerprint, fingerprint(fixture.attempt.source)), "Collision overwrote a file");
        require(take(fixture.journal.load(fixture.attempt.id)).phase != TransferPhase::Completed, "Collision falsely completed");
        return;
    }
    if (index == 2) {
        int checks = 0;
        // 한 블록을 실제 복사한 뒤 취소해 부분 파일을 남기는 함수.
        require(!fixture.commit([&] { return ++checks >= 3; }), "Partial copy cancellation should stop");
        require(std::filesystem::file_size(fixture.attempt.staging) == 1024 * 1024 &&
            sameFingerprint(fixture.attempt.sourceFingerprint, fingerprint(fixture.attempt.source)), "Real partial copy or retained source differs");
        ResumePartialCopyUseCase resume(fixture.history, fixture.journal, fixture.copy, fixture.hasher);
        require(take(resume.execute(fixture.attempt.id)).copyResume == CopyResume::Verified, "Resume whole verification missing");
        CommitResumedStagingUseCase commit(fixture.history, fixture.journal, fixture.copy, fixture.hasher);
        take(commit.execute(fixture.attempt.id));
        WindowsTransferRecoveryGuard guard;
        ResumeCommittedTransferUseCase finish(fixture.history, fixture.journal, fixture.removal, guard, fixture.hasher);
        take(finish.execute(fixture.attempt.id));
    } else if (index == 3) {
        fixture.faults.failAt = TransferPhase::DestinationCommitted;
        require(!fixture.commit(), "Commit result interruption should fail");
        require(std::filesystem::exists(fixture.attempt.source) && std::filesystem::exists(fixture.attempt.destination) &&
            !std::filesystem::exists(fixture.attempt.staging), "Unrecorded final did not retain both files");
        // 연결을 새로 열어 메모리가 아닌 SQLite 재생 증거로 복구한다.
        SqliteTransferJournal restarted(fixture.database, fixture.migration);
        WindowsTransferCommitRecoveryGuard guard;
        ReconcileTransferCommitUseCase recover(fixture.history, restarted, guard, fixture.hasher);
        take(recover.execute(fixture.attempt.id));
        WindowsTransferRecoveryGuard finalGuard;
        ResumeCommittedTransferUseCase finish(fixture.history, restarted, fixture.removal, finalGuard, fixture.hasher);
        take(finish.execute(fixture.attempt.id));
    } else {
        take(fixture.commit());
        require(std::filesystem::exists(fixture.attempt.source), "Commit alone removed original");
        if (index == 4) {
            fixture.faults.failAt = TransferPhase::SourceRemoved;
            require(!fixture.finish(), "Removal result interruption should fail");
            require(!std::filesystem::exists(fixture.attempt.source) && std::filesystem::exists(fixture.attempt.destination), "Removal interruption lost final file");
            SqliteTransferJournal restarted(fixture.database, fixture.migration);
            WindowsTransferRecoveryGuard guard;
            ReconcileTransferRecoveryUseCase recover(fixture.history, restarted, guard, fixture.hasher);
            take(recover.execute(fixture.attempt.id));
        } else take(fixture.finish());
    }
    fixture.completed();
    if (index == 5) {
        createFile(fixture.attempt.source, "owned reverse collision sentinel");
        const auto sentinel = fingerprint(fixture.attempt.source);
        const auto moved = fingerprint(fixture.attempt.destination);
        RollbackTransferUseCase undo(fixture.journal, fixture.history, fixture.copy, fixture.removal, fixture.files, fixture.hasher);
        require(!undo.execute(fixture.attempt.id, "reverse", fixture.sourceRoot / "reverse-stage.tmp"), "Reverse collision must reject");
        require(sameFingerprint(sentinel, fingerprint(fixture.attempt.source)) && sameFingerprint(moved, fingerprint(fixture.attempt.destination)),
            "Reverse collision modified an existing file");
    } else fixture.rollback();
}
}

// 명시한 두 기존 부모가 서로 다른 볼륨일 때만 새 전용 폴더에서 시험하는 함수.
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() != 6 || args[1] != "--run" || args[2] != "--source-parent" || args[4] != "--destination-parent") {
        std::cout << "Opt-in only: --run --source-parent <existing-directory> --destination-parent <existing-directory>\n";
        return args.size() == 1 ? 0 : 2;
    }
    try {
        for (const auto& parent : {args[3], args[5]})
            require(QFileInfo(parent).isAbsolute() && QFileInfo(parent).isDir(), "Absolute existing parent directories required");
        WindowsStagedCopy inspector;
        const auto sourceVolume = take(inspector.inspectVolume(std::filesystem::path(args[3].toStdWString())));
        const auto targetVolume = take(inspector.inspectVolume(std::filesystem::path(args[5].toStdWString())));
        require(sourceVolume.localNtfs && targetVolume.localNtfs && sourceVolume.name != targetVolume.name,
            "Two genuinely distinct fixed NTFS volumes required before creating any test folders");
        OperationMutex lock;
        saved(lock.acquire());
        QTemporaryDir source(QDir(args[3]).filePath("NookBit-cross-source-XXXXXX"));
        QTemporaryDir target(QDir(args[5]).filePath("NookBit-cross-target-XXXXXX"));
        source.setAutoRemove(false); target.setAutoRemove(false);
        require(source.isValid() && target.isValid(), "Cannot create unique owned test roots");
        std::cout << "Source fixtures: " << source.path().toStdString() << "\nTarget fixtures/report: " << target.path().toStdString() << '\n';
        QJsonArray results;
        int failures = 0;
        const QStringList names{"roundtrip", "forward-collision", "partial-resume-roundtrip", "commit-record-recovery-roundtrip", "removal-record-recovery-roundtrip", "reverse-collision"};
        for (int index = 0; index < names.size(); ++index) {
            try {
                scenario(source.path(), target.path(), index);
                results.append(QJsonObject{{"case", names[index]}, {"passed", true}});
                std::cout << "PASS " << names[index].toStdString() << '\n';
            } catch (const std::exception& error) {
                ++failures;
                results.append(QJsonObject{{"case", names[index]}, {"passed", false}, {"error", QString::fromUtf8(error.what())}});
                std::cerr << "FAIL " << names[index].toStdString() << ": " << error.what() << '\n';
            }
        }
        QSaveFile report(QDir(target.path()).filePath("report.json"));
        require(report.open(QIODevice::WriteOnly), "Cannot save test report");
        const auto bytes = QJsonDocument(QJsonObject{{"utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"sourceRoot", source.path()}, {"targetRoot", target.path()}, {"sourceVolume", QString::fromStdWString(sourceVolume.name)},
            {"destinationVolume", QString::fromStdWString(targetVolume.name)}, {"passed", failures == 0}, {"cases", results}}).toJson();
        require(report.write(bytes) == bytes.size() && report.commit(), "Test report commit failed");
        std::cout << "Fixtures retained; no recursive cleanup performed.\n";
        return failures ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << "Test stopped: " << error.what() << '\n';
        return 2;
    }
}
