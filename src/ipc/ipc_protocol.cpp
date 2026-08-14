#include "ipc/ipc_protocol.h"

#include "core/version.h"

#include <QJsonDocument>

namespace modelharbor::ipc {

namespace {

QByteArray encode(const QJsonObject& object) {
    return QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
}

} // namespace

QByteArray encodeRequest(const QString& id, const QString& method, const QJsonObject& params) {
    QJsonObject object{
        {QStringLiteral("protocol"), QStringLiteral("modelharbor.ipc")},
        {QStringLiteral("version"), modelharbor::core::kIpcProtocolVersion},
        {QStringLiteral("id"), id},
        {QStringLiteral("method"), method},
        {QStringLiteral("params"), params},
    };
    return encode(object);
}

QByteArray encodeResponse(const QString& id, bool ok, const QJsonObject& result,
                          const QJsonObject& error) {
    QJsonObject object{
        {QStringLiteral("protocol"), QStringLiteral("modelharbor.ipc")},
        {QStringLiteral("version"), modelharbor::core::kIpcProtocolVersion},
        {QStringLiteral("id"), id},
        {QStringLiteral("ok"), ok},
    };
    if (ok) {
        object.insert(QStringLiteral("result"), result);
    } else {
        object.insert(QStringLiteral("error"), error);
    }
    return encode(object);
}

QByteArray encodeEvent(const QString& event, const QJsonObject& data) {
    return encode({
        {QStringLiteral("protocol"), QStringLiteral("modelharbor.ipc")},
        {QStringLiteral("version"), modelharbor::core::kIpcProtocolVersion},
        {QStringLiteral("event"), event},
        {QStringLiteral("data"), data},
    });
}

DecodedMessage decodeMessage(const QByteArray& line) {
    DecodedMessage decoded;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(line.trimmed(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        decoded.error = QStringLiteral("invalid_json");
        return decoded;
    }

    decoded.object = document.object();
    if (decoded.object.value(QStringLiteral("protocol")).toString() !=
        QStringLiteral("modelharbor.ipc")) {
        decoded.error = QStringLiteral("invalid_protocol");
        return decoded;
    }
    if (decoded.object.value(QStringLiteral("version")).toInt() !=
        modelharbor::core::kIpcProtocolVersion) {
        decoded.error = QStringLiteral("protocol_version_mismatch");
        return decoded;
    }
    if (!decoded.object.value(QStringLiteral("id")).isString() &&
        !decoded.object.value(QStringLiteral("event")).isString()) {
        decoded.error = QStringLiteral("missing_message_identity");
        return decoded;
    }

    decoded.valid = true;
    return decoded;
}

} // namespace modelharbor::ipc
