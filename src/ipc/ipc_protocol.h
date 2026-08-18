#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace modelharbor::ipc {

inline constexpr qsizetype kMaximumFramePayload = 4 * 1024 * 1024;

enum class FrameStatus {
    NeedMoreData,
    Ready,
    Invalid,
};

struct DecodedMessage {
    bool valid = false;
    QJsonObject object;
    QString error;
};

QByteArray encodeRequest(const QString& id, const QString& method, const QJsonObject& params = {});
QByteArray encodeResponse(const QString& id, bool ok, const QJsonObject& result = {},
                          const QJsonObject& error = {});
QByteArray encodeEvent(const QString& event, const QJsonObject& data = {});
QByteArray framePayload(const QByteArray& payload);
FrameStatus takeFrame(QByteArray* buffer, QByteArray* payload, QString* error = nullptr);
DecodedMessage decodeMessage(const QByteArray& payload);

} // namespace modelharbor::ipc
