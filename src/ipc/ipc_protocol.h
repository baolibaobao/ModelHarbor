#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace modelharbor::ipc {

struct DecodedMessage {
    bool valid = false;
    QJsonObject object;
    QString error;
};

QByteArray encodeRequest(const QString& id, const QString& method,
                         const QJsonObject& params = {});
QByteArray encodeResponse(const QString& id, bool ok, const QJsonObject& result = {},
                          const QJsonObject& error = {});
QByteArray encodeEvent(const QString& event, const QJsonObject& data = {});
DecodedMessage decodeMessage(const QByteArray& line);

} // namespace modelharbor::ipc
