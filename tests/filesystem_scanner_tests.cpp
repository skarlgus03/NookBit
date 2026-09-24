#include "infrastructure/filesystem/filesystem_scanner.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

int failures = 0;

// 조건이 거짓이면 테스트 실패를 기록하는 함수.
void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

// 테스트가 만든 임시 폴더만 마지막에 정리하는 보조 객체.
struct FixtureDirectory {
    std::filesystem::path path;
    bool created{false};

    // 고유한 임시 테스트 폴더를 생성하는 함수.
    FixtureDirectory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("nookbit-scanner-test-" + std::to_string(stamp));
        created = std::filesystem::create_directory(path);
    }

    // 자신이 만든 임시 폴더만 삭제하는 함수.
    ~FixtureDirectory() {
        if (created && path.parent_path() == std::filesystem::temp_directory_path() &&
            path.filename().string().starts_with("nookbit-scanner-test-")) {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    }
};

// 스캐너가 읽을 작은 테스트 파일을 작성하는 함수.
bool writeFile(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream stream(path, std::ios::binary);
    if (!stream) return false;
    stream << contents;
    return stream.good();
}

// 재귀 설정과 제외 패턴에 따라 파일 목록이 달라지는지 검사하는 함수.
void testScanDepthAndExclusions() {
    FixtureDirectory fixture;
    expect(fixture.created, "temporary fixture directory should be created");
    if (!fixture.created) return;
    const auto watch = fixture.path / "watch";
    std::filesystem::create_directories(watch / "nested");
    expect(writeFile(watch / "photo.JPG", "image"), "top-level file should be created");
    expect(writeFile(watch / "download.partial", "temporary"), "excluded file should be created");
    expect(writeFile(watch / "nested" / "report.pdf", "pdf"), "nested file should be created");

    nookbit::FilesystemScanner scanner;
    nookbit::ScanRequest request{{watch}, false, {"*.partial"}};
    const auto shallow = scanner.scan(request);
    expect(shallow.hasValue() && shallow.value().files.size() == 1,
           "non-recursive scan should keep only the top-level non-excluded file");
    if (shallow && shallow.value().files.size() == 1) {
        const auto& fact = shallow.value().files.front();
        expect(fact.relativePath == L"photo.JPG", "relative path should use the watch root");
        expect(fact.fingerprint.size == 5, "file size should be captured");
        expect(fact.fingerprint.modifiedUtcMs > 0, "modified time should be captured");
    }

    request.recursive = true;
    const auto recursive = scanner.scan(request);
    expect(recursive.hasValue() && recursive.value().files.size() == 2,
           "recursive scan should include nested files but exclude temporary files");
    if (recursive && recursive.value().files.size() == 2) {
        expect(recursive.value().files[1].relativePath == L"nested/report.pdf" ||
                   recursive.value().files[0].relativePath == L"nested/report.pdf",
               "nested file should retain a relative path");
        expect(!recursive.value().id.empty(), "snapshot should have an ID");
    }
}

// 직접 선택한 파일만 읽고 감시 폴더 제외 패턴은 적용하지 않는지 검사하는 함수.
void testSelectedFilesOnly() {
    FixtureDirectory fixture;
    expect(fixture.created, "temporary fixture directory should be created");
    if (!fixture.created) return;
    const auto watch = fixture.path / "watch";
    std::filesystem::create_directories(watch);
    expect(writeFile(watch / "chosen.txt", "selected"), "selected file should be created");
    expect(writeFile(watch / "other.txt", "other"), "unselected file should be created");
    nookbit::FilesystemScanner scanner;
    nookbit::ScanRequest request{{}, false, {"*.txt"},
        {watch / "chosen.txt", watch / "chosen.txt"}};
    const auto result = scanner.scan(request);
    expect(result.hasValue() && result.value().files.size() == 1,
           "manual scan should read only one selected file despite duplicate paths");
    if (result && result.value().files.size() == 1) {
        expect(result.value().files.front().relativePath == L"chosen.txt",
               "manual scan should use the selected file name as its relative path");
    }
    request.roots = {watch};
    const auto mixed = scanner.scan(request);
    expect(!mixed.hasValue() && mixed.error().code == "scan.input_mode_invalid",
           "folder and explicit-file inputs must not be mixed");
}

// 잘못된 감시 폴더가 빈 스캔으로 처리되지 않는지 검사하는 함수.
void testInvalidRoots() {
    nookbit::FilesystemScanner scanner;
    const auto empty = scanner.scan({});
    expect(!empty.hasValue() && empty.error().code == "scan.input_mode_invalid",
           "empty scanner input should fail");
    const auto relative = scanner.scan({{L"relative-folder"}, false, {}});
    expect(!relative.hasValue() && relative.error().code == "scan.invalid_path",
           "relative watch root should fail");
    const auto missing = scanner.scan({{std::filesystem::temp_directory_path() /
        "nookbit-scanner-definitely-missing"}, false, {}});
    expect(!missing.hasValue(), "missing watch root should fail");
}

// 심볼릭 링크가 만들어진 환경에서는 그 뒤를 순회하지 않는지 검사하는 함수.
void testLinkIsNotFollowed() {
    FixtureDirectory fixture;
    expect(fixture.created, "temporary fixture directory should be created");
    if (!fixture.created) return;
    const auto watch = fixture.path / "watch";
    const auto outside = fixture.path / "outside";
    std::filesystem::create_directories(watch);
    std::filesystem::create_directories(outside);
    expect(writeFile(outside / "secret.txt", "secret"), "outside file should be created");
    std::error_code error;
    std::filesystem::create_directory_symlink(outside, watch / "linked", error);
    if (error) return; // Windows에서 링크 생성 권한이 없으면 이 부분만 건너뛴다.

    nookbit::FilesystemScanner scanner;
    const auto result = scanner.scan({{watch}, true, {}});
    expect(result.hasValue() && result.value().files.empty(),
           "recursive scan must not traverse a directory link");
    if (result) {
        expect(!result.value().warnings.empty(), "skipped link should be reported");
    }
    const auto linkedRoot = scanner.scan({{watch / "linked"}, false, {}});
    expect(!linkedRoot.hasValue() && linkedRoot.error().code == "scan.reparse_path",
           "a linked watch root should be rejected");
}

} // namespace

// 파일 시스템 스캐너 테스트를 실행하고 결과 코드를 반환하는 함수.
int main() {
    testScanDepthAndExclusions();
    testSelectedFilesOnly();
    testInvalidRoots();
    testLinkIsNotFollowed();
    if (failures == 0) {
        std::cout << "All filesystem-scanner tests passed.\n";
        return 0;
    }
    std::cerr << failures << " filesystem-scanner assertion(s) failed.\n";
    return 1;
}
