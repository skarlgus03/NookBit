#pragma once
#include <nookbit/application/image_pack_library.hpp>
#include <nookbit/application/image_pack_loader.hpp>
#include <QString>

namespace nookbit {
/// 내장 팩과 앱 소유 폴더만 조회하며 외부 경로를 식별자로 받지 않는 보관소.
class DirectoryImagePackLibrary final : public IImagePackLibrary {
public:
    /// 로더와 내장 매니페스트·사용자 보관 폴더를 주입하는 생성자.
    DirectoryImagePackLibrary(const IImagePackLoader& loader, QString bundledManifest, QString storageRoot);
    /// 유효한 내장 및 가져온 팩 목록을 반환하는 함수.
    Result<std::vector<ImagePackEntry>> list() const override;
    /// 명시된 PNG만 임시 폴더에 복사하고 재검증 후 공개하는 함수.
    Result<ImagePackEntry> importFolder(const std::filesystem::path& folder) override;
    /// 제한된 식별자로 팩을 검증해 읽는 함수.
    Result<ImagePack> load(const std::string& key) const override;
private:
    const IImagePackLoader& loader_;
    QString bundledManifest_, storageRoot_;
};
}
