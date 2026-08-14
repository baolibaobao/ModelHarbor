#include "core/version.h"
#include "ipc/ipc_protocol.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocalSocket>
#include <QMainWindow>
#include <QProcess>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace {

class MainWindow final : public QMainWindow {
public:
    MainWindow() {
        setWindowTitle(QStringLiteral("ModelHarbor"));
        resize(640, 360);

        auto* central = new QWidget(this);
        auto* layout = new QVBoxLayout(central);
        auto* title = new QLabel(QStringLiteral("ModelHarbor / 模港"), central);
        title->setStyleSheet(QStringLiteral("font-size: 20px; font-weight: 600;"));
        statusLabel_ = new QLabel(QStringLiteral("正在启动网关..."), central);
        protocolLabel_ = new QLabel(
            QStringLiteral("IPC 协议 v%1").arg(modelharbor::core::kIpcProtocolVersion), central);
        auto* row = new QHBoxLayout();
        auto* pingButton = new QPushButton(QStringLiteral("Ping 网关"), central);
        pingButton->setToolTip(QStringLiteral("发送版本化 IPC ping 请求"));
        auto* restartButton = new QPushButton(QStringLiteral("重启网关"), central);
        restartButton->setToolTip(QStringLiteral("停止并重新启动本地网关进程"));
        row->addWidget(pingButton);
        row->addWidget(restartButton);
        row->addStretch();
        layout->addWidget(title);
        layout->addWidget(statusLabel_);
        layout->addWidget(protocolLabel_);
        layout->addLayout(row);
        layout->addStretch();
        setCentralWidget(central);

        socketName_ = QStringLiteral("modelharbor-gateway-%1").arg(QCoreApplication::applicationPid());
        reconnectTimer_.setInterval(100);
        connect(pingButton, &QPushButton::clicked, this, [this]() { pingGateway(); });
        connect(restartButton, &QPushButton::clicked, this, [this]() { restartGateway(); });
        connect(&reconnectTimer_, &QTimer::timeout, this, [this]() { connectToGateway(); });
        connect(&gateway_, &QProcess::started, this, [this]() {
            statusLabel_->setText(QStringLiteral("网关进程已启动，连接 IPC..."));
            reconnectTimer_.start();
            connectToGateway();
        });
        connect(&gateway_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
            statusLabel_->setText(QStringLiteral("网关启动失败：%1").arg(gateway_.errorString()));
        });
        connect(&gateway_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
            reconnectTimer_.stop();
            statusLabel_->setText(QStringLiteral("网关已停止（退出码 %1）").arg(code));
            if (restartPending_) {
                restartPending_ = false;
                startGateway();
            }
        });
        connect(&socket_, &QLocalSocket::connected, this, [this]() {
            reconnectTimer_.stop();
            statusLabel_->setText(QStringLiteral("IPC 已连接，正在检查网关状态..."));
            socket_.write(modelharbor::ipc::encodeRequest(
                QString::number(++requestId_), QStringLiteral("subscribe_status")));
            pingGateway();
        });
        connect(&socket_, &QLocalSocket::disconnected, this, [this]() {
            if (gateway_.state() != QProcess::NotRunning) {
                statusLabel_->setText(QStringLiteral("IPC 已断开，正在重连..."));
                reconnectTimer_.start();
            }
        });
        connect(&socket_, &QLocalSocket::errorOccurred, this,
                [this](QLocalSocket::LocalSocketError) {
                    if (gateway_.state() != QProcess::NotRunning) {
                        reconnectTimer_.start();
                    }
                });
        connect(&socket_, &QLocalSocket::readyRead, this, [this]() {
            while (socket_.canReadLine()) {
                const auto decoded = modelharbor::ipc::decodeMessage(socket_.readLine());
                if (!decoded.valid || !decoded.object.value(QStringLiteral("ok")).toBool()) {
                    if (decoded.valid &&
                        decoded.object.value(QStringLiteral("event")).toString() ==
                            QStringLiteral("gateway_status")) {
                        const auto data = decoded.object.value(QStringLiteral("data")).toObject();
                        statusLabel_->setText(QStringLiteral("网关状态：%1（%2）")
                                                  .arg(data.value(QStringLiteral("status")).toString(),
                                                       data.value(QStringLiteral("gateway_version"))
                                                           .toString()));
                        continue;
                    }
                    const auto error = decoded.object.value(QStringLiteral("error")).toObject();
                    const QString code = error.value(QStringLiteral("code")).toString(decoded.error);
                    statusLabel_->setText(QStringLiteral("IPC 响应错误：%1").arg(code));
                    continue;
                }
                const auto result = decoded.object.value(QStringLiteral("result")).toObject();
                const QString gatewayVersion =
                    result.value(QStringLiteral("gateway_version")).toString();
                if (!gatewayVersion.isEmpty()) {
                    statusLabel_->setText(QStringLiteral("网关就绪：%1").arg(gatewayVersion));
                }
            }
        });

        gatewayProgram_ = QCoreApplication::applicationDirPath() +
#ifdef Q_OS_WIN
            QStringLiteral("/modelharbor-gateway.exe");
#else
            QStringLiteral("/modelharbor-gateway");
#endif
        startGateway();
    }

    ~MainWindow() override {
        restartPending_ = false;
        reconnectTimer_.stop();
        if (gateway_.state() != QProcess::NotRunning) {
            gateway_.terminate();
            if (!gateway_.waitForFinished(1500)) {
                gateway_.kill();
            }
        }
    }

private:
    void connectToGateway() {
        if (socket_.state() == QLocalSocket::ConnectedState ||
            socket_.state() == QLocalSocket::ConnectingState) {
            return;
        }
        socket_.abort();
        socket_.connectToServer(socketName_);
    }

    void startGateway() {
        socket_.abort();
        statusLabel_->setText(QStringLiteral("正在启动网关..."));
        reconnectTimer_.start();
        gateway_.start(gatewayProgram_, {QStringLiteral("--ipc-name"), socketName_});
    }

    void restartGateway() {
        if (gateway_.state() == QProcess::NotRunning) {
            startGateway();
            return;
        }
        restartPending_ = true;
        statusLabel_->setText(QStringLiteral("正在重启网关..."));
        socket_.abort();
        gateway_.terminate();
    }

    void pingGateway() {
        if (socket_.state() != QLocalSocket::ConnectedState) {
            connectToGateway();
            return;
        }
        socket_.write(modelharbor::ipc::encodeRequest(
            QString::number(++requestId_), QStringLiteral("ping")));
        socket_.flush();
    }

    QLabel* statusLabel_ = nullptr;
    QLabel* protocolLabel_ = nullptr;
    QProcess gateway_;
    QLocalSocket socket_;
    QTimer reconnectTimer_;
    QString socketName_;
    QString gatewayProgram_;
    int requestId_ = 0;
    bool restartPending_ = false;
};

} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    MainWindow window;
    window.show();
    return application.exec();
}
