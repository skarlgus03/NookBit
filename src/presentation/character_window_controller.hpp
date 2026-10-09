#pragma once
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QList>
#include <algorithm>
#include <limits>

namespace nookbit {
/// 독립 표시 창의 위치·노출만 관리하며 캐릭터 상태나 파일 작업을 알지 않는다.
class CharacterWindowController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool shown READ shown WRITE setShown NOTIFY visibilityChanged)
    Q_PROPERTY(int anchorX READ anchorX NOTIFY positionChanged)
    Q_PROPERTY(int anchorY READ anchorY NOTIFY positionChanged)
public:
    /// 화면 배치 범위와 초기 위치를 주입하는 생성자.
    CharacterWindowController(QList<QRect> screens, QPoint anchor, bool shown, QObject* parent = nullptr)
        : QObject(parent), screens_(std::move(screens)), shown_(shown) { moveAnchor(anchor.x(), anchor.y()); }
    /// 독립 창 표시 여부를 반환하는 함수.
    bool shown() const { return shown_; }
    /// 화면 기준 피벗의 가로 좌표를 반환하는 함수.
    int anchorX() const { return anchor_.x(); }
    /// 화면 기준 피벗의 세로 좌표를 반환하는 함수.
    int anchorY() const { return anchor_.y(); }
    /// 앱 종료 대신 표시 여부만 변경하는 함수.
    Q_INVOKABLE void setShown(bool value) {
        if (shown_ == value) return;
        shown_ = value; emit visibilityChanged();
    }
    /// 가장 가까운 사용 가능 화면 안으로 위치를 제한하는 함수.
    Q_INVOKABLE void moveAnchor(int x, int y) {
        QPoint best(x, y);
        double distance = std::numeric_limits<double>::max();
        for (const auto& screen : screens_) {
            if (!screen.isValid()) continue;
            const auto area = screen.width() > 64 && screen.height() > 64 ? screen.adjusted(32, 32, -32, -32) : screen;
            const QPoint candidate(std::clamp(x, area.left(), area.right()), std::clamp(y, area.top(), area.bottom()));
            const double dx = static_cast<double>(candidate.x()) - x, dy = static_cast<double>(candidate.y()) - y;
            if (dx * dx + dy * dy < distance) { best = candidate; distance = dx * dx + dy * dy; }
        }
        if (best == anchor_) return;
        anchor_ = best; emit positionChanged();
    }
    /// 모니터 변경 시 현재 위치를 새 배치 안으로 복원하는 함수.
    void setScreens(QList<QRect> screens) {
        screens_ = std::move(screens); moveAnchor(anchor_.x(), anchor_.y());
    }
    /// 기본 화면 오른쪽 아래로 캐릭터를 가져오는 함수.
    Q_INVOKABLE void resetPosition() {
        if (screens_.isEmpty()) return;
        moveAnchor(screens_.first().right() - 180, screens_.first().bottom() - 140);
    }
signals:
    void visibilityChanged();
    void positionChanged();
private:
    QList<QRect> screens_;
    QPoint anchor_;
    bool shown_{true};
};
}
