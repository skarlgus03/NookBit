#pragma once
#include <nookbit/domain/image_pack.hpp>
#include <nookbit/domain/result.hpp>

namespace nookbit {
/// 저장 위치나 JSON 처리 구현을 호출자에게 노출하지 않는 ImagePack 로딩 계약.
class IImagePackLoader {
public:
    /// 인터페이스를 통해 구현을 안전하게 해제하는 소멸자.
    virtual ~IImagePackLoader() = default;
    /// 로컬 절대 경로에서 공통 프레임을 가진 팩을 읽는 함수.
    [[nodiscard]] virtual Result<ImagePack> load(const std::filesystem::path& manifestPath) const = 0;
};
} // namespace nookbit
