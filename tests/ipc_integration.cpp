#include "ipc/ipc_protocol.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>

#include <iostream>

namespace {

bool connectWithRetry(QLocalSocket& socket, const QString& name) {
    for (int attempt = 0; attempt < 30; ++attempt) {
        socket.abort();
        socket.connectToServer(name);
        if (socket.waitForConnected(100)) {
            return true;
        }
        QThread::msleep(25);
    }
    return false;
}

bool waitForFrame(QLocalSocket& socket, QByteArray& buffer, QByteArray* payload) {
    for (int attempt = 0; attempt < 20; ++attempt) {
        QString error;
        const auto status = modelharbor::ipc::takeFrame(&buffer, payload, &error);
        if (status == modelharbor::ipc::FrameStatus::Ready) {
            return true;
        }
        if (status == modelharbor::ipc::FrameStatus::Invalid) {
            return false;
        }
        if (!socket.waitForReadyRead(100)) {
            continue;
        }
        buffer.append(socket.readAll());
    }
    return false;
}

bool sendAndReceive(QLocalSocket& socket, QByteArray& buffer, const QByteArray& request,
                    modelharbor::ipc::DecodedMessage* response) {
    if (socket.write(request) != request.size()) {
        return false;
    }
    socket.flush();
    if (socket.bytesToWrite() > 0 && !socket.waitForBytesWritten(500)) {
        return false;
    }
    QByteArray payload;
    if (!waitForFrame(socket, buffer, &payload)) {
        return false;
    }
    *response = modelharbor::ipc::decodeMessage(payload);
    return true;
}

class ProcessGuard final {
  public:
    explicit ProcessGuard(QProcess& process) : process_(process) {}
    ~ProcessGuard() {
        if (process_.state() != QProcess::NotRunning) {
            process_.kill();
            process_.waitForFinished(1000);
        }
    }

  private:
    QProcess& process_;
};

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    const QString socketName = QStringLiteral("modelharbor-test-%1")
                                   .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QTemporaryDir dataDirectory;
    if (!dataDirectory.isValid()) {
        std::cerr << "temporary gateway data directory failed\n";
        return 1;
    }
    const QStringList gatewayArguments{QStringLiteral("--ipc-name"), socketName,
                                       QStringLiteral("--data-dir"), dataDirectory.path()};
    QProcess gateway;
    ProcessGuard gatewayGuard(gateway);
    gateway.start(QStringLiteral(MODELHARBOR_GATEWAY_EXECUTABLE), gatewayArguments);
    if (!gateway.waitForStarted(3000)) {
        std::cerr << "gateway failed to start\n";
        return 1;
    }

    QLocalSocket socket;
    if (!connectWithRetry(socket, socketName)) {
        gateway.kill();
        gateway.waitForFinished(1000);
        std::cerr << "gateway IPC connection failed\n";
        return 2;
    }

    QByteArray buffer;
    modelharbor::ipc::DecodedMessage response;
    const bool pingReceived = sendAndReceive(
        socket, buffer,
        modelharbor::ipc::encodeRequest(QStringLiteral("1"), QStringLiteral("ping")), &response);
    const int pingSchemaVersion = response.object.value(QStringLiteral("result"))
                                      .toObject()
                                      .value(QStringLiteral("database_schema_version"))
                                      .toInt();
    if (!pingReceived || !response.valid || !response.object.value(QStringLiteral("ok")).toBool() ||
        pingSchemaVersion != 2) {
        std::cerr << "ping failed: received=" << pingReceived << " valid=" << response.valid
                  << " schema=" << pingSchemaVersion << " process_state=" << gateway.state()
                  << " process_error=" << gateway.errorString().toStdString()
                  << " stderr=" << gateway.readAllStandardError().toStdString() << '\n';
        return 3;
    }

    const QJsonObject incompatible{
        {QStringLiteral("protocol"), QStringLiteral("modelharbor.ipc")},
        {QStringLiteral("version"), 999},
        {QStringLiteral("id"), QStringLiteral("2")},
        {QStringLiteral("method"), QStringLiteral("ping")},
    };
    if (!sendAndReceive(socket, buffer,
                        modelharbor::ipc::framePayload(
                            QJsonDocument(incompatible).toJson(QJsonDocument::Compact)),
                        &response) ||
        response.object.value(QStringLiteral("error"))
                .toObject()
                .value(QStringLiteral("code"))
                .toString() != QStringLiteral("protocol_version_mismatch")) {
        std::cerr << "version mismatch was not rejected\n";
        return 4;
    }

    if (!sendAndReceive(socket, buffer,
                        modelharbor::ipc::encodeRequest(QStringLiteral("3"),
                                                        QStringLiteral("subscribe_status")),
                        &response) ||
        !response.object.value(QStringLiteral("ok")).toBool()) {
        std::cerr << "subscription failed\n";
        return 5;
    }
    QByteArray payload;
    if (!waitForFrame(socket, buffer, &payload) ||
        modelharbor::ipc::decodeMessage(payload).object.value(QStringLiteral("event")).toString() !=
            QStringLiteral("gateway_status")) {
        std::cerr << "status event failed\n";
        return 6;
    }

    if (!sendAndReceive(
            socket, buffer,
            modelharbor::ipc::encodeRequest(QStringLiteral("4"), QStringLiteral("shutdown")),
            &response) ||
        response.object.value(QStringLiteral("result"))
                .toObject()
                .value(QStringLiteral("status"))
                .toString() != QStringLiteral("stopping") ||
        !gateway.waitForFinished(2000)) {
        std::cerr << "graceful shutdown failed\n";
        gateway.kill();
        gateway.waitForFinished(1000);
        return 7;
    }

    gateway.start(QStringLiteral(MODELHARBOR_GATEWAY_EXECUTABLE), gatewayArguments);
    if (!gateway.waitForStarted(3000)) {
        std::cerr << "same-name restart failed\n";
        return 8;
    }
    QLocalSocket restartedSocket;
    if (!connectWithRetry(restartedSocket, socketName)) {
        std::cerr << "restarted gateway connection failed\n";
        gateway.kill();
        gateway.waitForFinished(1000);
        return 9;
    }
    QByteArray restartedBuffer;
    const bool restartedResponse = sendAndReceive(
        restartedSocket, restartedBuffer,
        modelharbor::ipc::encodeRequest(QStringLiteral("5"), QStringLiteral("shutdown")),
        &response);
    const bool restartedExited = gateway.waitForFinished(2000);
    if (!restartedResponse || !restartedExited) {
        std::cerr << "restarted gateway shutdown failed: response=" << restartedResponse
                  << " exited=" << restartedExited
                  << " process_error=" << gateway.errorString().toStdString()
                  << " stderr=" << gateway.readAllStandardError().toStdString() << '\n';
        gateway.kill();
        gateway.waitForFinished(1000);
        return 10;
    }

    std::cout << "ModelHarbor IPC integration OK\n";
    return 0;
}
