#pragma once

#include <nookbit/application/ports.hpp>
#include <QString>

namespace nookbit {

/// 대기 파일 경로만 버전이 있는 JSON으로 원자적으로 보관하는 저장소.
class JsonPendingFilesRepository final : public IPendingFilesRepository {
public:
    /// 저장 파일의 경로를 보관하는 생성자.
    explicit JsonPendingFilesRepository(QString storagePath);

    /// 문서 전체를 검증한 뒤 대기 경로를 읽는 함수.
    [[nodiscard]] Result<std::vector<std::filesystem::path>> load() const override;

    /// 경로를 검증하고 이전 문서를 원자적으로 교체하는 함수.
    [[nodiscard]] Result<void> save(const std::vector<std::filesystem::path>& paths) const override;
    /// 경로와 규칙에 속한 폴더 처리 선택을 함께 복원하는 함수.
    [[nodiscard]] Result<std::vector<PendingFileEntry>> loadEntries() const override;
    /// 폴더별 선택까지 포함해 대기 문서를 원자적으로 교체하는 함수.
    [[nodiscard]] Result<void> saveEntries(const std::vector<PendingFileEntry>& entries) const override;

private:
    QString storagePath_;
};

} // namespace nookbit
