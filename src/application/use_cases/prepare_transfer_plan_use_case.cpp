#include <nookbit/application/prepare_transfer_plan_use_case.hpp>
#include "application/use_cases/plan_signature.hpp"

namespace nookbit {
namespace {
// Windows 볼륨 정보의 ASCII 식별자를 정규 형식으로 변환하는 함수.
Result<PlanVolume> convert(const TransferVolume& volume) {
    if (!volume.localNtfs || volume.filesystem != L"NTFS")
        return Result<PlanVolume>::failure({"plan.volume_unsupported", "Only fixed local NTFS volumes are supported."});
    std::string guid;
    for (const auto ch : volume.name) {
        if (ch < 0 || ch > 127)
            return Result<PlanVolume>::failure({"plan.volume_invalid", "Volume GUID must be ASCII."});
        // Volume 접두사를 유지하고 GUID의 A-F만 소문자로 정규화한다.
        guid += static_cast<char>(ch >= L'A' && ch <= L'F' ? ch + (L'a' - L'A') : ch);
    }
    return Result<PlanVolume>::success({volume.id, guid, "NTFS"});
}

// 볼륨 계약의 불변 필드 전체가 같은지 비교하는 함수.
bool sameVolume(const PlanVolume& left, const PlanVolume& right) {
    return left.serial == right.serial && left.guidPath == right.guidPath && left.filesystem == right.filesystem;
}
}

// 복사·삭제 권한 없이 볼륨 조회 기능만 받는 생성자.
PrepareTransferPlanUseCase::PrepareTransferPlanUseCase(ITransferVolumeInspector& volumes) : volumes_(volumes) {}

// 드라이브 문자가 아닌 실제 볼륨 GUID로 이동 방식을 선택하는 함수.
Result<MoveContract> PrepareTransferPlanUseCase::inspect(const MoveStep& step) {
    const auto source = volumes_.inspectVolume(step.source);
    if (!source) return Result<MoveContract>::failure(source.error());
    const auto destination = volumes_.inspectVolume(step.destination);
    if (!destination) return Result<MoveContract>::failure(destination.error());
    const auto a = convert(source.value()), b = convert(destination.value());
    if (!a) return Result<MoveContract>::failure(a.error());
    if (!b) return Result<MoveContract>::failure(b.error());
    return Result<MoveContract>::success({a.value().guidPath == b.value().guidPath ?
        MoveStrategy::Rename : MoveStrategy::VerifiedCopy, a.value(), b.value(), PreservationPolicy::LocalNtfsBasic});
}

// 초안만 새 계약으로 확장하며 이미 서명된 계획은 변경하지 않는 함수.
Result<OperationPlan> PrepareTransferPlanUseCase::execute(const OperationPlan& draft) {
    if (draft.status != PlanStatus::Draft || !draft.planHash.empty())
        return Result<OperationPlan>::failure({"plan.contract_already_frozen", "Only unsigned drafts may acquire new volume contracts."});
    const auto format = PlanSignature::validateFormat(draft);
    if (!format) return Result<OperationPlan>::failure(format.error());
    auto result = draft;
    result.formatVersion = 2;
    for (auto& step : result.steps) {
        const auto contract = inspect(step);
        if (!contract) return Result<OperationPlan>::failure(contract.error());
        step.movement = contract.value();
    }
    const auto valid = PlanSignature::validateFormat(result);
    if (!valid) return Result<OperationPlan>::failure(valid.error());
    return Result<OperationPlan>::success(std::move(result));
}

// 볼륨 교체·이동 방식 변경을 거부하고 기존 승인 데이터를 그대로 유지하는 함수.
Result<void> PrepareTransferPlanUseCase::verify(const OperationPlan& plan) {
    const auto format = PlanSignature::validateFormat(plan);
    if (!format) return format;
    if (plan.formatVersion != 2)
        return Result<void>::failure({"plan.contract_missing", "V2 volume contracts are required."});
    for (const auto& step : plan.steps) {
        const auto current = inspect(step);
        if (!current) return Result<void>::failure(current.error());
        const auto& saved = *step.movement;
        if ((saved.strategy == MoveStrategy::MetadataRename ? current.value().strategy != MoveStrategy::Rename : saved.strategy != current.value().strategy) || saved.preservation != current.value().preservation ||
            !sameVolume(saved.sourceVolume, current.value().sourceVolume) ||
            !sameVolume(saved.destinationVolume, current.value().destinationVolume))
            return Result<void>::failure({"plan.volume_changed", "Approved volume identity or movement strategy changed."});
    }
    return Result<void>::success();
}

} // namespace nookbit
