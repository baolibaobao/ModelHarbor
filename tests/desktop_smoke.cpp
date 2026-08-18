#include "desktop/main_window.h"

#include <QApplication>
#include <QListWidget>

#include <iostream>

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    modelharbor::desktop::MainWindow window(false);

    const auto* navigation = window.findChild<QListWidget*>();
    if (navigation == nullptr || navigation->count() != 8) {
        std::cerr << "desktop navigation shell is incomplete\n";
        return 1;
    }
    if (window.minimumWidth() < 920 || window.minimumHeight() < 560) {
        std::cerr << "desktop minimum size is below the stage 1 baseline\n";
        return 1;
    }

    window.resize(1100, 700);
    window.show();
    application.processEvents();
    if (window.size().width() != 1100 || window.size().height() != 700) {
        std::cerr << "desktop shell did not preserve the target viewport\n";
        return 1;
    }

    std::cout << "ModelHarbor desktop shell smoke OK\n";
    return 0;
}
