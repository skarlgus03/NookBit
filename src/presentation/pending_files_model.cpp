#include "presentation/pending_files_model.hpp"

#include <QDir>
#include <QFileInfo>
#include <utility>

namespace nookbit {

// 비어 있는 대기 목록을 생성하는 생성자.
PendingFilesModel::PendingFilesModel(QObject* parent) : QAbstractListModel(parent) {}

// 화면에 표시할 파일 개수를 반환하는 함수.
int PendingFilesModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : entries_.size();
}

// 파일 이름이나 전체 경로를 화면에 제공하는 함수.
QVariant PendingFilesModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= entries_.size()) return {};
    const auto& entry = entries_.at(index.row());
    if (role == FileNameRole) return entry.fileName;
    if (role == FullPathRole) return entry.fullPath;
    if (role == AvailableRole) return entry.available;
    if (role == DirectoryRole) return QFileInfo(entry.fullPath).isDir();
    return {};
}

// QML에서 사용할 역할 이름을 반환하는 함수.
QHash<int, QByteArray> PendingFilesModel::roleNames() const {
    return {{FileNameRole, "fileName"}, {FullPathRole, "fullPath"}, {AvailableRole, "available"}, {DirectoryRole, "directory"}};
}

// 현재 대기 중인 파일 개수를 반환하는 함수.
int PendingFilesModel::count() const noexcept {
    return entries_.size();
}

// 최근 등록 또는 제외 결과를 반환하는 함수.
QString PendingFilesModel::lastMessage() const {
    return lastMessage_;
}

// 미리보기 요청에 넘길 대기 파일의 전체 경로를 반환하는 함수.
QStringList PendingFilesModel::filePaths() const {
    QStringList paths;
    paths.reserve(entries_.size());
    for (const auto& entry : entries_) paths.append(entry.fullPath);
    return paths;
}

// 저장 상태에 관한 별도 경고를 반환하는 함수.
QString PendingFilesModel::storageNotice() const { return storageNotice_; }

// 현재 존재하지 않거나 파일이 아닌 항목 수를 반환하는 함수.
int PendingFilesModel::unavailableCount() const {
    int count = 0;
    for (const auto& entry : entries_) if (!entry.available) ++count;
    return count;
}

// 저장소를 직접 읽지 않고 전달받은 경로로 초기 목록을 복원하는 함수.
void PendingFilesModel::restorePaths(const QStringList& paths) {
    QVector<Entry> restored;
    for (const auto& path : paths) {
        const QFileInfo info(path);
        if (path.isEmpty() || path.contains(QChar::Null) || !info.isAbsolute() || QDir(path).isRoot()) continue;
        const auto fullPath = QDir::cleanPath(info.absoluteFilePath());
        bool duplicate = false;
        for (const auto& entry : restored) {
            if (QString::compare(entry.fullPath, fullPath, Qt::CaseInsensitive) == 0) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) restored.push_back({info.fileName(), fullPath, info.exists() && (info.isFile() || info.isDir()) && !info.isSymLink()});
    }
    beginResetModel();
    entries_ = std::move(restored);
    endResetModel();
    emit countChanged();
    emit availabilityChanged();
    if (!entries_.isEmpty()) setLastMessage(QStringLiteral("대기 파일 %1개를 복원했어요. 정리하려면 새 미리보기를 만들어 주세요.").arg(count()));
}

// 파일 존재 여부를 다시 확인하고 바뀐 항목을 알리는 함수.
void PendingFilesModel::refreshAvailability() {
    bool changed = false;
    for (int row = 0; row < entries_.size(); ++row) {
        auto& entry = entries_[row];
        const QFileInfo info(entry.fullPath);
        const bool available = info.exists() && (info.isFile() || info.isDir()) && !info.isSymLink();
        if (entry.available == available) continue;
        entry.available = available;
        changed = true;
        emit dataChanged(index(row), index(row), {AvailableRole});
    }
    if (changed) emit availabilityChanged();
}

// 목록 저장·복원 실패 경고를 일반 등록 메시지와 분리해 표시하는 함수.
void PendingFilesModel::setStorageNotice(const QString& notice) {
    if (storageNotice_ == notice) return;
    storageNotice_ = notice;
    emit storageNoticeChanged();
}

// 로컬 파일 URL 하나를 원본 변경 없이 대기 목록에 등록하는 함수.
void PendingFilesModel::addUrl(const QUrl& url) {
    if (!url.isLocalFile()) {
        setLastMessage(QStringLiteral("컴퓨터에 있는 파일만 추가할 수 있어요."));
        return;
    }
    const QFileInfo info(url.toLocalFile());
    if (!info.exists() || (!info.isFile() && !info.isDir()) || info.isSymLink() || QDir(info.absoluteFilePath()).isRoot()) {
        setLastMessage(QStringLiteral("존재하는 파일이나 폴더만 추가할 수 있어요. 바로가기와 드라이브 전체는 제외해요."));
        return;
    }
    const auto fullPath = QDir::cleanPath(info.absoluteFilePath());
    for (const auto& entry : entries_) {
        if (QString::compare(entry.fullPath, fullPath, Qt::CaseInsensitive) == 0) {
            setLastMessage(QStringLiteral("이미 대기 목록에 있는 파일이에요."));
            return;
        }
        if (fullPath.startsWith(entry.fullPath + "/", Qt::CaseInsensitive) ||
            entry.fullPath.startsWith(fullPath + "/", Qt::CaseInsensitive)) {
            setLastMessage(QStringLiteral("폴더와 그 안의 항목은 함께 등록할 수 없어요. 폴더 전체 또는 내부 항목 중 하나를 선택해 주세요."));
            return;
        }
    }
    const int newRow = entries_.size();
    beginInsertRows({}, newRow, newRow);
    entries_.push_back({info.fileName(), fullPath});
    endInsertRows();
    emit countChanged();
    setLastMessage(info.isDir() ? QStringLiteral("폴더 전체를 하나의 대상으로 추가했어요. 내부 항목은 따로 분류하지 않아요.")
                              : QStringLiteral("파일을 대기 목록에 추가했어요. 원본은 그대로 있어요."));
}

// 지정한 위치의 파일을 대기 목록에서만 제외하는 함수.
void PendingFilesModel::removeAt(int index) {
    if (index < 0 || index >= entries_.size()) return;
    beginRemoveRows({}, index, index);
    entries_.removeAt(index);
    endRemoveRows();
    emit countChanged();
    emit availabilityChanged();
    setLastMessage(QStringLiteral("대기 목록에서만 뺐어요. 원본 파일은 그대로 있어요."));
}

// 화면에 보여줄 결과 문구를 갱신하는 함수.
void PendingFilesModel::setLastMessage(const QString& message) {
    if (lastMessage_ == message) return;
    lastMessage_ = message;
    emit lastMessageChanged();
}

} // namespace nookbit
