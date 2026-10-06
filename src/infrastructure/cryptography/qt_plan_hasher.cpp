#include "infrastructure/cryptography/qt_plan_hasher.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <array>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include <bcrypt.h>
#endif

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
#ifdef _WIN32
    struct NativeHash {
        BCRYPT_ALG_HANDLE algorithm{};
        BCRYPT_HASH_HANDLE hash{};
        /// OS 해시 핸들을 역순으로 해제하는 함수.
        ~NativeHash() { if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); }
    } digest;
    if (BCryptOpenAlgorithmProvider(&digest.algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
        BCryptCreateHash(digest.algorithm, &digest.hash, nullptr, 0, nullptr, 0, 0) < 0)
        return Result<std::string>::failure({"hash.initialize_failed", "Cannot initialize Windows SHA-256."});
#else
    QCryptographicHash digest(QCryptographicHash::Sha256);
#endif
    QByteArray buffer(1024 * 1024, Qt::Uninitialized);
    while (true) {
        const auto bytesRead = file.read(buffer.data(), buffer.size());
        if (bytesRead < 0) {
            return Result<std::string>::failure({"hash.read_failed", "Cannot read complete file for hashing."});
        }
        if (bytesRead == 0) break;
#ifdef _WIN32
        if (BCryptHashData(digest.hash, reinterpret_cast<PUCHAR>(buffer.data()), static_cast<ULONG>(bytesRead), 0) < 0)
            return Result<std::string>::failure({"hash.update_failed", "Cannot hash file data."});
#else
        digest.addData(QByteArrayView(buffer.constData(), bytesRead));
#endif
    }
#ifdef _WIN32
    std::array<unsigned char, 32> bytes{};
    if (BCryptFinishHash(digest.hash, bytes.data(), static_cast<ULONG>(bytes.size()), 0) < 0)
        return Result<std::string>::failure({"hash.finish_failed", "Cannot finish file SHA-256."});
    return Result<std::string>::success(QByteArray(reinterpret_cast<const char*>(bytes.data()), bytes.size()).toHex().toStdString());
#else
    return Result<std::string>::success(digest.result().toHex().toStdString());
#endif
}

} // namespace nookbit
