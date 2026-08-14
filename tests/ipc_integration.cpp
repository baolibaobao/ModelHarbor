#include "ipc/ipc_protocol.h"

#include <QCoreApplication>
#include <QLocalSocket>
#include <QProcess>
#include <QThread>

#include <iostream>

namespace {

bool waitForConnection(QLocalSocket& socket, const QString& name) {
    socket.abort();
    socket.connectToServer(name);
    return socket.waitForConnected(100);
}

bool waitForLine(QLocalSocket& socket, QByteArray* line) {
    if (!socket.canReadLine() && !socket.waitForReadyRead(2000)) {
        return false;
    }
    if (!socket.canReadLine()) {
        return false;
    }
    *line = socket.readLine();
    return true;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    const QString socketName = QStringLiteral("modelharbor-test-%1").arg(application.applicationPid());
    QProcess gateway;
    gateway.start(QStringLiteral(MODELHARBOR_GATEWAY_EXECUTABLE),
                  {QStringLiteral("--ipc-name"), socketName});
    if (!gateway.waitForStarted(3000)) {
        std::cerr << "gateway failed to start: " << gateway.errorString().toStdString() << '\n';
        return 1;
    }

    QLocalSocket socket;
    bool connected = false;
    for (int attempt = 0; attempt < 30 && !connected; ++attempt) {
        connected = waitForConnection(socket, socketName);
        if (!connected) {
            QThread::msleep(25);
        }
    }
    if (!connected) {
        std::cerr << "gateway IPC connection failed\n";
        gateway.kill();
        gateway.waitForFinished(1000);
        return 2;
    }

    socket.write(modelharbor::ipc::encodeRequest(QStringLiteral("1"), QStringLiteral("ping")));
    socket.flush();
    QByteArray line;
    if (!waitForLine(socket, &line)) {
        std::cerr << "ping response timeout\n";
        gateway.kill();
        gateway.waitForFinished(1000);
        return 3;
    }
    const auto response = modelharbor::ipc::decodeMessage(line);
    const auto result = response.object.value(QStringLiteral("result")).toObject();
    if (!response.valid || !response.object.value(QStringLiteral("ok")).toBool() ||
        result.value(QStringLiteral("status")).toString() != QStringLiteral("ready")) {
        std::cerr << "invalid ping response\n";
        gateway.kill();
        gateway.waitForFinished(1000);
        return 4;
    }

    socket.write(QByteArray("{\"protocol\":\"modelharbor.ipc\",\"version\":999,\"id\":\"2\",\"method\":\"ping\"}\n"));
    socket.flush();
    if (!waitForLine(socket, &line)) {
        std::cerr << "version response timeout\n";
        gateway.kill();
        gateway.waitForFinished(1000);
        return 5;
    }
    const auto mismatch = modelharbor::ipc::decodeMessage(line);
    const auto mismatchError = mismatch.object.value(QStringLiteral("error")).toObject();
    if (!mismatch.valid || mismatch.object.value(QStringLiteral("ok")).toBool() ||
        mismatchError.value(QStringLiteral("code")).toString() !=
            QStringLiteral("protocol_version_mismatch")) {
        std::cerr << "version mismatch was not rejected\n";
        gateway.kill();
        gateway.waitForFinished(1000);
        return 6;
    }

    socket.write(modelharbor::ipc::encodeRequest(QStringLiteral("3"),
                                                 QStringLiteral("subscribe_status")));
    socket.flush();
    if (!waitForLine(socket, &line)) {
        std::cerr << "subscription response timeout\n";
        gateway.kill();
        gateway.waitForFinished(1000);
        return 7;
    }
    const auto subscription = modelharbor::ipc::decodeMessage(line);
    if (!subscription.valid || !subscription.object.value(QStringLiteral("ok")).toBool()) {
        std::cerr << "status subscription failed\n";
        gateway.kill();
        gateway.waitForFinished(1000);
        return 8;
    }
    if (!waitForLine(socket, &line)) {
        std::cerr << "status event timeout\n";
        gateway.kill();
        gateway.waitForFinished(1000);
        return 9;
    }
    const auto statusEvent = modelharbor::ipc::decodeMessage(line);
    if (!statusEvent.valid ||
        statusEvent.object.value(QStringLiteral("event")).toString() !=
            QStringLiteral("gateway_status")) {
        std::cerr << "invalid status event\n";
        gateway.kill();
        gateway.waitForFinished(1000);
        return 10;
    }

    gateway.terminate();
    if (!gateway.waitForFinished(2000)) {
        gateway.kill();
        gateway.waitForFinished(1000);
    }

    QLocalSocket restartedSocket;
    gateway.start(QStringLiteral(MODELHARBOR_GATEWAY_EXECUTABLE),
                  {QStringLiteral("--ipc-name"), socketName});
    if (!gateway.waitForStarted(3000)) {
        std::cerr << "gateway restart failed\n";
        return 11;
    }
    connected = false;
    for (int attempt = 0; attempt < 30 && !connected; ++attempt) {
        connected = waitForConnection(restartedSocket, socketName);
        if (!connected) {
            QThread::msleep(25);
        }
    }
    if (!connected) {
        std::cerr << "restarted gateway IPC connection failed\n";
        gateway.kill();
        gateway.waitForFinished(1000);
        return 12;
    }
    restartedSocket.write(
        modelharbor::ipc::encodeRequest(QStringLiteral("4"), QStringLiteral("ping")));
    restartedSocket.flush();
    if (!waitForLine(restartedSocket, &line)) {
        std::cerr << "restarted gateway ping timeout\n";
        gateway.kill();
        gateway.waitForFinished(1000);
        return 13;
    }
    const auto restartedResponse = modelharbor::ipc::decodeMessage(line);
    if (!restartedResponse.valid ||
        !restartedResponse.object.value(QStringLiteral("ok")).toBool()) {
        std::cerr << "restarted gateway ping failed\n";
        gateway.kill();
        gateway.waitForFinished(1000);
        return 14;
    }
    gateway.terminate();
    if (!gateway.waitForFinished(2000)) {
        gateway.kill();
        gateway.waitForFinished(1000);
    }
    std::cout << "ModelHarbor IPC integration OK\n";
    return 0;
}
