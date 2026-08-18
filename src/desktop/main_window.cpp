#include "desktop/main_window.h"

#include "core/version.h"
#include "desktop/theme.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QToolButton>
#include <QVBoxLayout>

namespace modelharbor::desktop {

namespace {

using GatewayState = modelharbor::application::GatewayController::State;

QString stateText(GatewayState state) {
    switch (state) {
    case GatewayState::Stopped:
        return QStringLiteral("已停止");
    case GatewayState::Starting:
        return QStringLiteral("正在启动");
    case GatewayState::Connecting:
        return QStringLiteral("正在连接");
    case GatewayState::Ready:
        return QStringLiteral("已就绪");
    case GatewayState::Restarting:
        return QStringLiteral("正在重启");
    case GatewayState::Stopping:
        return QStringLiteral("正在停止");
    case GatewayState::Error:
        return QStringLiteral("异常");
    }
    return QStringLiteral("未知");
}

struct NavigationItem {
    const char* title;
    const char* icon;
    const char* description;
};

constexpr NavigationItem kNavigation[] = {
    {"总览", ":/lucide/activity.svg", "网关状态、请求趋势和最近异常将在后续阶段接入。"},
    {"渠道", ":/lucide/server.svg", "站点、凭据、模型映射和调度参数。"},
    {"模型", ":/lucide/boxes.svg", "逻辑模型、上游模型和渠道可用性。"},
    {"健康", ":/lucide/heart-pulse.svg", "连通性、首字时间和总延迟检测。"},
    {"实验室", ":/lucide/flask-conical.svg", "Chat Completions 试跑和响应结构校验。"},
    {"账号池", ":/lucide/users.svg", "平台账号、分组、标签和导入任务。"},
    {"统计", ":/lucide/chart-no-axes-combined.svg", "请求、Token、费用和路由统计。"},
    {"设置", ":/lucide/settings.svg", "本地地址、主题、日志和数据保留策略。"},
};

} // namespace

MainWindow::MainWindow(bool startGateway, QWidget* parent)
    : QMainWindow(parent), startGateway_(startGateway) {
    setWindowTitle(QStringLiteral("ModelHarbor / 模港"));
    setMinimumSize(920, 560);
    resize(1180, 720);
    applyCurrentTheme();

    auto* central = new QWidget(this);
    auto* rootLayout = new QHBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* sidebar = new QWidget(central);
    sidebar->setMinimumWidth(190);
    sidebar->setMaximumWidth(230);
    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(12, 16, 12, 12);
    auto* brand = new QLabel(QStringLiteral("ModelHarbor\n模港"), sidebar);
    brand->setStyleSheet(QStringLiteral("font-size: 17px; font-weight: 600;"));
    sidebarLayout->addWidget(brand);
    navigation_ = new QListWidget(sidebar);
    navigation_->setIconSize(QSize(18, 18));
    navigation_->setSpacing(2);
    navigation_->setUniformItemSizes(true);
    sidebarLayout->addWidget(navigation_);
    auto* sidebarFooter = new QLabel(QStringLiteral("本地模式 · 127.0.0.1"), sidebar);
    sidebarFooter->setObjectName(QStringLiteral("muted"));
    sidebarLayout->addWidget(sidebarFooter);
    rootLayout->addWidget(sidebar);

    auto* content = new QWidget(central);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(28, 22, 28, 22);
    contentLayout->setSpacing(16);
    auto* header = new QHBoxLayout();
    pageTitle_ = new QLabel(QStringLiteral("总览"), content);
    pageTitle_->setObjectName(QStringLiteral("pageTitle"));
    header->addWidget(pageTitle_);
    header->addStretch();
    themeButton_ = new QToolButton(content);
    themeButton_->setIcon(QIcon(QStringLiteral(":/lucide/sun-moon.svg")));
    themeButton_->setToolTip(QStringLiteral("切换浅色/深色主题"));
    themeButton_->setFixedSize(34, 34);
    header->addWidget(themeButton_);
    contentLayout->addLayout(header);

    auto* gatewayBar = new QHBoxLayout();
    gatewayStatus_ = new QLabel(QStringLiteral("网关：未启动"), content);
    gatewayDetail_ = new QLabel(
        QStringLiteral("IPC 协议 v%1").arg(modelharbor::core::kIpcProtocolVersion), content);
    gatewayDetail_->setObjectName(QStringLiteral("muted"));
    gatewayBar->addWidget(gatewayStatus_);
    gatewayBar->addSpacing(12);
    gatewayBar->addWidget(gatewayDetail_);
    gatewayBar->addStretch();
    auto* pingButton = new QPushButton(QStringLiteral("Ping"), content);
    pingButton->setIcon(QIcon(QStringLiteral(":/lucide/activity.svg")));
    pingButton->setToolTip(QStringLiteral("检查本地网关 IPC 状态"));
    auto* restartButton = new QPushButton(QStringLiteral("重启"), content);
    restartButton->setIcon(QIcon(QStringLiteral(":/lucide/rotate-ccw.svg")));
    restartButton->setToolTip(QStringLiteral("优雅停止并重新启动本地网关"));
    gatewayBar->addWidget(pingButton);
    gatewayBar->addWidget(restartButton);
    contentLayout->addLayout(gatewayBar);

    pages_ = new QStackedWidget(content);
    for (const auto& item : kNavigation) {
        auto* page = new QWidget(pages_);
        auto* pageLayout = new QVBoxLayout(page);
        pageLayout->setContentsMargins(0, 12, 0, 0);
        auto* pageDescription = new QLabel(QString::fromUtf8(item.description), page);
        pageDescription->setWordWrap(true);
        pageDescription->setObjectName(QStringLiteral("muted"));
        auto* emptyState = new QLabel(QStringLiteral("此模块将在对应阶段实现。"), page);
        emptyState->setAlignment(Qt::AlignCenter);
        emptyState->setMinimumHeight(180);
        pageLayout->addWidget(pageDescription);
        pageLayout->addWidget(emptyState);
        pageLayout->addStretch();
        pages_->addWidget(page);
        auto* navigationItem = new QListWidgetItem(QIcon(QString::fromUtf8(item.icon)),
                                                   QString::fromUtf8(item.title), navigation_);
        navigationItem->setToolTip(QString::fromUtf8(item.description));
    }
    contentLayout->addWidget(pages_, 1);
    setCentralWidget(central);

    connect(navigation_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0 || row >= pages_->count())
            return;
        pages_->setCurrentIndex(row);
        pageTitle_->setText(navigation_->item(row)->text());
    });
    connect(themeButton_, &QToolButton::clicked, this, [this]() {
        darkMode_ = !darkMode_;
        applyCurrentTheme();
    });
    connect(pingButton, &QPushButton::clicked, &gateway_,
            &modelharbor::application::GatewayController::ping);
    connect(restartButton, &QPushButton::clicked, &gateway_,
            &modelharbor::application::GatewayController::restart);
    connect(&gateway_, &modelharbor::application::GatewayController::stateChanged, this,
            [this](GatewayState state, const QString& detail) {
                gatewayStatus_->setText(QStringLiteral("网关：%1").arg(stateText(state)));
                gatewayDetail_->setText(detail);
                if (tray_ != nullptr) {
                    tray_->setToolTip(QStringLiteral("ModelHarbor - %1").arg(stateText(state)));
                }
            });
    connect(&gateway_, &modelharbor::application::GatewayController::gatewayVersionChanged, this,
            [this](const QString& version) {
                gatewayDetail_->setText(QStringLiteral("网关版本：%1").arg(version));
            });

    navigation_->setCurrentRow(0);
    setupTray();
    if (startGateway_)
        gateway_.start();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (forceExit_) {
        gateway_.shutdownAndWait();
        if (tray_ != nullptr)
            tray_->hide();
        event->accept();
        return;
    }

    QMessageBox prompt(this);
    prompt.setWindowTitle(QStringLiteral("关闭 ModelHarbor"));
    prompt.setText(QStringLiteral("请选择关闭窗口后的行为。"));
    auto* trayButton =
        prompt.addButton(QStringLiteral("最小化到托盘并保持服务"), QMessageBox::ActionRole);
    auto* exitButton =
        prompt.addButton(QStringLiteral("退出软件并停止网关"), QMessageBox::DestructiveRole);
    prompt.addButton(QMessageBox::Cancel);
    trayButton->setEnabled(QSystemTrayIcon::isSystemTrayAvailable());
    prompt.exec();
    if (prompt.clickedButton() == trayButton) {
        hide();
        if (tray_ != nullptr) {
            tray_->showMessage(QStringLiteral("ModelHarbor"),
                               QStringLiteral("本地服务继续在托盘运行。"),
                               QSystemTrayIcon::Information, 2500);
        }
        event->ignore();
        return;
    }
    if (prompt.clickedButton() == exitButton) {
        forceExit_ = true;
        gateway_.shutdownAndWait();
        if (tray_ != nullptr)
            tray_->hide();
        event->accept();
        return;
    }
    event->ignore();
}

void MainWindow::setupTray() {
    tray_ = new QSystemTrayIcon(this);
    tray_->setIcon(style()->standardIcon(QStyle::SP_ComputerIcon));
    tray_->setToolTip(QStringLiteral("ModelHarbor"));
    auto* menu = new QMenu(this);
    QAction* showAction = menu->addAction(QStringLiteral("显示主窗口"));
    QAction* pingAction = menu->addAction(QStringLiteral("Ping 网关"));
    QAction* restartAction = menu->addAction(QStringLiteral("重启网关"));
    menu->addSeparator();
    QAction* exitAction = menu->addAction(QStringLiteral("退出软件"));
    tray_->setContextMenu(menu);
    connect(showAction, &QAction::triggered, this, [this]() {
        showNormal();
        activateWindow();
    });
    connect(pingAction, &QAction::triggered, &gateway_,
            &modelharbor::application::GatewayController::ping);
    connect(restartAction, &QAction::triggered, &gateway_,
            &modelharbor::application::GatewayController::restart);
    connect(exitAction, &QAction::triggered, this, &MainWindow::requestExit);
    connect(tray_, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::DoubleClick) {
                    showNormal();
                    activateWindow();
                }
            });
    if (QSystemTrayIcon::isSystemTrayAvailable())
        tray_->show();
}

void MainWindow::applyCurrentTheme() {
    if (qApp != nullptr)
        modelharbor::desktop::applyTheme(*qApp, darkMode_);
}

void MainWindow::requestExit() {
    forceExit_ = true;
    close();
}

} // namespace modelharbor::desktop
