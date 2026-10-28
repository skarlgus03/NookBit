#include "infrastructure/cryptography/qt_plan_hasher.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <array>
#include <memory>
#include "infrastructure/filesystem/directory_entries.hpp"
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
    std::error_code typeError;
    if (std::filesystem::is_directory(path, typeError)) {
        const auto entries = directoryEntries(path);
        if (!entries) return Result<std::string>::failure(entries.error());
        QCryptographicHash tree(QCryptographicHash::Sha256);
        tree.addData(QByteArrayView("nookbit-directory-v1"));
        // 필드 길이를 함께 기록해 파일 이름과 내용의 경계가 모호하지 않게 하는 함수.
        const auto append = [&](const QByteArray& field) {
            tree.addData(QByteArray::number(field.size()) + ':' + field);
        };
        for (const auto& entry : entries.value()) {
            const auto name = entry.relative.generic_u8string();
            append(QByteArray(reinterpret_cast<const char*>(name.data()), qsizetype(name.size())));
            append(entry.directory ? QByteArray("directory") : QByteArray("file"));
            if (!entry.directory) {
                const auto content = sha256File(path / entry.relative);
                if (!content) return content;
                append(QByteArray::fromStdString(content.value()));
            }
#ifdef _WIN32
            // 기본 스트림 외의 데이터도 승인·복사 검증에 포함하는 함수.
            WIN32_FIND_STREAM_DATA stream{};
            const auto streamHandle = FindFirstStreamW((path / entry.relative).c_str(), FindStreamInfoStandard, &stream, 0);
            if (streamHandle == INVALID_HANDLE_VALUE && GetLastError() != ERROR_HANDLE_EOF)
                return Result<std::string>::failure({"folder.stream_unavailable", "Cannot inspect folder entry streams."});
            std::vector<std::wstring> streams;
            if (streamHandle != INVALID_HANDLE_VALUE) {
                do { if (std::wstring_view(stream.cStreamName) != L"::$DATA") streams.emplace_back(stream.cStreamName); }
                while (FindNextStreamW(streamHandle, &stream));
                const auto last = GetLastError(); FindClose(streamHandle);
                if (last != ERROR_HANDLE_EOF) return Result<std::string>::failure({"folder.stream_unavailable", "Cannot list complete streams."});
            }
            if (entry.directory && !streams.empty())
                return Result<std::string>::failure({"folder.directory_stream_unsupported", "Directories with named streams are not supported for cross-volume moves."});
            std::sort(streams.begin(), streams.end());
            append(QByteArray::number(streams.size()));
            for (const auto& streamName : streams) {
                append(QString::fromStdWString(streamName).toUtf8());
                const auto content = sha256File(std::filesystem::path((path / entry.relative).wstring() + streamName));
                if (!content) return content;
                append(QByteArray::fromStdString(content.value()));
            }
#endif
        }
        // 해시를 읽는 사이 항목이 추가·삭제되었으면 그 결과를 승인에 쓰지 않는다.
        const auto after = directoryEntries(path);
        if (!after || after.value().size() != entries.value().size() ||
            !std::equal(entries.value().begin(), entries.value().end(), after.value().begin(),
                [](const auto& a, const auto& b) { return a.relative == b.relative && a.directory == b.directory; }))
            return Result<std::string>::failure({"folder.changed", "Folder entries changed during inspection."});
        return Result<std::string>::success(tree.result().toHex().toStdString());
    }
#ifdef _WIN32
    const auto close = [](void* handle) { CloseHandle(handle); };
    std::unique_ptr<void, decltype(close)> file(CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr), close);
    if (file.get() == INVALID_HANDLE_VALUE) {
        file.release();
        return Result<std::string>::failure({"hash.open_failed", "Cannot open file for content hashing."});
    }
#else
    QFile file(QString::fromStdWString(path.wstring()));
    if (!file.open(QIODevice::ReadOnly)) {
        return Result<std::string>::failure({"hash.open_failed", "Cannot open file for content hashing."});
    }
#endif
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
#ifdef _WIN32
        DWORD bytesRead{};
        if (!ReadFile(file.get(), buffer.data(), DWORD(buffer.size()), &bytesRead, nullptr))
            return Result<std::string>::failure({"hash.read_failed", "Cannot read complete file for hashing."});
#else
        const auto bytesRead = file.read(buffer.data(), buffer.size());
        if (bytesRead < 0) {
            return Result<std::string>::failure({"hash.read_failed", "Cannot read complete file for hashing."});
        }
#endif
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
