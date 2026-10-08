#pragma once
#include <nookbit/domain/image_pack.hpp>
#include <vector>

namespace nookbit {
struct ImagePackEntry {
    std::string key, name, version;
};
/// 팩 보관소의 조회·가져오기·읽기를 화면 및 재생기와 분리하는 창구.
class IImagePackLibrary {
public:
    /// 구현체의 자원을 올바르게 해제하는 소멸자.
    virtual ~IImagePackLibrary() = default;
    /// 사용 가능한 팩 목록을 반환하는 함수.
    virtual Result<std::vector<ImagePackEntry>> list() const = 0;
    /// 원본 폴더를 변경하지 않고 검증된 팩을 보관하는 함수.
    virtual Result<ImagePackEntry> importFolder(const std::filesystem::path& folder) = 0;
    /// 목록의 식별자로 팩을 다시 검증해 읽는 함수.
    virtual Result<ImagePack> load(const std::string& key) const = 0;
};
}
