#include "ipc/ipc_protocol.h"

#include "core/version.h"

#include <QJsonDocument>

namespace modelharbor::ipc {

namespace {

QByteArray encode(const QJsonObject& object) {
    return framePayload(QJsonDocument(object).toJson(QJsonDocument::Compact));
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

QByteArray framePayload(const QByteArray& payload) {
    if (payload.size() > kMaximumFramePayload) {
        return {};
    }

    const quint32 length = static_cast<quint32>(payload.size());
    QByteArray frame(4, Qt::Uninitialized);
    frame[0] = static_cast<char>((length >> 24U) & 0xffU);
    frame[1] = static_cast<char>((length >> 16U) & 0xffU);
    frame[2] = static_cast<char>((length >> 8U) & 0xffU);
    frame[3] = static_cast<char>(length & 0xffU);
    frame.append(payload);
    return frame;
}

FrameStatus takeFrame(QByteArray* buffer, QByteArray* payload, QString* error) {
    if (buffer == nullptr || payload == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("invalid_frame_arguments");
        }
        return FrameStatus::Invalid;
    }
    if (buffer->size() < 4) {
        return FrameStatus::NeedMoreData;
    }

    const auto byte = [buffer](qsizetype index) {
        return static_cast<quint32>(static_cast<unsigned char>(buffer->at(index)));
    };
    const quint32 length = (byte(0) << 24U) | (byte(1) << 16U) | (byte(2) << 8U) | byte(3);
    if (length > static_cast<quint32>(kMaximumFramePayload)) {
        buffer->clear();
        if (error != nullptr) {
            *error = QStringLiteral("frame_too_large");
        }
        return FrameStatus::Invalid;
    }
    if (buffer->size() < 4 + static_cast<qsizetype>(length)) {
        return FrameStatus::NeedMoreData;
    }

    *payload = buffer->mid(4, static_cast<qsizetype>(length));
    buffer->remove(0, 4 + static_cast<qsizetype>(length));
    return FrameStatus::Ready;
}

DecodedMessage decodeMessage(const QByteArray& payload) {
    DecodedMessage decoded;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
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
