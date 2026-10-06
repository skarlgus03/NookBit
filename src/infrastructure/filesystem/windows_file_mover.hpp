#pragma once

#include <nookbit/domain/result.hpp>
#include <nookbit/domain/model.hpp>

#include <filesystem>

namespace nookbit {

/// Windows에서 목적지 덮어쓰기 없이 같은 볼륨의 파일을 이름 변경하는 도우미.
class WindowsFileMover final {
public:
    /// 승인 객체를 쓰기·교체로부터 보호하고 내용 해시 없이 같은 볼륨에서 이동하는 함수.
    [[nodiscard]] static Result<void> moveMatchingNoReplace(const std::filesystem::path& source,
        const std::filesystem::path& destination, const FileFingerprint& expected);
    /// 링크를 거치지 않는 부모 폴더를 준비하고 파일을 덮어쓰기 없이 옮기는 함수.
    [[nodiscard]] static Result<void> moveNoReplace(
        const std::filesystem::path& source,
        const std::filesystem::path& destination);
};

} // namespace nookbit
