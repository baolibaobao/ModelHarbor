#include "ipc/ipc_endpoint.h"

#include <QCryptographicHash>

#ifdef Q_OS_WIN
#include <Windows.h>
#include <sddl.h>
#endif

namespace modelharbor::ipc {

namespace {

QByteArray currentUserIdentity() {
#ifdef Q_OS_WIN
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return {};
    }

    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    QByteArray storage(static_cast<qsizetype>(size), Qt::Uninitialized);
    if (size == 0 || !GetTokenInformation(token, TokenUser, storage.data(), size, &size)) {
        CloseHandle(token);
        return {};
    }

    const auto* tokenUser = reinterpret_cast<const TOKEN_USER*>(storage.constData());
    LPWSTR sidText = nullptr;
    if (!ConvertSidToStringSidW(tokenUser->User.Sid, &sidText)) {
        CloseHandle(token);
        return {};
    }

    const QString sid = QString::fromWCharArray(sidText);
    LocalFree(sidText);
    CloseHandle(token);
    return sid.toUtf8();
#else
    return qgetenv("USER");
#endif
}

} // namespace

QString currentUserSocketName() {
    QByteArray identity = currentUserIdentity();
    if (identity.isEmpty()) {
        identity = QByteArrayLiteral("current-user");
    }
    const QByteArray digest =
        QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex().left(24);
    return QStringLiteral("modelharbor-gateway-%1").arg(QString::fromLatin1(digest));
}

} // namespace modelharbor::ipc
