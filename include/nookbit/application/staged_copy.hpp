#pragma once

#include <nookbit/domain/model.hpp>
#include <functional>
#include <memory>

namespace nookbit {
struct TransferAttempt;

struct TransferVolume {
    /// 기존 파일 지문과 호환되는 볼륨 일련번호.
    std::string id;
    /// 드라이브 글자와 독립적인 Windows 볼륨 GUID 경로.
    std::wstring name;
    std::wstring filesystem;
    bool localNtfs{false};
};

/// 핸들을 유지하는 동안 원본·임시 파일과 부모 경로의 교체를 막는 복사 세션.
class IStagedCopySession {
public:
    /// 파일을 삭제하지 않고 보유한 핸들만 해제하는 함수.
    virtual ~IStagedCopySession() = default;
    /// 세션을 연 시점의 임시 파일 객체 지문을 반환하는 함수.
    [[nodiscard]] virtual const FileFingerprint& createdFingerprint() const = 0;
    /// 한 번만 복사하고 내용을 재검증하며 취소·실패 시에도 원본을 유지하는 함수.
    [[nodiscard]] virtual Result<FileFingerprint> copyAndVerify(
        const std::function<bool()>& cancelled = {}) = 0;
    /// 검증된 임시 파일만 같은 부모의 최종 이름으로 덮어쓰기 없이 확정하는 함수.
    [[nodiscard]] virtual Result<FileFingerprint> commitNoReplace(const std::filesystem::path& destination) = 0;
};

/// 계획 단계는 복사 기능이 아닌 볼륨 조회 인터페이스만 사용한다.
class ITransferVolumeInspector {
public:
    /// 볼륨 조회 일꾼을 해제하는 함수.
    virtual ~ITransferVolumeInspector() = default;
    /// 파일을 생성하지 않고 가장 가까운 기존 경로의 실제 볼륨을 조회하는 함수.
    [[nodiscard]] virtual Result<TransferVolume> inspectVolume(const std::filesystem::path& path) = 0;
};

class IStagedCopy : public ITransferVolumeInspector {
public:
    /// 새 임시 파일 생성 전 승인 원본의 전체 내용 검증·보호를 보장하는지 반환하는 함수.
    [[nodiscard]] virtual bool verifiesSourceBeforeStaging() const { return false; }
    /// 복사 일꾼을 해제하는 함수.
    ~IStagedCopy() override = default;
    /// 기록된 임시 객체·원본·앞부분을 보호 검증해 이어쓰기 세션만 여는 함수.
    [[nodiscard]] virtual Result<std::unique_ptr<IStagedCopySession>> openResume(
        const TransferAttempt&, const MoveContract*, const std::function<bool()>& = {}) {
        return Result<std::unique_ptr<IStagedCopySession>>::failure({"transfer.resume_unsupported", "Protected copy resume unsupported"});
    }
    /// 기록된 전체 검증 복사본을 다시 보호 검사하고 확정 전용 세션을 여는 함수.
    [[nodiscard]] virtual Result<std::unique_ptr<IStagedCopySession>> openVerified(
        const TransferAttempt&, const MoveContract*, const std::function<bool()>& = {}) {
        return Result<std::unique_ptr<IStagedCopySession>>::failure({"transfer.commit_resume_unsupported", "Protected staging commit unsupported"});
    }
    /// 원본을 검증하고 기존 파일을 덮어쓰지 않는 빈 임시 파일 세션을 만드는 함수.
    [[nodiscard]] virtual Result<std::unique_ptr<IStagedCopySession>> open(
        const std::filesystem::path& source, const FileFingerprint& expected,
        const std::filesystem::path& staging) = 0;
    /// 보호된 원본·승인 볼륨을 검증한 뒤 필요한 부모와 새 임시 파일을 만드는 함수.
    [[nodiscard]] virtual Result<std::unique_ptr<IStagedCopySession>> openApproved(
        const std::filesystem::path&, const FileFingerprint&, const std::filesystem::path&, const MoveContract&) {
        return Result<std::unique_ptr<IStagedCopySession>>::failure({"transfer.contract_unsupported", "Approved volume handles are required."});
    }
};

} // namespace nookbit
