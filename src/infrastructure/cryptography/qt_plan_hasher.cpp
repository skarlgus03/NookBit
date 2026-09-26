#include "infrastructure/cryptography/qt_plan_hasher.hpp"

#include <QCryptographicHash>
#include <QFile>

namespace nookbit {

// 입력 바이트의 소문자 16진 SHA-256을 계산하는 함수.
std::string QtPlanHasher::sha256(std::string_view bytes) const {
    const QByteArrayView view(bytes.data(), static_cast<qsizetype>(bytes.size()));
    return QCryptographicHash::hash(view, QCryptographicHash::Sha256).toHex().toStdString();
}

// 파일 전체를 나눠 읽으며 SHA-256을 계산하는 함수.
Result<std::string> QtPlanHasher::sha256File(const std::filesystem::path& path) const {
    QFile file(QString::fromStdWString(path.wstring()));
    if (!file.open(QIODevice::ReadOnly)) {
        return Result<std::string>::failure({"hash.open_failed", "Cannot open file for content hashing."});
    }
    QCryptographicHash digest(QCryptographicHash::Sha256);
    QByteArray buffer(1024 * 1024, Qt::Uninitialized);
    while (true) {
        const auto bytesRead = file.read(buffer.data(), buffer.size());
        if (bytesRead < 0) {
            return Result<std::string>::failure({"hash.read_failed", "Cannot read complete file for hashing."});
        }
        if (bytesRead == 0) break;
        digest.addData(QByteArrayView(buffer.constData(), bytesRead));
    }
    return Result<std::string>::success(digest.result().toHex().toStdString());
}

} // namespace nookbit
