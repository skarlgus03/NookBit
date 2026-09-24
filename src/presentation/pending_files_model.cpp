#include "presentation/pending_files_model.hpp"

#include <QDir>
#include <QFileInfo>

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
    return {};
}

// QML에서 사용할 역할 이름을 반환하는 함수.
QHash<int, QByteArray> PendingFilesModel::roleNames() const {
    return {{FileNameRole, "fileName"}, {FullPathRole, "fullPath"}};
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

// 로컬 파일 URL 하나를 원본 변경 없이 대기 목록에 등록하는 함수.
void PendingFilesModel::addUrl(const QUrl& url) {
    if (!url.isLocalFile()) {
        setLastMessage(QStringLiteral("컴퓨터에 있는 파일만 추가할 수 있어요."));
        return;
    }
    const QFileInfo info(url.toLocalFile());
    if (!info.exists() || !info.isFile()) {
        setLastMessage(QStringLiteral("폴더나 존재하지 않는 파일은 추가할 수 없어요."));
        return;
    }
    const auto fullPath = QDir::cleanPath(info.absoluteFilePath());
    for (const auto& entry : entries_) {
        if (QString::compare(entry.fullPath, fullPath, Qt::CaseInsensitive) == 0) {
            setLastMessage(QStringLiteral("이미 대기 목록에 있는 파일이에요."));
            return;
        }
    }
    const int newRow = entries_.size();
    beginInsertRows({}, newRow, newRow);
    entries_.push_back({info.fileName(), fullPath});
    endInsertRows();
    emit countChanged();
    setLastMessage(QStringLiteral("파일을 대기 목록에 추가했어요. 원본은 그대로 있어요."));
}

// 지정한 위치의 파일을 대기 목록에서만 제외하는 함수.
void PendingFilesModel::removeAt(int index) {
    if (index < 0 || index >= entries_.size()) return;
    beginRemoveRows({}, index, index);
    entries_.removeAt(index);
    endRemoveRows();
    emit countChanged();
    setLastMessage(QStringLiteral("대기 목록에서만 뺐어요. 원본 파일은 그대로 있어요."));
}

// 화면에 보여줄 결과 문구를 갱신하는 함수.
void PendingFilesModel::setLastMessage(const QString& message) {
    if (lastMessage_ == message) return;
    lastMessage_ = message;
    emit lastMessageChanged();
}

} // namespace nookbit
