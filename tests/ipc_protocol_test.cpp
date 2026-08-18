#include "ipc/ipc_endpoint.h"
#include "ipc/ipc_protocol.h"

#include <QCoreApplication>

#include <iostream>

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);

    const QByteArray first =
        modelharbor::ipc::encodeRequest(QStringLiteral("1"), QStringLiteral("ping"));
    const QByteArray second =
        modelharbor::ipc::encodeRequest(QStringLiteral("2"), QStringLiteral("get_status"));
    QByteArray buffer = first.left(2);
    QByteArray payload;
    QString error;
    if (modelharbor::ipc::takeFrame(&buffer, &payload, &error) !=
        modelharbor::ipc::FrameStatus::NeedMoreData) {
        std::cerr << "partial frame was accepted\n";
        return 1;
    }

    buffer.append(first.mid(2));
    buffer.append(second);
    if (modelharbor::ipc::takeFrame(&buffer, &payload, &error) !=
            modelharbor::ipc::FrameStatus::Ready ||
        modelharbor::ipc::decodeMessage(payload).object.value(QStringLiteral("id")).toString() !=
            QStringLiteral("1")) {
        std::cerr << "first coalesced frame failed\n";
        return 2;
    }
    if (modelharbor::ipc::takeFrame(&buffer, &payload, &error) !=
            modelharbor::ipc::FrameStatus::Ready ||
        modelharbor::ipc::decodeMessage(payload).object.value(QStringLiteral("id")).toString() !=
            QStringLiteral("2") ||
        !buffer.isEmpty()) {
        std::cerr << "second coalesced frame failed\n";
        return 3;
    }

    const quint32 oversized = static_cast<quint32>(modelharbor::ipc::kMaximumFramePayload + 1);
    buffer.clear();
    buffer.append(static_cast<char>((oversized >> 24U) & 0xffU));
    buffer.append(static_cast<char>((oversized >> 16U) & 0xffU));
    buffer.append(static_cast<char>((oversized >> 8U) & 0xffU));
    buffer.append(static_cast<char>(oversized & 0xffU));
    if (modelharbor::ipc::takeFrame(&buffer, &payload, &error) !=
            modelharbor::ipc::FrameStatus::Invalid ||
        error != QStringLiteral("frame_too_large")) {
        std::cerr << "oversized frame was not rejected\n";
        return 4;
    }

    const QString socketName = modelharbor::ipc::currentUserSocketName();
    if (!socketName.startsWith(QStringLiteral("modelharbor-gateway-")) ||
        socketName != modelharbor::ipc::currentUserSocketName() ||
        socketName.contains(QStringLiteral("S-1-"), Qt::CaseInsensitive)) {
        std::cerr << "current-user socket name is not stable and opaque\n";
        return 5;
    }

    std::cout << "ModelHarbor IPC protocol OK\n";
    return 0;
}
