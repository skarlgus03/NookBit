#pragma once
#include <QCoreApplication>

#include <QDateTime>
#include <QString>

namespace nookbit {

// 작업 시각을 사용자 현지 날짜와 월요일 기준 주 단위로 묶는 함수.
inline QString historyDateGroup(const QDateTime& activity, const QDate& today) {
    if (!activity.isValid()) return QCoreApplication::translate("HistoryDateGroup", "날짜 정보 없음");
    const auto date = activity.toLocalTime().date();
    if (date == today) return QCoreApplication::translate("HistoryDateGroup", "오늘");
    if (date == today.addDays(-1)) return QCoreApplication::translate("HistoryDateGroup", "어제");
    if (date > today) return QCoreApplication::translate("HistoryDateGroup", "날짜 확인 필요");
    const auto monday = today.addDays(1 - today.dayOfWeek());
    if (date >= monday) return QCoreApplication::translate("HistoryDateGroup", "이번 주");
    if (date >= monday.addDays(-7)) return QCoreApplication::translate("HistoryDateGroup", "지난주");
    return QCoreApplication::translate("HistoryDateGroup", "이전 기록");
}

} // namespace nookbit
