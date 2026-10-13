#pragma once
#include <QObject>
#include <QFile>
#include <QSaveFile>
#include <QUrl>
#include <QDir>
#include <QStandardPaths>

namespace nookbit {
/// 규칙 AI 작성 계약과 캐릭터 제작 규격을 내장 리소스에서 내보내는 표시 창구.
class UserGuideController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
public:
    /// 문서 창구의 부모 객체를 받는 생성자.
    explicit UserGuideController(QObject* parent = nullptr) : QObject(parent) {}
    /// 최근 문서 저장 오류를 반환하는 함수.
    QString error() const { return error_; }
    /// 저장 창에 제안할 문서 이름과 기본 폴더를 반환하는 함수.
    Q_INVOKABLE QUrl suggestedFile(const QString& kind) const {
        const auto name = kind == "rules" ? QStringLiteral("NookBit-Rules-AI.md") : QStringLiteral("NookBit-CharacterPack-Guide.md");
        return QUrl::fromLocalFile(QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath(name));
    }
    /// 지정한 사용자 가이드의 Markdown 본문을 반환하는 함수.
    Q_INVOKABLE QString text(const QString& kind) const {
        const auto path = resourcePath(kind);
        if (path.isEmpty()) return {};
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
    }
    /// 사용자가 저장 창에서 지정한 로컬 파일에 문서를 안전하게 저장하는 함수.
    Q_INVOKABLE bool save(const QString& kind, const QUrl& destination) {
        const auto content = text(kind).toUtf8();
        QSaveFile file(destination.toLocalFile());
        if (content.isEmpty() || !destination.isLocalFile() || !file.open(QIODevice::WriteOnly) ||
            file.write(content) != content.size() || !file.commit()) {
            error_ = QStringLiteral("문서를 저장하지 못했어요. 저장할 위치와 권한을 확인해 주세요.");
            emit errorChanged();
            return false;
        }
        clearError();
        return true;
    }
    /// 이전 저장 오류를 새 문서 창에 표시하지 않도록 지우는 함수.
    Q_INVOKABLE void clearError() { error_.clear(); emit errorChanged(); }
signals:
    void errorChanged();
private:
    /// 임의 파일 접근 없이 두 사용자 가이드만 읽도록 리소스 경로를 고르는 함수.
    static QString resourcePath(const QString& kind) {
        if (kind == "rules") return QStringLiteral(":/nookbit/guides/rules-ai.md");
        if (kind == "character") return QStringLiteral(":/nookbit/guides/character.ko.md");
        return {};
    }
    QString error_;
};
}
