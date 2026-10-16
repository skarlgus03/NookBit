#pragma once

#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <memory>

namespace nookbit {
/// 앱 데이터와 설정의 수명을 소유하며 UI 검사는 매 실행 별도 임시 공간을 사용한다.
class ApplicationStorage final {
public:
    /// 사용자 실행과 자동 검사의 저장 위치를 한곳에서 결정하는 생성자.
    explicit ApplicationStorage(bool isolated) {
        if (isolated) {
            temporary_ = std::make_unique<QTemporaryDir>(
                QDir::tempPath() + QStringLiteral("/NookBit-ui-XXXXXX"));
            if (!temporary_->isValid()) return;
            data_ = QDir(temporary_->path()).filePath(QStringLiteral("data"));
            documents_ = QDir(temporary_->path()).filePath(QStringLiteral("documents"));
            downloads_ = QDir(temporary_->path()).filePath(QStringLiteral("downloads"));
            valid_ = QDir().mkpath(data_) && QDir().mkpath(documents_) && QDir().mkpath(downloads_);
            settings_ = std::make_unique<QSettings>(
                QDir(temporary_->path()).filePath(QStringLiteral("ui.ini")), QSettings::IniFormat);
        } else {
            data_ = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
            documents_ = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
            downloads_ = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
            settings_ = std::make_unique<QSettings>();
            valid_ = !data_.isEmpty();
        }
    }
    /// 임시 공간 생성 실패 시 사용자 저장소로 대체하지 않고 시작을 중단하게 하는 함수.
    bool valid() const { return valid_; }
    /// 현재 실행이 사용할 앱 데이터 디렉터리를 반환하는 함수.
    const QString& dataDirectory() const { return data_; }
    /// 현재 실행의 기본 문서 디렉터리를 반환하는 함수.
    const QString& documentsDirectory() const { return documents_; }
    /// 현재 실행의 기본 다운로드 디렉터리를 반환하는 함수.
    const QString& downloadsDirectory() const { return downloads_; }
    /// 현재 실행에 속한 설정 저장소만 반환하는 함수.
    QSettings& settings() const { return *settings_; }

private:
    std::unique_ptr<QTemporaryDir> temporary_;
    QString data_, documents_, downloads_;
    std::unique_ptr<QSettings> settings_;
    bool valid_{false};
};
}
