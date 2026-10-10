#pragma once
#include <QObject>
#include <QSettings>

namespace nookbit {
/// 테마만 관리하며 파일 정리나 캐릭터 상태를 알지 않는 표시 설정.
class UiSettingsController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool darkMode READ darkMode WRITE setDarkMode NOTIFY darkModeChanged)
public:
    /// 저장소와 자동 검사 시 저장 금지 여부를 받는 생성자.
    UiSettingsController(QSettings& settings, bool persist, QObject* parent = nullptr)
        : QObject(parent), settings_(settings), persist_(persist),
          dark_(persist && settings.value(QStringLiteral("appearance/darkMode"), false).toBool()) {}
    /// 앱 전체의 현재 테마를 반환하는 함수.
    bool darkMode() const { return dark_; }
    /// 테마를 변경하고 일반 실행에서만 개인 설정에 저장하는 함수.
    void setDarkMode(bool value) {
        if (dark_ == value) return;
        dark_ = value;
        if (persist_) { settings_.setValue(QStringLiteral("appearance/darkMode"), value); settings_.sync(); }
        emit darkModeChanged();
    }
signals:
    void darkModeChanged();
private:
    QSettings& settings_;
    bool persist_, dark_;
};
}
