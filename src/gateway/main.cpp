#include "core/version.h"
#include "ipc/ipc_endpoint.h"
#include "ipc/ipc_protocol.h"
#include "persistence/migration.h"
#include "persistence/sqlite_database.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

#include <filesystem>
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

std::filesystem::path filesystemPath(const QString& value) {
#ifdef Q_OS_WIN
    return std::filesystem::path(value.toStdWString());
#else
    return std::filesystem::path(value.toStdString());
#endif
}

void processAvailableFrames(QLocalSocket* socket, const std::shared_ptr<QByteArray>& buffer,
                            QCoreApplication* application, int schemaVersion) {
    buffer->append(socket->readAll());
    while (true) {
        QByteArray payload;
        QString frameError;
        const auto frameStatus = modelharbor::ipc::takeFrame(buffer.get(), &payload, &frameError);
        if (frameStatus == modelharbor::ipc::FrameStatus::NeedMoreData) {
            return;
        }
        if (frameStatus == modelharbor::ipc::FrameStatus::Invalid) {
            socket->abort();
            return;
        }

        const auto decoded = modelharbor::ipc::decodeMessage(payload);
        const QString id = decoded.object.value(QStringLiteral("id")).toString();
        if (!decoded.valid) {
            sendError(socket, id, decoded.error, QStringLiteral("IPC request rejected"));
            continue;
        }

        if (!decoded.object.value(QStringLiteral("method")).isString()) {
            sendError(socket, id, QStringLiteral("missing_request_fields"),
                      QStringLiteral("IPC method is required"));
            continue;
        }

        const QString method = decoded.object.value(QStringLiteral("method")).toString();
        if (method == QStringLiteral("ping")) {
            socket->write(modelharbor::ipc::encodeResponse(
                id, true,
                {{QStringLiteral("gateway_version"), modelharbor::core::productVersion()},
                 {QStringLiteral("protocol_version"), modelharbor::core::kIpcProtocolVersion},
                 {QStringLiteral("database_schema_version"), schemaVersion},
                 {QStringLiteral("status"), QStringLiteral("ready")}}));
            socket->flush();
        } else if (method == QStringLiteral("get_status")) {
            socket->write(modelharbor::ipc::encodeResponse(
                id, true,
                {{QStringLiteral("status"), QStringLiteral("ready")},
                 {QStringLiteral("database_schema_version"), schemaVersion}}));
            socket->flush();
        } else if (method == QStringLiteral("subscribe_status")) {
            socket->write(
                modelharbor::ipc::encodeResponse(id, true, {{QStringLiteral("subscribed"), true}}));
            socket->write(modelharbor::ipc::encodeEvent(
                QStringLiteral("gateway_status"),
                {{QStringLiteral("status"), QStringLiteral("ready")},
                 {QStringLiteral("gateway_version"), modelharbor::core::productVersion()},
                 {QStringLiteral("database_schema_version"), schemaVersion}}));
            socket->flush();
        } else if (method == QStringLiteral("shutdown")) {
            socket->write(modelharbor::ipc::encodeResponse(
                id, true, {{QStringLiteral("status"), QStringLiteral("stopping")}}));
            socket->flush();
            QTimer::singleShot(25, application, &QCoreApplication::quit);
        } else {
            sendError(socket, id, QStringLiteral("method_not_found"),
                      QStringLiteral("Unknown IPC method"));
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ModelHarbor"));
    QCoreApplication::setApplicationName(QStringLiteral("modelharbor-gateway"));
    const QString socketName = optionValue(application.arguments(), QStringLiteral("--ipc-name"),
                                           modelharbor::ipc::currentUserSocketName());
    QString defaultDataDirectory = qEnvironmentVariable("MODELHARBOR_DATA_DIR");
    if (defaultDataDirectory.isEmpty()) {
        defaultDataDirectory =
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    }
    const QString dataDirectory =
        optionValue(application.arguments(), QStringLiteral("--data-dir"), defaultDataDirectory);

    int schemaVersion = 0;
    std::unique_ptr<modelharbor::persistence::SqliteDatabase> database;
    try {
        auto opened = modelharbor::persistence::SqliteDatabase::open(filesystemPath(dataDirectory) /
                                                                     "modelharbor.db");
        modelharbor::persistence::MigrationRunner migrations;
        migrations.apply(opened, modelharbor::persistence::builtInMigrations());
        schemaVersion = modelharbor::persistence::MigrationRunner::currentVersion(opened);
        database = std::make_unique<modelharbor::persistence::SqliteDatabase>(std::move(opened));
    } catch (const std::exception& error) {
        QTextStream(stderr) << "gateway database initialization failed: " << error.what() << '\n';
        return 3;
    }

    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!server.listen(socketName)) {
        QTextStream(stderr) << "gateway listen failed: " << server.errorString() << '\n';
        return 2;
    }

    const auto acceptPending = [&server, &application, schemaVersion]() {
        while (server.hasPendingConnections()) {
            QLocalSocket* socket = server.nextPendingConnection();
            auto buffer = std::make_shared<QByteArray>();
            const auto process = [socket, buffer, &application, schemaVersion]() {
                processAvailableFrames(socket, buffer, &application, schemaVersion);
            };
            QObject::connect(socket, &QLocalSocket::readyRead, socket, process);
            QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            auto* readSafetyTimer = new QTimer(socket);
            readSafetyTimer->setInterval(25);
            QObject::connect(readSafetyTimer, &QTimer::timeout, socket, process);
            readSafetyTimer->start();
            process();
            QTimer::singleShot(0, socket, process);
        }
    };
    QObject::connect(&server, &QLocalServer::newConnection, &application, acceptPending);
    QTimer acceptSafetyTimer;
    acceptSafetyTimer.setInterval(25);
    QObject::connect(&acceptSafetyTimer, &QTimer::timeout, &application, acceptPending);
    acceptSafetyTimer.start();

    return application.exec();
}
