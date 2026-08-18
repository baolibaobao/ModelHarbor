#include "core/version.h"
#include "ipc/ipc_endpoint.h"
#include "ipc/ipc_protocol.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTextStream>
#include <QTimer>

#include <memory>

namespace {

QString optionValue(const QStringList& arguments, const QString& option, const QString& fallback) {
    const qsizetype index = arguments.indexOf(option);
    if (index >= 0 && index + 1 < arguments.size()) {
        return arguments.at(index + 1);
    }
    return fallback;
}

void sendError(QLocalSocket* socket, const QString& id, const QString& code,
               const QString& message) {
    socket->write(modelharbor::ipc::encodeResponse(
        id, false, {}, {{QStringLiteral("code"), code}, {QStringLiteral("message"), message}}));
    socket->flush();
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    const QString socketName = optionValue(application.arguments(), QStringLiteral("--ipc-name"),
                                           modelharbor::ipc::currentUserSocketName());

    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!server.listen(socketName)) {
        QTextStream(stderr) << "gateway listen failed: " << server.errorString() << '\n';
        return 2;
    }

    QObject::connect(
        &server, &QLocalServer::newConnection, &application, [&server, &application]() {
            while (server.hasPendingConnections()) {
                QLocalSocket* socket = server.nextPendingConnection();
                auto buffer = std::make_shared<QByteArray>();
                QObject::connect(
                    socket, &QLocalSocket::readyRead, socket, [socket, buffer, &application]() {
                        buffer->append(socket->readAll());
                        while (true) {
                            QByteArray payload;
                            QString frameError;
                            const auto frameStatus =
                                modelharbor::ipc::takeFrame(buffer.get(), &payload, &frameError);
                            if (frameStatus == modelharbor::ipc::FrameStatus::NeedMoreData) {
                                break;
                            }
                            if (frameStatus == modelharbor::ipc::FrameStatus::Invalid) {
                                socket->abort();
                                break;
                            }

                            const auto decoded = modelharbor::ipc::decodeMessage(payload);
                            const QString id =
                                decoded.object.value(QStringLiteral("id")).toString();
                            if (!decoded.valid) {
                                sendError(socket, id, decoded.error,
                                          QStringLiteral("IPC request rejected"));
                                continue;
                            }

                            if (!decoded.object.value(QStringLiteral("method")).isString()) {
                                sendError(socket, id, QStringLiteral("missing_request_fields"),
                                          QStringLiteral("IPC method is required"));
                                continue;
                            }

                            const QString method =
                                decoded.object.value(QStringLiteral("method")).toString();
                            if (method == QStringLiteral("ping")) {
                                socket->write(modelharbor::ipc::encodeResponse(
                                    id, true,
                                    {{QStringLiteral("gateway_version"),
                                      modelharbor::core::productVersion()},
                                     {QStringLiteral("protocol_version"),
                                      modelharbor::core::kIpcProtocolVersion},
                                     {QStringLiteral("status"), QStringLiteral("ready")}}));
                                socket->flush();
                            } else if (method == QStringLiteral("get_status")) {
                                socket->write(modelharbor::ipc::encodeResponse(
                                    id, true,
                                    {{QStringLiteral("status"), QStringLiteral("ready")}}));
                                socket->flush();
                            } else if (method == QStringLiteral("subscribe_status")) {
                                socket->write(modelharbor::ipc::encodeResponse(
                                    id, true, {{QStringLiteral("subscribed"), true}}));
                                socket->write(modelharbor::ipc::encodeEvent(
                                    QStringLiteral("gateway_status"),
                                    {{QStringLiteral("status"), QStringLiteral("ready")},
                                     {QStringLiteral("gateway_version"),
                                      modelharbor::core::productVersion()}}));
                                socket->flush();
                            } else if (method == QStringLiteral("shutdown")) {
                                socket->write(modelharbor::ipc::encodeResponse(
                                    id, true,
                                    {{QStringLiteral("status"), QStringLiteral("stopping")}}));
                                socket->flush();
                                QTimer::singleShot(25, &application, &QCoreApplication::quit);
                            } else {
                                sendError(socket, id, QStringLiteral("method_not_found"),
                                          QStringLiteral("Unknown IPC method"));
                            }
                        }
                    });
                QObject::connect(socket, &QLocalSocket::disconnected, socket,
                                 &QObject::deleteLater);
            }
        });

    return application.exec();
}
