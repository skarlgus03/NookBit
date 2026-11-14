#pragma once

#include <nookbit/domain/result.hpp>

#include <QCoreApplication>
#include <QDebug>

#include <algorithm>
#include <initializer_list>
#include <string_view>

namespace nookbit {

// 정리·되돌리기 실패 코드를 사용자에게 보여줄 문구로 변환하는 함수.
inline QString fileOperationErrorText(const Error& error) {
    const std::string_view code = error.code;
    // 같은 해결 방법을 가진 오류 코드를 찾는 함수.
    const auto is = [code](std::initializer_list<std::string_view> codes) {
        return std::find(codes.begin(), codes.end(), code) != codes.end();
    };
    if (is({"verify.selection_changed", "path.source_not_selected"}))
        return QCoreApplication::translate("FileOperationError", "대기 목록이 확인한 계획과 달라졌어요. 새 미리보기를 만들어 주세요.");
    if (is({"freeze.rules_changed", "verify.rules_changed", "freeze.settings_changed", "verify.settings_changed", "preview.rule_set_version_mismatch"}))
        return QCoreApplication::translate("FileOperationError", "분류 규칙이나 정리 설정이 바뀌었어요. 새 미리보기를 만들어 주세요.");
    if (is({"sort.confirmation_stale", "sort.saved_plan_changed", "verify.plan_stale", "verify.plan_modified",
            "freeze.preview_stale", "execute.plan_mismatch", "execute.approval_missing", "transfer.zone_contract_required"}))
        return QCoreApplication::translate("FileOperationError", "확인한 이동 계획을 그대로 사용할 수 없어요. 새 미리보기를 만들어 주세요.");
    if (is({"execute.source_changed", "move.source_changed", "transfer.source_changed", "transfer.zone_changed",
            "folder.source_changed", "folder.changed"}))
        return QCoreApplication::translate("FileOperationError", "원본 파일이나 폴더가 바뀌었어요. 현재 위치를 확인하고 새 미리보기를 만들어 주세요.");
    if (is({"move.source_open_failed", "transfer.source_unavailable", "scan.path_unavailable",
            "scan.entry_unavailable", "folder.in_use", "folder.stream_in_use"}))
        return QCoreApplication::translate("FileOperationError", "원본을 열 수 없어요. 파일이 있는지 확인하고 사용 중인 프로그램을 닫아 주세요.");
    if (is({"collision.abort", "move.destination_exists", "folder.destination_exists"}))
        return QCoreApplication::translate("FileOperationError", "목적지에 같은 이름의 항목이 있어요. 목적지나 이름 충돌 설정을 확인해 주세요.");
    if (is({"move.mkdir_failed", "move.parent_unavailable", "move.destination_unavailable", "transfer.parent_create",
            "transfer.parent_unavailable", "transfer.staging_create", "folder.mkdir_failed", "folder.parent_unavailable", "folder.staging_failed"}))
        return QCoreApplication::translate("FileOperationError", "목적지에 파일을 준비할 수 없어요. 드라이브 연결, 폴더 권한, 남은 공간을 확인해 주세요.");
    if (is({"transfer.write", "transfer.flush", "folder.copy_failed", "folder.flush_failed"}))
        return QCoreApplication::translate("FileOperationError", "복사 데이터를 저장하지 못했어요. 목적지 드라이브의 남은 공간과 연결 상태를 확인해 주세요.");
    if (is({"transfer.streams", "folder.stream_unavailable"}))
        return QCoreApplication::translate("FileOperationError", "파일에 붙은 추가 데이터를 안전하게 복사할 수 없어요.");
    if (is({"path.destination_outside_home", "path.home_is_drive_root", "path.app_data_protected",
            "move.unsafe_parent", "folder.unsafe_parent", "rules.unsafe_destination"}))
        return QCoreApplication::translate("FileOperationError", "이 목적지는 사용할 수 없어요. 정리할 위치와 허용한 외부 목적지를 확인해 주세요.");
    if (is({"path.overlapping_selection", "scan.overlapping_selection", "preview.flow_overlap"}))
        return QCoreApplication::translate("FileOperationError", "폴더와 그 안의 항목이 함께 선택됐어요. 대기 목록에서 겹치는 항목을 빼 주세요.");
    if (is({"preview.folder_flow_invalid", "preview.flow_input_invalid"}))
        return QCoreApplication::translate("FileOperationError", "선택한 폴더 정리 방식을 사용할 수 없어요. 대기 목록에서 다시 선택해 주세요.");
    if (is({"preview.flow_scan_incomplete", "scan.enumeration_incomplete", "scan.expansion_incomplete"}))
        return QCoreApplication::translate("FileOperationError", "폴더 내부를 모두 확인하지 못했어요. 읽기 권한과 드라이브 연결을 확인해 주세요.");
    if (is({"plan.volume_changed", "transfer.volume_changed"}))
        return QCoreApplication::translate("FileOperationError", "파일을 옮길 드라이브가 바뀌었어요. 연결 상태를 확인하고 새 미리보기를 만들어 주세요.");
    if (is({"plan.volume_unsupported", "transfer.volume_unsupported", "transfer.unsupported"}))
        return QCoreApplication::translate("FileOperationError", "이 드라이브 사이의 이동은 지원하지 않아요. 다른 목적지를 선택해 주세요.");
    if (is({"rollback.file_changed", "transfer.rollback_source_changed", "transfer.rollback_destination", "transfer.rollback_origin"}))
        return QCoreApplication::translate("FileOperationError", "이동한 항목이 바뀌었거나 원래 위치를 사용할 수 없어요. 작업 기록에서 두 위치를 확인해 주세요.");
    if (code == "execute.partial_failure")
        return QCoreApplication::translate("FileOperationError", "일부 항목만 이동했어요. 작업 기록에서 이동하지 못한 항목을 확인해 주세요.");
    if (code == "execute.batch_rolled_back")
        return QCoreApplication::translate("FileOperationError", "이동 중 문제가 생겨 이번 작업에서 먼저 옮긴 항목을 원래 위치로 돌려놓았어요.");
    if (is({"execute.needs_recovery", "execute.rollback_needs_recovery", "rollback.needs_recovery"}))
        return QCoreApplication::translate("FileOperationError", "작업이 중간에 멈췄어요. ‘중단 작업 확인’에서 파일 상태를 확인해 주세요.");
    if (is({"rollback.no_steps", "rollback.plan_invalid"}))
        return QCoreApplication::translate("FileOperationError", "이 기록에서 되돌릴 수 있는 이동 항목이 없어요.");
    if (is({"execute.no_steps", "freeze.no_steps"}))
        return QCoreApplication::translate("FileOperationError", "이동할 항목이 없어요. 대기 목록과 분류 규칙을 확인해 주세요.");
    if (code == "operation.busy")
        return QCoreApplication::translate("FileOperationError", "다른 파일 작업이 진행 중이에요. 끝난 뒤 다시 시도해 주세요.");
    if (code == "history.migration_missing")
        return QCoreApplication::translate("FileOperationError", "작업 기록을 준비하지 못했어요. 앱의 배포 폴더가 온전히 풀려 있는지 확인해 주세요.");
    if (code == "history.migration_failed")
        return QCoreApplication::translate("FileOperationError", "작업 기록을 새 버전에 맞게 준비하지 못했어요.");
    if (code == "history.schema_too_new")
        return QCoreApplication::translate("FileOperationError", "이 작업 기록을 읽으려면 더 최신 버전의 NookBit이 필요해요.");
    if (code.starts_with("history."))
        return QCoreApplication::translate("FileOperationError", "작업 기록을 읽거나 저장하지 못했어요.");
    if (code.starts_with("rules."))
        return QCoreApplication::translate("FileOperationError", "분류 규칙을 적용하지 못했어요. 분류 규칙 페이지에서 현재 규칙을 확인해 주세요.");
    return QCoreApplication::translate("FileOperationError", "파일 위치와 작업 기록을 확인해 주세요.");
}

// 작업 실패 원문을 진단 로그에 남기고 화면에는 한국어 이유를 반환하는 함수.
inline QString reportFileOperationError(const Error& error) {
    qWarning().noquote() << "NookBit file operation:" << QString::fromStdString(error.code)
                         << QString::fromStdString(error.message);
    return fileOperationErrorText(error);
}

} // namespace nookbit
