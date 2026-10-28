#pragma once

#include <QAbstractListModel>
#include <QStringList>
#include <QUrl>
#include <QVector>
#include <nookbit/domain/result.hpp>
#include <filesystem>

namespace nookbit {

/// 파일을 옮기지 않고 드롭된 로컬 파일 경로만 보관하는 화면 모델.
class PendingFilesModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString lastMessage READ lastMessage NOTIFY lastMessageChanged)
    Q_PROPERTY(QString storageNotice READ storageNotice NOTIFY storageNoticeChanged)
    Q_PROPERTY(int unavailableCount READ unavailableCount NOTIFY availabilityChanged)

public:
    enum Role {
        FileNameRole = Qt::UserRole + 1,
        FullPathRole,
        AvailableRole,
        DirectoryRole,
    };

    /// 비어 있는 대기 목록을 생성하는 생성자.
    explicit PendingFilesModel(QObject* parent = nullptr);

    /// 화면에 표시할 파일 개수를 반환하는 함수.
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;

    /// 파일 이름이나 전체 경로를 화면에 제공하는 함수.
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;

    /// QML에서 사용할 역할 이름을 반환하는 함수.
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /// 현재 대기 중인 파일 개수를 반환하는 함수.
    [[nodiscard]] int count() const noexcept;

    /// 최근 등록 또는 제외 결과를 반환하는 함수.
    [[nodiscard]] QString lastMessage() const;

    /// 미리보기 요청에 넘길 대기 파일의 전체 경로를 반환하는 함수.
    [[nodiscard]] QStringList filePaths() const;

    /// 저장 상태에 관한 별도 경고를 반환하는 함수.
    [[nodiscard]] QString storageNotice() const;

    /// 현재 존재하지 않거나 파일이 아닌 항목 수를 반환하는 함수.
    [[nodiscard]] int unavailableCount() const;

    /// 저장소를 직접 읽지 않고 전달받은 경로로 초기 목록을 복원하는 함수.
    void restorePaths(const QStringList& paths);

    /// 파일 존재 여부를 다시 확인하고 바뀐 항목을 알리는 함수.
    Q_INVOKABLE void refreshAvailability();

    /// 목록 저장·복원 실패 경고를 일반 등록 메시지와 분리해 표시하는 함수.
    void setStorageNotice(const QString& notice);

    /// 로컬 파일 URL 하나를 원본 변경 없이 대기 목록에 등록하는 함수.
    Q_INVOKABLE void addUrl(const QUrl& url);
    /// 기존 등록 정책으로 경로를 추가하고 확정 경로 또는 거부 원인을 반환하는 함수.
    [[nodiscard]] Result<std::filesystem::path> addPath(const std::filesystem::path& path);

    /// 지정한 위치의 파일을 대기 목록에서만 제외하는 함수.
    Q_INVOKABLE void removeAt(int index);

signals:
    void countChanged();
    void lastMessageChanged();
    void storageNoticeChanged();
    void availabilityChanged();

private:
    struct Entry {
        QString fileName;
        QString fullPath;
        bool available{true};
    };

    /// 화면에 보여줄 결과 문구를 갱신하는 함수.
    void setLastMessage(const QString& message);

    QVector<Entry> entries_;
    QString lastMessage_;
    QString storageNotice_;
};

} // namespace nookbit
