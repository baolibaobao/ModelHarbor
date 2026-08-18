#pragma once

#include "application/gateway_controller.h"

#include <QMainWindow>

class QCloseEvent;
class QLabel;
class QListWidget;
class QStackedWidget;
class QSystemTrayIcon;
class QToolButton;

namespace modelharbor::desktop {

class MainWindow final : public QMainWindow {
  public:
    explicit MainWindow(bool startGateway = true, QWidget* parent = nullptr);
    ~MainWindow() override = default;

  protected:
    void closeEvent(QCloseEvent* event) override;

  private:
    void setupTray();
    void applyCurrentTheme();
    void requestExit();

    QLabel* pageTitle_ = nullptr;
    QLabel* gatewayStatus_ = nullptr;
    QLabel* gatewayDetail_ = nullptr;
    QListWidget* navigation_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    QToolButton* themeButton_ = nullptr;
    QSystemTrayIcon* tray_ = nullptr;
    modelharbor::application::GatewayController gateway_;
    bool darkMode_ = false;
    bool forceExit_ = false;
    bool startGateway_ = true;
};

} // namespace modelharbor::desktop
