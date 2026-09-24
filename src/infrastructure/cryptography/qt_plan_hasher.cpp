#include "infrastructure/cryptography/qt_plan_hasher.hpp"

#include <QCryptographicHash>

namespace nookbit {

// 입력 바이트의 소문자 16진 SHA-256을 계산하는 함수.
std::string QtPlanHasher::sha256(std::string_view bytes) const {
    const QByteArrayView view(bytes.data(), static_cast<qsizetype>(bytes.size()));
    return QCryptographicHash::hash(view, QCryptographicHash::Sha256).toHex().toStdString();
}

} // namespace nookbit
