#include "application/gateway_controller.h"

#include "ipc/ipc_endpoint.h"
#include "ipc/ipc_protocol.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QLocalSocket>

#include <utility>

namespace modelharbor::application {

GatewayController::GatewayController(QString socketName, QString gatewayProgram, QObject* parent)
    : QObject(parent), socketName_(socketName.isEmpty() ? modelharbor::ipc::currentUserSocketName()
                                                        : std::move(socketName)),
      gatewayProgram_(std::move(gatewayProgram)), socket_(new QLocalSocket(this)) {
    if (gatewayProgram_.isEmpty()) {
        gatewayProgram_ = QCoreApplication::applicationDirPath() +
#ifdef Q_OS_WIN
                          QStringLiteral("/modelharbor-gateway.exe");
#else
                          QStringLiteral("/modelharbor-gateway");
#endif
    }

    reconnectTimer_.setInterval(150);
    spawnTimer_.setSingleShot(true);
    spawnTimer_.setInterval(350);
    stopTimer_.setSingleShot(true);
    stopTimer_.setInterval(1500);

    connect(&reconnectTimer_, &QTimer::timeout, this, &GatewayController::connectToGateway);
    connect(&spawnTimer_, &QTimer::timeout, this, &GatewayController::spawnGateway);
    connect(&stopTimer_, &QTimer::timeout, this, [this]() {
        if (ownsGateway_ && gatewayProcess_.state() != QProcess::NotRunning) {
            gatewayProcess_.kill();
        }
    });
    connect(&gatewayProcess_, &QProcess::started, this, [this]() {
        setState(State::Connecting, QStringLiteral("gateway_process_started"));
        reconnectTimer_.start();
        connectToGateway();
    });
    connect(&gatewayProcess_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        setState(State::Error, gatewayProcess_.errorString());
    });
    connect(&gatewayProcess_, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
        stopTimer_.stop();
        ownsGateway_ = false;
        if (restartPending_ && !shuttingDown_) {
            restartPending_ = false;
            spawnAttempted_ = false;
            spawnGateway();
            return;
        }
        if (shuttingDown_) {
            setState(State::Stopped, QStringLiteral("gateway_stopped"));
        } else if (socket_->state() != QLocalSocket::ConnectedState) {
            setState(State::Stopped, QStringLiteral("gateway_process_exited"));
        }
    });

    connect(socket_, &QLocalSocket::connected, this, [this]() {
        spawnTimer_.stop();
        reconnectTimer_.stop();
        receiveBuffer_.clear();
        setState(State::Connecting, QStringLiteral("ipc_connected"));
        sendRequest(QStringLiteral("subscribe_status"));
        sendRequest(QStringLiteral("ping"));
    });
    connect(socket_, &QLocalSocket::readyRead, this, &GatewayController::processIncoming);
    connect(socket_, &QLocalSocket::disconnected, this, [this]() {
        receiveBuffer_.clear();
        if (shuttingDown_) {
            setState(State::Stopped, QStringLiteral("ipc_disconnected"));
            return;
        }
        if (restartPending_ && gatewayProcess_.state() == QProcess::NotRunning) {
            restartPending_ = false;
            spawnAttempted_ = false;
            spawnGateway();
            return;
        }
        setState(State::Connecting, QStringLiteral("ipc_reconnecting"));
        reconnectTimer_.start();
        if (gatewayProcess_.state() == QProcess::NotRunning) {
            spawnTimer_.start();
        }
    });
    connect(socket_, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError) {
        if (!shuttingDown_ && gatewayProcess_.state() != QProcess::Starting) {
            reconnectTimer_.start();
        }
    });
}

GatewayController::~GatewayController() { shutdownAndWait(); }

void GatewayController::start() {
    if (state_ == State::Ready || state_ == State::Starting || state_ == State::Connecting) {
        return;
    }
    shuttingDown_ = false;
    restartPending_ = false;
    spawnAttempted_ = false;
    setState(State::Connecting, QStringLiteral("discovering_gateway"));
    connectToGateway();
    spawnTimer_.start();
}

void GatewayController::ping() {
    if (socket_->state() == QLocalSocket::ConnectedState) {
        sendRequest(QStringLiteral("ping"));
    } else {
        connectToGateway();
    }
}

void GatewayController::restart() {
    shuttingDown_ = false;
    restartPending_ = true;
    setState(State::Restarting, QStringLiteral("restart_requested"));
    if (socket_->state() == QLocalSocket::ConnectedState) {
        sendRequest(QStringLiteral("shutdown"));
        stopTimer_.start();
        return;
    }
    if (ownsGateway_ && gatewayProcess_.state() != QProcess::NotRunning) {
        gatewayProcess_.terminate();
        stopTimer_.start();
        return;
    }
    restartPending_ = false;
    spawnAttempted_ = false;
    spawnGateway();
}

bool GatewayController::shutdownAndWait(int timeoutMilliseconds) {
    if (state_ == State::Stopped && gatewayProcess_.state() == QProcess::NotRunning &&
        socket_->state() != QLocalSocket::ConnectedState) {
        return true;
    }

    shuttingDown_ = true;
    restartPending_ = false;
    reconnectTimer_.stop();
    spawnTimer_.stop();
    stopTimer_.stop();
    setState(State::Stopping, QStringLiteral("shutdown_requested"));
    if (socket_->state() == QLocalSocket::ConnectedState) {
        sendRequest(QStringLiteral("shutdown"));
        socket_->waitForBytesWritten(250);
    }

    bool stopped = true;
    if (ownsGateway_ && gatewayProcess_.state() != QProcess::NotRunning) {
        stopped = gatewayProcess_.waitForFinished(timeoutMilliseconds);
        if (!stopped) {
            gatewayProcess_.kill();
            stopped = gatewayProcess_.waitForFinished(500);
        }
    } else if (socket_->state() == QLocalSocket::ConnectedState) {
        stopped = socket_->waitForDisconnected(timeoutMilliseconds);
    }
    socket_->abort();
    setState(State::Stopped,
             stopped ? QStringLiteral("gateway_stopped") : QStringLiteral("gateway_stop_timeout"));
    return stopped;
}

GatewayController::State GatewayController::state() const { return state_; }

QString GatewayController::gatewayVersion() const { return gatewayVersion_; }

QString GatewayController::socketName() const { return socketName_; }

void GatewayController::setState(State state, const QString& detail) {
    if (state_ == state && detail.isEmpty()) {
        return;
    }
    state_ = state;
    emit stateChanged(state_, detail);
}

void GatewayController::connectToGateway() {
    if (shuttingDown_ || socket_->state() == QLocalSocket::ConnectedState ||
        socket_->state() == QLocalSocket::ConnectingState) {
        return;
    }
    socket_->abort();
    socket_->connectToServer(socketName_);
}

void GatewayController::spawnGateway() {
    if (shuttingDown_ || socket_->state() == QLocalSocket::ConnectedState ||
        gatewayProcess_.state() != QProcess::NotRunning || spawnAttempted_) {
        return;
    }
    spawnAttempted_ = true;
    ownsGateway_ = true;
    setState(State::Starting, QStringLiteral("starting_gateway_process"));
    gatewayProcess_.start(gatewayProgram_, {QStringLiteral("--ipc-name"), socketName_});
}

void GatewayController::sendRequest(const QString& method) {
    socket_->write(modelharbor::ipc::encodeRequest(QString::number(++requestId_), method));
    socket_->flush();
}

void GatewayController::processIncoming() {
    receiveBuffer_.append(socket_->readAll());
    while (true) {
        QByteArray payload;
        QString frameError;
        const auto frameStatus =
            modelharbor::ipc::takeFrame(&receiveBuffer_, &payload, &frameError);
        if (frameStatus == modelharbor::ipc::FrameStatus::NeedMoreData) {
            return;
        }
        if (frameStatus == modelharbor::ipc::FrameStatus::Invalid) {
            setState(State::Error, frameError);
            socket_->abort();
            return;
        }

        const auto decoded = modelharbor::ipc::decodeMessage(payload);
        if (!decoded.valid) {
            setState(State::Error, decoded.error);
            continue;
        }
        if (decoded.object.value(QStringLiteral("event")).toString() ==
            QStringLiteral("gateway_status")) {
            const QJsonObject data = decoded.object.value(QStringLiteral("data")).toObject();
            gatewayVersion_ = data.value(QStringLiteral("gateway_version")).toString();
            emit gatewayVersionChanged(gatewayVersion_);
            setState(State::Ready, data.value(QStringLiteral("status")).toString());
            continue;
        }
        if (!decoded.object.value(QStringLiteral("ok")).toBool()) {
            const QJsonObject error = decoded.object.value(QStringLiteral("error")).toObject();
            setState(State::Error, error.value(QStringLiteral("code")).toString());
            continue;
        }

        const QJsonObject result = decoded.object.value(QStringLiteral("result")).toObject();
        const QString version = result.value(QStringLiteral("gateway_version")).toString();
        if (!version.isEmpty()) {
            gatewayVersion_ = version;
            emit gatewayVersionChanged(gatewayVersion_);
            setState(State::Ready, result.value(QStringLiteral("status")).toString());
        } else if (result.value(QStringLiteral("status")).toString() ==
                   QStringLiteral("stopping")) {
            setState(restartPending_ ? State::Restarting : State::Stopping,
                     QStringLiteral("gateway_stopping"));
        }
    }
}

} // namespace modelharbor::application
