#include "infrastructure/filesystem/windows_staged_copy.hpp"
#include "infrastructure/filesystem/windows_transfer_source_removal.hpp"
#include "infrastructure/filesystem/windows_transfer_file_inspector.hpp"
#include "infrastructure/filesystem/filesystem_scanner.hpp"
#include "infrastructure/filesystem/file_identity.hpp"
#include "infrastructure/cryptography/qt_plan_hasher.hpp"
#include <nookbit/domain/transfer.hpp>
#include <nookbit/application/prepare_transfer_plan_use_case.hpp>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QProcess>
#include <QDir>
#include <QElapsedTimer>
#include <fstream>
#include <iostream>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif

namespace {
int failures{};
// 다운로드 표식 원문을 보존하고 비지원 스트림/기존 계약을 계속 거부하는 함수.
void testZonePreservation(const std::filesystem::path& root);
// 조건이 거짓이면 실패를 기록하는 함수.
void expect(bool condition, const char* message) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
// 테스트 전용 파일을 작성하는 함수.
void write(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    expect(file.good(), "fixture write");
}
// 기존 스캐너·Qt 해시와 호환되는 원본 지문을 만드는 함수.
nookbit::FileFingerprint fingerprint(const std::filesystem::path& path) {
    nookbit::FilesystemScanner scanner;
    nookbit::QtPlanHasher hasher;
    auto result = nookbit::scanFingerprint(scanner, path, &hasher);
    expect(bool(result), "fixture fingerprint");
    return result ? result.value() : nookbit::FileFingerprint{};
}
// 실패한 복사에서도 원본이 같은 내용을 유지하는지 확인하는 함수.
void unchanged(const std::filesystem::path& source, const nookbit::FileFingerprint& expected) {
    expect(nookbit::sameFingerprint(expected, fingerprint(source)), "source retained unchanged");
}

#ifdef _WIN32
// 보호가 끊기면 내용 검증을 다시 수행하고 같은 보호 세션에서만 증거를 재사용하는 함수.
void testProtectedEvidenceReuse(const std::filesystem::path& root) {
    using namespace nookbit;
    for (int scenario = 0; scenario < 3; ++scenario) {
        const auto folder = root / ("protected-evidence-" + std::to_string(scenario));
        std::filesystem::create_directory(folder);
        const auto source = folder / "source", staging = folder / "stage", final = folder / "final";
        const std::string bytes("owned binary\0payload", 20);
        write(source, bytes);
        const auto expected = fingerprint(source);
        QtPlanHasher hasher;
        expect(expected.sha256 == hasher.sha256(bytes), "file evidence matches independent in-memory hash");
        WindowsStagedCopy copy;
        auto session = copy.open(source, expected, staging);
        expect(bool(session), "protected evidence session opens");
        if (!session) continue;
        const auto verified = session.value()->copyAndVerify();
        expect(verified && verified.value().sha256 == hasher.sha256(bytes), "streaming digest includes binary bytes");
        if (!verified) continue;
        const auto committed = session.value()->commitNoReplace(final);
        expect(bool(committed), "protected evidence commits");
        if (!committed) continue;
        session.value().reset();
        TransferAttempt attempt;
        attempt.id = "protected"; attempt.planId = "plan"; attempt.stepId = "step";
        attempt.source = source; attempt.staging = staging; attempt.destination = final;
        attempt.sourceFingerprint = expected; attempt.stagingFingerprint = verified.value();
        attempt.destinationFingerprint = committed.value(); attempt.phase = TransferPhase::DestinationCommitted;
        if (scenario != 0) {
            const auto changed = scenario == 1 ? source : final;
            const auto time = std::filesystem::last_write_time(changed);
            write(changed, std::string(bytes.size(), 'x'));
            std::filesystem::last_write_time(changed, time);
        }
        WindowsTransferSourceRemoval removal;
        auto guard = removal.open(attempt);
        expect(bool(guard), "removal evidence opens new protection");
        if (!guard) continue;
        if (scenario != 0) {
            const auto refused = guard.value()->removeVerifiedSource();
            expect(!refused && refused.error().code == "transfer.removal_changed",
                "same-size same-time mutation between sessions requires a fresh full hash");
            expect(std::filesystem::exists(source) && std::filesystem::exists(final), "mismatch preserves both files");
            continue;
        }
        expect(guard.value()->verify().hasValue(), "first removal verification hashes reopened evidence");
        for (const auto& path : {source, final}) {
            HANDLE writer = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr, OPEN_EXISTING, 0, nullptr);
            expect(writer == INVALID_HANDLE_VALUE, "cached removal evidence denies writers");
            if (writer != INVALID_HANDLE_VALUE) CloseHandle(writer);
            expect(!DeleteFileW(path.c_str()), "cached removal evidence denies external deletion");
        }
        expect(guard.value()->verify().hasValue(), "same protected removal session reuses evidence");
        expect(guard.value()->removeVerifiedSource().hasValue(), "protected removal succeeds after repeated verification");
        expect(!guard.value()->removeVerifiedSource(), "protected removal cannot run twice");
        guard.value().reset();
        expect(!std::filesystem::exists(source) && sameFingerprint(committed.value(), fingerprint(final)),
            "retained final independently verifies after removal");
    }
}

// 소유한 256 MiB 자료로 복사·확정·삭제 단계 시간을 별도로 측정하는 함수.
void benchmarkProtectedTransfer(const std::filesystem::path& root) {
    using namespace nookbit;
    const auto source = root / "benchmark-source", staging = root / "benchmark-stage", final = root / "benchmark-final";
    write(source, std::string(256 * 1024 * 1024, 'b'));
    const auto expected = fingerprint(source);
    WindowsStagedCopy copy;
    QElapsedTimer timer; timer.start();
    auto session = copy.open(source, expected, staging);
    const auto openedMs = timer.restart();
    expect(bool(session), "benchmark protected copy opens");
    if (!session) return;
    const auto verified = session.value()->copyAndVerify();
    const auto copiedMs = timer.restart();
    expect(bool(verified), "benchmark copy verifies");
    if (!verified) return;
    for (const auto& path : {source, staging}) {
        HANDLE writer = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, 0, nullptr);
        expect(writer == INVALID_HANDLE_VALUE, "verified evidence remains protected from writers");
        if (writer != INVALID_HANDLE_VALUE) CloseHandle(writer);
    }
    const auto committed = session.value()->commitNoReplace(final);
    const auto committedMs = timer.restart();
    expect(bool(committed), "benchmark copy commits");
    if (!committed) return;
    session.value().reset();
    TransferAttempt attempt;
    attempt.id = "benchmark"; attempt.planId = "plan"; attempt.stepId = "step";
    attempt.source = source; attempt.staging = staging; attempt.destination = final;
    attempt.sourceFingerprint = expected; attempt.stagingFingerprint = verified.value();
    attempt.destinationFingerprint = committed.value(); attempt.phase = TransferPhase::DestinationCommitted;
    WindowsTransferSourceRemoval removal;
    auto guard = removal.open(attempt);
    expect(bool(guard), "benchmark removal opens with fresh protection");
    if (!guard) return;
    expect(guard.value()->verify().hasValue(), "benchmark removal validates reopened files");
    const auto removalCheckMs = timer.restart();
    expect(guard.value()->verify().hasValue(), "repeated protected removal verification succeeds");
    expect(guard.value()->removeVerifiedSource().hasValue(), "benchmark removes verified source");
    const auto removedMs = timer.elapsed();
    guard.value().reset();
    expect(!std::filesystem::exists(source) && sameFingerprint(committed.value(), fingerprint(final)),
        "benchmark retained final independently matches contents and identity");
    std::cout << "BENCHMARK 256MiB open=" << openedMs << "ms copy=" << copiedMs << "ms commit=" << committedMs
        << "ms removal-check=" << removalCheckMs << "ms removal=" << removedMs << "ms\n";
}

// 새로 만든 시험 파일의 Zone.Identifier만 복사·확정·보호하는 함수.
void testZonePreservation(const std::filesystem::path& root) {
    using namespace nookbit;
    const auto source = root / "zone-source", staging = root / "zone-stage", final = root / "zone-final";
    const auto zonePath = std::filesystem::path(source.wstring() + L":Zone.Identifier");
    const std::string marker = "[ZoneTransfer]\r\nZoneId=3\r\nHostUrl=https://example.invalid/owned-test\r\n";
    write(source, "owned zone payload"); write(zonePath, marker);
    WindowsStagedCopy copy;
    const auto supported = copy.preflightSource(source);
    expect(supported && supported.value(), "zone preflight detects bounded marker without full contents");
    const auto expected = fingerprint(source);
    expect(!copy.open(source, expected, root / "zone-legacy-stage"), "legacy contract still rejects named streams");
    const auto volume = copy.inspectVolume(source);
    if (!volume) { expect(false, "zone fixture volume"); return; }
    auto guid = volume.value().name;
    for (auto& ch : guid) if (ch >= L'A' && ch <= L'F') ch = ch + (L'a' - L'A');
    PlanVolume approved{volume.value().id, std::string(guid.begin(), guid.end()), "NTFS"};
    MoveContract movement{MoveStrategy::VerifiedCopy, approved, approved, PreservationPolicy::LocalNtfsBasicWithZone};
    auto session = copy.openApproved(source, expected, staging, movement);
    if (!session) { std::cerr << session.error().code << ": " << session.error().message << '\n'; expect(false, "zone protected session"); return; }
    expect(session.value()->copyAndVerify().hasValue(), "zone copy verifies data and marker");
    HANDLE writer = CreateFileW(zonePath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    expect(writer == INVALID_HANDLE_VALUE, "source marker write is blocked during copy session"); if (writer != INVALID_HANDLE_VALUE) CloseHandle(writer);
    const auto committed = session.value()->commitNoReplace(final);
    if (!committed) std::cerr << committed.error().code << ": " << committed.error().message << '\n';
    expect(committed.hasValue(), "zone commit preserves marker"); session.value().reset();
    if (!committed) return;
    std::ifstream copied(std::filesystem::path(final.wstring() + L":Zone.Identifier"), std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(copied)), std::istreambuf_iterator<char>());
    expect(bytes == marker, "final marker bytes exactly preserved");
    copied.close();
    TransferAttempt attempt;
    attempt.id = "owned-zone-attempt"; attempt.planId = "owned-zone-plan"; attempt.stepId = "owned-zone-step";
    attempt.source = source; attempt.staging = staging; attempt.destination = final;
    attempt.sourceFingerprint = expected; attempt.stagingFingerprint = committed.value(); attempt.destinationFingerprint = committed.value();
    attempt.phase = TransferPhase::DestinationCommitted;
    write(zonePath, "owned changed marker");
    WindowsTransferSourceRemoval removal;
    auto guard = removal.openApproved(attempt, movement);
    expect(bool(guard), "zone removal guard opens without deleting");
    if (guard) {
        const auto refused = guard.value()->removeVerifiedSource();
        expect(!refused && refused.error().code == "transfer.zone_changed", "marker mismatch refuses source deletion");
        guard.value().reset();
    }
    expect(std::filesystem::exists(source) && std::filesystem::exists(final), "marker mismatch retains both copies");
    write(std::filesystem::path(source.wstring() + L":extra"), "owned unsupported stream");
    expect(!copy.preflightSource(source), "unknown named stream is rejected at preflight");
    expect(!std::filesystem::exists(root / "zone-legacy-stage"), "early rejected legacy stream creates no staging");
    const auto oversized = root / "oversized-zone";
    write(oversized, "owned oversized marker fixture");
    write(std::filesystem::path(oversized.wstring() + L":Zone.Identifier"), std::string(65537, 'z'));
    expect(!copy.preflightSource(oversized), "oversized download marker is rejected before full content hashing");
}

// 기록된 임시 객체의 앞부분을 검증하고 나머지만 이어 쓰는 경계를 검사하는 함수.
void testPartialCopyResume(const std::filesystem::path& root) {
    using namespace nookbit;
    std::string bytes(3 * 1024 * 1024 + 37, 'a');
    for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<char>(i % 251);
    for (int scenario = 0; scenario <= 11; ++scenario) {
        const auto folder = root / ("resume-" + std::to_string(scenario));
        std::filesystem::create_directory(folder);
        TransferAttempt attempt;
        attempt.id = "resume"; attempt.planId = "plan"; attempt.stepId = "step";
        attempt.source = folder / "source"; attempt.staging = folder / "stage"; attempt.destination = folder / "final";
        write(attempt.source, bytes); write(attempt.staging, "");
        attempt.sourceFingerprint = fingerprint(attempt.source);
        attempt.stagingFingerprint = fingerprint(attempt.staging);
        attempt.phase = TransferPhase::Cancelled;
        const size_t length = scenario == 8 ? 0 : scenario == 9 ? bytes.size() : 1024 * 1024 + 37;
        write(attempt.staging, bytes.substr(0, length));
        if (scenario == 1) write(attempt.staging, bytes + "extra");
        if (scenario == 2) write(attempt.source, std::string(bytes.size(), 'x'));
        if (scenario == 3) {
            std::filesystem::rename(attempt.staging, folder / "held-stage");
            write(attempt.staging, bytes.substr(0, length));
        }
        if (scenario == 4) write(attempt.staging, std::string(length, 'x'));
        if (scenario == 5) write(attempt.destination, "keep existing final");
        if (scenario == 6) attempt.stagingFingerprint.reset();
        const auto before = fingerprint(attempt.staging);
        HANDLE blocker = INVALID_HANDLE_VALUE;
        if (scenario == 10) blocker = CreateFileW(attempt.staging.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        WindowsStagedCopy worker;
        std::optional<MoveContract> movement;
        if (scenario == 11) {
            OperationPlan draft;
            MoveStep step; step.source = attempt.source; step.destination = attempt.destination; step.fingerprint = attempt.sourceFingerprint;
            draft.steps.push_back(step);
            PrepareTransferPlanUseCase prepare(worker);
            const auto contract = prepare.execute(draft);
            expect(contract.hasValue(), "native resume volume fixture");
            if (!contract) continue;
            // 한 볼륨의 저수준 어댑터 검사이며 실제 두 볼륨 승인 계획은 아니다.
            movement = *contract.value().steps[0].movement;
            movement->strategy = MoveStrategy::VerifiedCopy; movement->destinationVolume.serial = "0";
        }
        // 취소된 검사에서는 임시 파일에 아무것도 쓰지 않는지 확인하는 함수.
        auto session = worker.openResume(attempt, movement ? &*movement : nullptr, [&] { return scenario == 7; });
        if (scenario == 0 || scenario == 8 || scenario == 9) {
            expect(session.hasValue(), "valid recorded prefix opens resume session");
            if (session) {
                const auto verified = session.value()->copyAndVerify();
                expect(verified && TransferValidation::sameContent(attempt.sourceFingerprint, verified.value()) &&
                    TransferValidation::sameObject(*attempt.stagingFingerprint, verified.value()), "resume retains staging identity and verifies full binary contents");
                expect(!session.value()->copyAndVerify(), "resume session cannot copy twice");
            }
            expect(!std::filesystem::exists(attempt.destination), "resume copy alone does not commit a final file");
            unchanged(attempt.source, attempt.sourceFingerprint);
        } else {
            expect(!session, "invalid prefix, identity, source, destination or lock refuses resume");
            if (blocker != INVALID_HANDLE_VALUE) CloseHandle(blocker);
            unchanged(attempt.staging, before);
            if (scenario != 2) unchanged(attempt.source, attempt.sourceFingerprint);
            if (scenario == 5) expect(std::filesystem::exists(attempt.destination), "occupied final retained");
        }
    }
}

// 승인된 로컬 NTFS 부모 생성의 성공·거부·보호 경계를 실제 임시 자료로 검사하는 함수.
void testApprovedParentCreation(const std::filesystem::path& root) {
    using namespace nookbit;
    WindowsStagedCopy worker;
    const auto source = root / "parent-source";
    write(source, "parent creation source");
    const auto expected = fingerprint(source);
    OperationPlan draft;
    MoveStep step;
    step.source = source; step.destination = root / "new-parent" / "nested" / L"최종-문서.txt"; step.fingerprint = expected;
    draft.steps.push_back(step);
    PrepareTransferPlanUseCase prepare(worker);
    const auto plan = prepare.execute(draft);
    expect(plan.hasValue(), "parent creation volume contract fixture");
    if (!plan) return;
    auto movement = *plan.value().steps[0].movement;
    // 한 볼륨에서 저수준 복사 어댑터를 검사할 뿐 실제 두 볼륨 승인 계획은 아니다.
    movement.strategy = MoveStrategy::VerifiedCopy;
    {
        auto session = worker.openApproved(source, expected, root / "new-parent" / "nested" / "stage", movement);
        if (!session) std::cerr << session.error().code << ": " << session.error().message << '\n';
        expect(session.hasValue(), "approved copy creates missing parent chain");
        if (session) {
            expect(std::filesystem::is_directory(root / "new-parent" / "nested"), "nested target parents created");
            expect(!MoveFileW((root / "new-parent").c_str(), (root / "renamed-new-parent").c_str()), "new parents protected from rename during copy");
            const auto writableParent = CreateFileW((root / "new-parent" / "nested").c_str(), GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
            expect(writableParent == INVALID_HANDLE_VALUE && GetLastError() == ERROR_SHARING_VIOLATION,
                "parent protection denies writable directory handles needed for reparse replacement");
            if (writableParent != INVALID_HANDLE_VALUE) CloseHandle(writableParent);
            expect(session.value()->copyAndVerify().hasValue(), "copy verifies in newly created parent");
            expect(session.value()->commitNoReplace(step.destination).hasValue(), "copy commits without replacing in new parent");
        }
    }
    expect(std::filesystem::exists(step.destination), "new parent contains verified final copy");
    unchanged(source, expected);
    auto wrong = movement;
    wrong.destinationVolume.guidPath[11] = wrong.destinationVolume.guidPath[11] == 'a' ? 'b' : 'a';
    expect(!worker.openApproved(source, expected, root / "wrong-target-volume" / "nested" / "stage", wrong), "wrong approved destination GUID rejected");
    expect(!std::filesystem::exists(root / "wrong-target-volume"), "destination volume mismatch creates no folders");
    wrong = movement; wrong.sourceVolume.serial = "0";
    expect(!worker.openApproved(source, expected, root / "wrong-source-volume" / "nested" / "stage", wrong), "wrong approved source volume rejected");
    expect(!std::filesystem::exists(root / "wrong-source-volume"), "source volume mismatch creates no folders");
    auto stale = expected; stale.sha256 = std::string(64, '0');
    expect(!worker.openApproved(source, stale, root / "stale-source" / "nested" / "stage", movement), "changed source rejected before parent creation");
    expect(!std::filesystem::exists(root / "stale-source"), "source hash mismatch creates no folders");
    expect(!worker.open(source, expected, root / "legacy-missing" / "stage") && !std::filesystem::exists(root / "legacy-missing"),
        "unsigned legacy parent creation stays disabled");
    write(root / "file-parent", "existing file must remain");
    const auto occupied = fingerprint(root / "file-parent");
    expect(!worker.openApproved(source, expected, root / "file-parent" / "nested" / "stage", movement), "file in parent chain is never replaced");
    unchanged(root / "file-parent", occupied);
    const auto partial = root / "partial-created-parent";
    expect(!worker.openApproved(source, expected, partial / std::string(300, 'x') / "stage", movement),
        "later invalid parent component fails without creating staging");
    expect(std::filesystem::is_directory(partial), "folders already created before later failure are retained without blind cleanup");
    const auto busy = root / "busy-existing-parent";
    std::filesystem::create_directory(busy);
    HANDLE parentLock = CreateFileW(busy.c_str(), FILE_READ_ATTRIBUTES | DELETE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    expect(parentLock != INVALID_HANDLE_VALUE, "busy parent fixture lock");
    const auto blocked = worker.openApproved(source, expected, busy / "nested" / "stage", movement);
    expect(!blocked && !std::filesystem::exists(busy / "nested"),
        "inaccessible existing parent fails without descending or creating children");
    if (parentLock != INVALID_HANDLE_VALUE) CloseHandle(parentLock);
    expect(!worker.openApproved(source, expected, root / "device-prefix" / "COM1.txt" / "stage", movement) &&
        !std::filesystem::exists(root / "device-prefix"), "device parent is rejected before any folder creation");
    expect(!worker.openApproved(source, expected, root / "traversal-prefix" / ".." / "stage", movement) &&
        !std::filesystem::exists(root / "traversal-prefix"), "parent traversal rejected before creation");
    {
        auto session = worker.openApproved(source, expected, root / "cancel-parent" / "nested" / "stage", movement);
        expect(session.hasValue(), "cancelled copy parent fixture");
        if (session) expect(!session.value()->copyAndVerify([] { return true; }), "cancelled copy preserves source and created folders");
    }
    expect(std::filesystem::is_directory(root / "cancel-parent" / "nested") &&
        std::filesystem::exists(root / "cancel-parent" / "nested" / "stage"), "failure does not delete new folders or staging automatically");
    unchanged(source, expected);

    const auto redirected = root / "junction-target";
    const auto junction = root / "junction-parent";
    std::filesystem::create_directory(redirected);
    QProcess command;
    // 테스트용 명령 창을 표시하지 않고 같은 임시 폴더 안에만 junction을 만드는 함수.
    command.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) { args->flags |= CREATE_NO_WINDOW; });
    command.start(qEnvironmentVariable("ComSpec"), {"/c", "mklink", "/J", QDir::toNativeSeparators(QString::fromStdWString(junction.wstring())),
        QDir::toNativeSeparators(QString::fromStdWString(redirected.wstring()))});
    const bool linked = command.waitForFinished(5000) && command.exitCode() == 0;
    if (!linked) std::cerr << command.errorString().toStdString() << ": " << command.readAllStandardError().toStdString() <<
        command.readAllStandardOutput().toStdString() << '\n';
    expect(linked, "native NTFS junction fixture should be created");
    if (linked) {
        expect(!worker.openApproved(source, expected, junction / "nested" / "stage", movement), "junction parent rejected before descent");
        expect(!std::filesystem::exists(redirected / "nested"), "rejected junction creates no redirected folders or files");
        expect(RemoveDirectoryW(junction.c_str()), "remove only test junction without touching target");
    }
    unchanged(source, expected);
}
#endif
}

// 실제 임시 NTFS 파일로 복사 세션의 안전 경계를 시험하는 함수.
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
#ifdef _WIN32
    QTemporaryDir fixture(QCoreApplication::applicationDirPath() + "/staged-copy-XXXXXX");
    expect(fixture.isValid(), "temporary directory");
    const std::filesystem::path root(fixture.path().toStdWString());
    testProtectedEvidenceReuse(root);
    if (qEnvironmentVariableIsSet("NOOKBIT_BENCHMARK_COPY")) benchmarkProtectedTransfer(root);
    testApprovedParentCreation(root);
    testPartialCopyResume(root);
    nookbit::WindowsStagedCopy worker;
    const auto volume = worker.inspectVolume(root / "missing" / "nested" / "file");
    expect(bool(volume) && volume.value().localNtfs, "inspect existing ancestor on local NTFS");
    expect(bool(volume) && volume.value().name.starts_with(L"\\\\?\\Volume{"), "stable volume GUID inspected");
    expect(!std::filesystem::exists(root / "missing"), "volume inspection makes no folders");
    const auto contractSource = root / "contract-source";
    write(contractSource, "volume contract source");
    nookbit::OperationPlan contractDraft;
    nookbit::MoveStep contractStep;
    contractStep.source = contractSource;
    contractStep.destination = root / "missing" / "nested" / "destination";
    contractStep.fingerprint = fingerprint(contractSource);
    contractDraft.steps.push_back(contractStep);
    nookbit::PrepareTransferPlanUseCase prepare(worker);
    const auto contractPlan = prepare.execute(contractDraft);
    expect(contractPlan && contractPlan.value().steps[0].movement->strategy == nookbit::MoveStrategy::Rename,
           "native volume query should prepare a same-volume v2 contract");
    expect(contractPlan && prepare.verify(contractPlan.value()).hasValue(), "native approved-volume recheck should match");
    expect(!std::filesystem::exists(root / "missing"), "plan preparation and recheck must not create destination parents");
    unchanged(contractSource, contractStep.fingerprint);
    if (contractPlan) {
        auto movement = *contractPlan.value().steps[0].movement;
        movement.strategy = nookbit::MoveStrategy::VerifiedCopy;
        movement.destinationVolume.guidPath[11] = movement.destinationVolume.guidPath[11] == 'a' ? 'b' : 'a';
        const auto rejected = worker.openApproved(contractSource, contractStep.fingerprint, root / "contract-stage", movement);
        expect(!rejected && rejected.error().code == "transfer.volume_changed" && !std::filesystem::exists(root / "contract-stage"),
               "protected destination GUID mismatch must reject before staging creation");
        movement = *contractPlan.value().steps[0].movement;
        movement.strategy = nookbit::MoveStrategy::VerifiedCopy;
        movement.sourceVolume.serial = "0";
        expect(!worker.openApproved(contractSource, contractStep.fingerprint, root / "contract-stage", movement),
               "protected source serial mismatch must reject");
    }

    for (const auto size : {size_t(0), size_t(5), size_t(2 * 1024 * 1024 + 13)}) {
        const auto source = root / ("source-" + std::to_string(size));
        const auto staging = root / ("stage-" + std::to_string(size));
        write(source, std::string(size, 'x'));
        const auto expected = fingerprint(source);
        auto session = worker.open(source, expected, staging);
        if (!session) std::cerr << session.error().code << ": " << session.error().message << '\n';
        expect(bool(session), "create staging session");
        if (!session) continue;
        const auto created = session.value()->createdFingerprint();
        expect(created.size == 0 && !created.sha256, "created evidence before data copy");
        expect(!nookbit::TransferValidation::sameObject(expected, created), "copy has different object ID");
        HANDLE mutation = CreateFileW(source.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE |
            FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        expect(mutation == INVALID_HANDLE_VALUE, "source write blocked during session");
        if (mutation != INVALID_HANDLE_VALUE) CloseHandle(mutation);
        expect(!DeleteFileW(source.c_str()), "source removal blocked during session");
        mutation = CreateFileW(staging.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE |
            FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        expect(mutation == INVALID_HANDLE_VALUE, "staging write blocked during session");
        if (mutation != INVALID_HANDLE_VALUE) CloseHandle(mutation);
        const auto verified = session.value()->copyAndVerify();
        if (!verified) std::cerr << verified.error().code << ": " << verified.error().message << '\n';
        expect(bool(verified), "copy verified");
        if (verified) {
            expect(nookbit::TransferValidation::sameContent(expected, verified.value()), "content matches");
            expect(nookbit::TransferValidation::sameObject(created, verified.value()), "staging object retained");
            expect(expected.modifiedUtcMs == verified.value().modifiedUtcMs, "modified time preserved");
        }
        expect(!session.value()->copyAndVerify(), "session cannot run twice");
        session.value().reset();
        unchanged(source, expected);
        if (verified) expect(nookbit::sameFingerprint(verified.value(), fingerprint(staging)), "verified file on disk");
    }

    const auto source = root / "failure-source";
    const auto staging = root / "failure-stage";
    write(source, std::string(3 * 1024 * 1024, 'c'));
    const auto expected = fingerprint(source);
    auto incorrect = expected;
    incorrect.sha256 = std::string(64, '0');
    expect(!worker.open(source, incorrect, staging), "wrong source hash rejected");
    expect(!std::filesystem::exists(staging), "rejection before staging creation");
    write(staging, "do not overwrite");
    const auto existing = fingerprint(staging);
    expect(!worker.open(source, expected, staging), "existing staging rejected");
    unchanged(staging, existing);
    auto session = worker.open(source, expected, root / "cancel-stage");
    expect(bool(session), "cancel fixture session");
    if (session) {
        int checks{};
        const auto cancelled = session.value()->copyAndVerify([&] { return ++checks == 2; });
        expect(!cancelled && cancelled.error().code == "transfer.cancelled", "copy cancellation reported");
        session.value().reset();
        expect(std::filesystem::file_size(root / "cancel-stage") == 1024 * 1024, "partial copy retained for journal recovery");
    }
    unchanged(source, expected);
    HANDLE writer = CreateFileW(source.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE |
        FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    expect(writer != INVALID_HANDLE_VALUE, "existing writer fixture");
    expect(!worker.open(source, expected, root / "writer-stage"), "source with existing writer rejected");
    nookbit::WindowsTransferFileInspector inspector;
    const auto unavailable = inspector.inspect(source);
    expect(unavailable.presence == nookbit::TransferPathPresence::Unavailable && unavailable.error,
        "read-only inspector reports existing writer as unavailable, not missing");
    if (writer != INVALID_HANDLE_VALUE) CloseHandle(writer);
    expect(!worker.open(source, expected, root / "no-parent" / "stage"), "missing target parent rejected");
    expect(!worker.open(source, expected, root / "stage:stream"), "alternate stream target rejected");
    expect(!worker.inspectVolume(root / ".." / "other"), "parent traversal rejected");

    write(source, "changed before session");
    expect(!worker.open(source, expected, root / "changed-stage"), "changed source rejected");
    expect(!std::filesystem::exists(root / "changed-stage"), "changed source creates nothing");
    auto now = fingerprint(source);
    expect(CreateHardLinkW((root / "link").c_str(), source.c_str(), nullptr), "hardlink fixture");
    expect(!worker.open(source, now, root / "hardlink-stage"), "multiple hardlinks rejected");
    expect(DeleteFileW((root / "link").c_str()), "remove fixture hardlink");
    write(std::filesystem::path(source.wstring() + L":extra"), "named stream");
    expect(!worker.open(source, now, root / "ads-stage"), "named streams rejected");
    expect(DeleteFileW((source.wstring() + L":extra").c_str()), "remove fixture stream");
    expect(nookbit::TransferValidation::sameContent(now, fingerprint(source)), "main stream retained after ADS fixture");
    testZonePreservation(root);

    const auto parent = root / "locked-parent";
    std::filesystem::create_directory(parent);
    write(parent / "source", "attributes");
    expect(SetFileAttributesW((parent / "source").c_str(), FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN), "attribute fixture");
    auto attributeSession = worker.open(parent / "source", fingerprint(parent / "source"), parent / "stage");
    expect(bool(attributeSession), "readonly source accepted");
    if (attributeSession) {
        expect(!MoveFileW(parent.c_str(), (root / "renamed-parent").c_str()), "parent rename blocked");
        const auto result = attributeSession.value()->copyAndVerify();
        expect(bool(result), "readonly attributes copied and verified");
        const auto committed = attributeSession.value()->commitNoReplace(parent / "final");
        if (!committed) std::cerr << committed.error().code << ": " << committed.error().message << '\n';
        expect(bool(committed), "readonly verified copy renamed without losing attributes");
        attributeSession.value().reset();
        const auto copiedPath = std::filesystem::exists(parent / "final") ? parent / "final" : parent / "stage";
        expect(GetFileAttributesW(copiedPath.c_str()) == (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN), "attributes preserved on disk");
        expect(SetFileAttributesW(copiedPath.c_str(), FILE_ATTRIBUTE_NORMAL), "unlock fixture staging cleanup");
    }
    expect(SetFileAttributesW((parent / "source").c_str(), FILE_ATTRIBUTE_NORMAL), "unlock fixture source cleanup");
    expect(MoveFileW(parent.c_str(), (root / "renamed-parent").c_str()), "parent unlocked after session closes");
    const auto commitSource = root / "commit-source";
    write(commitSource, "commit contents");
    auto commit = worker.open(commitSource, fingerprint(commitSource), root / "commit-stage");
    expect(bool(commit), "direct commit session");
    if (commit) {
        expect(!commit.value()->commitNoReplace(root / "commit-final"), "unverified commit rejected");
        expect(commit.value()->copyAndVerify().hasValue(), "copy after premature commit rejection");
        const auto result = commit.value()->commitNoReplace(root / "commit-final");
        expect(bool(result), "direct verified rename succeeds");
        expect(!commit.value()->commitNoReplace(root / "another-final"), "commit cannot run twice");
    }
#else
    nookbit::WindowsStagedCopy worker;
    expect(!worker.inspectVolume("/tmp"), "unsupported platform rejected");
#endif
    return failures ? 1 : 0;
}
