#pragma once
#include <nookbit/application/image_pack_loader.hpp>
#include <QString>

namespace nookbit {
/// JSON과 PNG 헤더를 검증해 공통 프레임으로 변환하는 로더.
class JsonImagePackLoader final : public IImagePackLoader {
public:
    /// 검증용 Schema 위치를 보관하는 생성자.
    explicit JsonImagePackLoader(QString schemaPath = QStringLiteral(":/nookbit/imagepack.schema.json"));
    /// 매니페스트와 이미지 크기·참조를 검증해 팩을 반환하는 함수.
    [[nodiscard]] Result<ImagePack> load(const std::filesystem::path& manifestPath) const override;
private:
    QString schemaPath_;
};
} // namespace nookbit
