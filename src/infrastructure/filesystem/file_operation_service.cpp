#include "file_operation_service.hpp"
#include "safe_executor.hpp"

namespace nookbit {
namespace {
/// 한 요청의 설정 스냅샷만 제공하며 사용자 저장소를 변경하지 않는 어댑터.
class OperationSettings final : public ISettingsRepository {
    UserSettings value_;
public:
    /// 전달받은 현재 설정을 요청의 수명 안에서 보관하는 함수.
    explicit OperationSettings(UserSettings value) : value_(std::move(value)) {}
    /// 승인 재검증에 사용할 같은 설정 스냅샷을 반환하는 함수.
    Result<UserSettings> load() const override { return Result<UserSettings>::success(value_); }
    /// 요청 설정은 저장하지 않는 함수.
    Result<void> save(const UserSettings&) override { return Result<void>::failure({"settings.read_only", "Operation settings are not persisted."}); }
};
}
// 앱 조립부에서 생성한 포트들의 수명과 보호 경로를 빌려 받는 함수.
FileOperationService::FileOperationService(IVerifySavedPlanUseCase& verifier, IScanner& scanner, IPlanHasher& hasher,
    IHistoryRepository& history, std::filesystem::path appDataRoot, ITransferVolumeInspector* volumes,
    ITransferStepExecutor* forward, IRollbackTransferStepExecutor* reverse)
    : verifier_(verifier), scanner_(scanner), hasher_(hasher), history_(history), appDataRoot_(std::move(appDataRoot)),
      volumes_(volumes), forward_(forward), reverse_(reverse) {}
// 설정 스냅샷과 외부 포트로 실행기를 조립하고 기존 승인·잠금·보상 절차를 호출하는 함수.
Result<void> FileOperationService::executeApproved(const OperationPlan& plan, const ApprovalRecord& approval,
    const std::vector<std::filesystem::path>& selected, const UserSettings& current) {
    OperationSettings settings(current);
    SafeExecutor executor(settings, verifier_, scanner_, hasher_, history_, appDataRoot_, volumes_, forward_, reverse_);
    return executor.execute(plan, approval, selected);
}
// 되돌리기에서 UI 설정을 읽지 않고 저장된 경로·볼륨·전송 증거를 사용하는 함수.
Result<void> FileOperationService::rollback(const PlanId& planId) {
    OperationSettings unusedSettings(UserSettings{});
    SafeExecutor executor(unusedSettings, scanner_, hasher_, history_, appDataRoot_, volumes_, reverse_);
    return executor.rollback(planId);
}
}
