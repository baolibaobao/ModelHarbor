#include "application/gateway_controller.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>

#include <iostream>

namespace {

using Controller = modelharbor::application::GatewayController;

bool waitForState(Controller& controller, Controller::State expected, int timeoutMs) {
    if (controller.state() == expected) {
        return true;
    }
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&controller, &Controller::stateChanged, &loop,
                     [&loop, expected](Controller::State state, const QString&) {
                         if (state == expected) {
                             loop.quit();
                         }
                     });
    timeout.start(timeoutMs);
    loop.exec();
    return controller.state() == expected;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    const QString socketName =
        QStringLiteral("modelharbor-controller-test-%1").arg(application.applicationPid());
    Controller controller(socketName, QStringLiteral(MODELHARBOR_GATEWAY_EXECUTABLE));
    controller.start();
    if (!waitForState(controller, Controller::State::Ready, 5000) ||
        controller.gatewayVersion().isEmpty()) {
        std::cerr << "controller did not reach ready\n";
        return 1;
    }

    controller.restart();
    if (!waitForState(controller, Controller::State::Ready, 5000)) {
        std::cerr << "controller restart/reconnect failed\n";
        return 2;
    }

    if (!controller.shutdownAndWait(2000) || controller.state() != Controller::State::Stopped) {
        std::cerr << "controller shutdown failed\n";
        return 3;
    }

    std::cout << "ModelHarbor gateway controller integration OK\n";
    return 0;
}
