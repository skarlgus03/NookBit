#include "modules/watch/watch_file_readiness.hpp"

#include <algorithm>
#include <cwctype>

namespace nookbit {
namespace {

// Windows 대소문자와 구분자를 정규화해 파일 관찰용 경로 키를 만드는 함수.
std::wstring observationKey(const std::filesystem::path& path) {
    auto key = path.lexically_normal().generic_wstring();
#ifdef _WIN32
    std::transform(key.begin(), key.end(), key.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
#endif
    return key;
}

// 다운로드용 임시 확장자는 안정 시간과 무관하게 후보에서 제외하는 함수.
bool isDownloadTemporaryFile(const FileFact& file) {
    auto extension = file.absolutePath.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
    return extension == L".crdownload" || extension == L".partial" ||
           extension == L".part" || extension == L".download";
}

// 관찰한 파일의 크기·수정 시각·파일 식별자가 같은지 확인하는 함수.
bool sameMetadata(const FileFingerprint& left, const FileFingerprint& right) {
    return left.size == right.size && left.modifiedUtcMs == right.modifiedUtcMs &&
           left.volumeId == right.volumeId && left.fileId == right.fileId;
}

} // namespace

// 기본 30초의 안정 대기 시간을 보관하는 생성자.
WatchFileReadiness::WatchFileReadiness(int stableWindowMs)
    : stableWindowMs_(std::max(0, stableWindowMs)) {}

// 폴더 변경이나 검사 실패 시 이전 관찰 결과를 지우는 함수.
void WatchFileReadiness::reset() {
    observations_.clear();
    lastObservedUtcMs_ = 0;
}

// 현재 파일을 관찰하고 일정 시간 변하지 않은 파일만 반환하는 함수.
WatchReadiness WatchFileReadiness::observe(const std::vector<FileFact>& files, std::int64_t nowUtcMs) {
    if (nowUtcMs < lastObservedUtcMs_) reset();
    lastObservedUtcMs_ = nowUtcMs;
    WatchReadiness result;
    std::map<std::wstring, Observation> current;
    for (const auto& file : files) {
        if (isDownloadTemporaryFile(file)) continue;
        const auto key = observationKey(file.absolutePath);
        const auto previous = observations_.find(key);
        const bool unchanged = previous != observations_.end() &&
                               sameMetadata(previous->second.fingerprint, file.fingerprint);
        const auto stableSince = unchanged ? previous->second.stableSinceUtcMs : nowUtcMs;
        current.emplace(key, Observation{file.fingerprint, stableSince});
        const auto elapsed = static_cast<long double>(nowUtcMs) - stableSince;
        if (elapsed >= stableWindowMs_) {
            result.readyFiles.push_back(file);
        } else {
            ++result.waitingCount;
            const auto remaining = stableWindowMs_ - static_cast<int>(elapsed);
            result.nextCheckDelayMs = result.nextCheckDelayMs == 0 ? remaining :
                std::min(result.nextCheckDelayMs, remaining);
        }
    }
    // 사라지거나 제외된 파일의 관찰 기록을 남겨 두지 않는다.
    observations_ = std::move(current);
    return result;
}

} // namespace nookbit
