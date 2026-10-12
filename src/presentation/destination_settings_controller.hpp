#pragma once
#include <QObject>
#include <QSettings>
#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <QStringList>

namespace nookbit {
/// 규칙과 별개로 기본 경로 표시와 외부 목적지 허용을 관리하는 설정 창구.
class DestinationSettingsController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString homeRoot READ homeRoot NOTIFY homeRootChanged)
    Q_PROPERTY(QStringList allowedRoots READ allowedRoots NOTIFY allowedRootsChanged)
public:
    /// 기본 정리 폴더와 저장소를 받으며 자동 검사에서는 개인 설정을 읽지 않는 생성자.
    DestinationSettingsController(QSettings& settings, QString home, bool persist, QObject* parent = nullptr)
        : QObject(parent), settings_(settings), home_(std::move(home)), persist_(persist) {
        if (persist_) {
            for (const auto& root : settings_.value(QStringLiteral("destinations/allowedRoots")).toStringList())
                if (validFolder(root) && !roots_.contains(QDir::cleanPath(root))) roots_.append(QDir::cleanPath(root));
        }
    }
    /// 현재 기본 정리 폴더를 반환하는 함수.
    QString homeRoot() const { return home_; }
    /// 사용자가 직접 허용한 외부 폴더를 반환하는 함수.
    QStringList allowedRoots() const { return roots_; }
    /// 기본 정리 폴더 변경을 편집기의 실제 경로 표시에도 반영하는 함수.
    void setHomeRoot(const QString& root) { home_ = root; emit homeRootChanged(); }
    /// 폴더 선택 URL을 일반 파일 시스템 경로로 바꾸는 함수.
    Q_INVOKABLE QString folderPath(const QUrl& url) const { return QDir::cleanPath(url.toLocalFile()); }
    /// 홈 내부의 선택 경로를 상대 경로로 바꾸며 외부이면 빈 문자열을 반환하는 함수.
    Q_INVOKABLE QString relativeToHome(const QString& path) const {
        const auto relative = QDir(home_).relativeFilePath(path);
        if (QDir::isAbsolutePath(relative) || relative == ".." || relative.startsWith("../")) return {};
        return relative;
    }
    /// 경로 입력값을 실제로 사용할 전체 경로 표시로 변환하는 함수.
    Q_INVOKABLE QString resolvedPath(const QString& path, bool homeRelative) const {
        if (path.trimmed().isEmpty()) return {};
        return QDir::toNativeSeparators(QDir::cleanPath(homeRelative ? QDir(home_).filePath(path.trimmed()) : path.trimmed()));
    }
    /// 명시적인 사용자 확인 이후 외부 목적지 폴더를 허용하고 저장하는 함수.
    Q_INVOKABLE bool allowFolder(const QString& path) {
        const auto clean = QDir::cleanPath(path.trimmed());
        if (!validFolder(clean)) return false;
        if (!roots_.contains(clean)) {
            roots_.append(clean);
            if (persist_) { settings_.setValue(QStringLiteral("destinations/allowedRoots"), roots_); settings_.sync(); }
            emit allowedRootsChanged();
        }
        return true;
    }
    /// 허용 목록에서 지정 폴더를 제거하고 저장하는 함수.
    Q_INVOKABLE void revokeFolder(const QString& path) {
        if (!roots_.removeAll(path)) return;
        if (persist_) { settings_.setValue(QStringLiteral("destinations/allowedRoots"), roots_); settings_.sync(); }
        emit allowedRootsChanged();
    }
signals:
    void homeRootChanged();
    void allowedRootsChanged();
private:
    /// 드라이브 루트·상위 이동·변수 등을 허용 폴더로 등록하지 못하게 검사하는 함수.
    static bool validFolder(const QString& path) {
        const QFileInfo info(path);
        return info.isAbsolute() && !QDir(path).isRoot() && !path.contains('{') && !path.contains('}') &&
               !path.split('/').contains("..") && !path.split('\\').contains("..");
    }
    QSettings& settings_;
    QString home_;
    QStringList roots_;
    bool persist_;
};
}
