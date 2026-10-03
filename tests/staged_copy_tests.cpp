#include "infrastructure/filesystem/windows_staged_copy.hpp"
#include "infrastructure/filesystem/windows_transfer_file_inspector.hpp"
#include "infrastructure/filesystem/filesystem_scanner.hpp"
#include "infrastructure/filesystem/file_identity.hpp"
#include "infrastructure/cryptography/qt_plan_hasher.hpp"
#include <nookbit/domain/transfer.hpp>
#include <nookbit/application/prepare_transfer_plan_use_case.hpp>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <fstream>
#include <iostream>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif

namespace {
int failures{};
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
}

// 실제 임시 NTFS 파일로 복사 세션의 안전 경계를 시험하는 함수.
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
#ifdef _WIN32
    QTemporaryDir fixture(QCoreApplication::applicationDirPath() + "/staged-copy-XXXXXX");
    expect(fixture.isValid(), "temporary directory");
    const std::filesystem::path root(fixture.path().toStdWString());
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
