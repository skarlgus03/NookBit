#pragma once
#include <QObject>
#include <QSettings>
#include <QSize>
#include <QHash>

namespace nookbit {
/// 테마·페이지별 창 크기만 관리하며 작업 모듈을 알지 않는 표시 설정.
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
    /// 페이지의 마지막 크기를 반환하며 없거나 잘못된 저장값은 기본값을 사용하는 함수.
    Q_INVOKABLE QSize extensionSize(int page, QSize fallback) const {
        const auto key = pageKey(page);
        if (key.isEmpty()) return fallback;
        if (sizes_.contains(page)) return sizes_.value(page);
        const auto saved = persist_ ? settings_.value(key).value<QSize>() : QSize();
        return validSize(saved) ? saved : fallback;
    }
    /// 현재 페이지 크기만 기억하고 일반 실행에서 개인 설정에 저장하는 함수.
    Q_INVOKABLE void rememberExtensionSize(int page, int width, int height) {
        const auto key = pageKey(page);
        const QSize size(width, height);
        if (key.isEmpty() || !validSize(size)) return;
        sizes_.insert(page, size);
        if (persist_) { settings_.setValue(key, size); settings_.sync(); }
    }
signals:
    void darkModeChanged();
private:
    /// 페이지 순서가 아닌 안정적인 이름으로 설정 키를 만드는 함수.
    static QString pageKey(int page) {
        switch (page) {
        case 1: return QStringLiteral("windows/ruleEditor/size");
        case 3: return QStringLiteral("windows/watchSettings/size");
        case 4: return QStringLiteral("windows/history/size");
        case 5: return QStringLiteral("windows/characterSettings/size");
        case 6: return QStringLiteral("windows/collisionSettings/size");
        case 7: return QStringLiteral("windows/fileSort/size");
        case 8: return QStringLiteral("windows/settings/size");
        case 9: return QStringLiteral("windows/developerTools/size");
        case 10: return QStringLiteral("windows/recovery/size");
        case 11: return QStringLiteral("windows/notifications/size");
        default: return {};
        }
    }
    /// 손상된 크기나 과도한 크기가 창 생성에 사용되지 않도록 검사하는 함수.
    static bool validSize(QSize size) {
        return size.width() > 0 && size.height() > 0 && size.width() <= 16384 && size.height() <= 16384;
    }
    QSettings& settings_;
    QHash<int, QSize> sizes_;
    bool persist_, dark_;
};
}
