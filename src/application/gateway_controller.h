#pragma once

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>

class QLocalSocket;

namespace modelharbor::application {

class GatewayController final : public QObject {
    Q_OBJECT

  public:
    enum class State {
        Stopped,
        Starting,
        Connecting,
        Ready,
        Restarting,
        Stopping,
        Error,
    };
    Q_ENUM(State)

    explicit GatewayController(QString socketName = {}, QString gatewayProgram = {},
                               QString dataDirectory = {}, QObject* parent = nullptr);
    ~GatewayController() override;

    void start();
    void ping();
    void restart();
    bool shutdownAndWait(int timeoutMilliseconds = 1500);

    State state() const;
    QString gatewayVersion() const;
    QString socketName() const;

  signals:
    void stateChanged(modelharbor::application::GatewayController::State state,
                      const QString& detail);
    void gatewayVersionChanged(const QString& version);

  private:
    void setState(State state, const QString& detail = {});
    void connectToGateway();
    void spawnGateway();
    void sendRequest(const QString& method);
    void processIncoming();

    QString socketName_;
    QString gatewayProgram_;
    QString dataDirectory_;
    QProcess gatewayProcess_;
    QLocalSocket* socket_ = nullptr;
    QTimer reconnectTimer_;
    QTimer spawnTimer_;
    QTimer stopTimer_;
    QByteArray receiveBuffer_;
    State state_ = State::Stopped;
    QString gatewayVersion_;
    int requestId_ = 0;
    bool ownsGateway_ = false;
    bool spawnAttempted_ = false;
    bool restartPending_ = false;
    bool shuttingDown_ = false;
};

} // namespace modelharbor::application
