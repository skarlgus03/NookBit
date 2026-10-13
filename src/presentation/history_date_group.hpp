#pragma once

#include <QDateTime>
#include <QString>

namespace nookbit {

// 작업 시각을 사용자 현지 날짜와 월요일 기준 주 단위로 묶는 함수.
inline QString historyDateGroup(const QDateTime& activity, const QDate& today) {
    if (!activity.isValid()) return QStringLiteral("날짜 정보 없음");
    const auto date = activity.toLocalTime().date();
    if (date == today) return QStringLiteral("오늘");
    if (date == today.addDays(-1)) return QStringLiteral("어제");
    if (date > today) return QStringLiteral("날짜 확인 필요");
    const auto monday = today.addDays(1 - today.dayOfWeek());
    if (date >= monday) return QStringLiteral("이번 주");
    if (date >= monday.addDays(-7)) return QStringLiteral("지난주");
    return QStringLiteral("이전 기록");
}

} // namespace nookbit
