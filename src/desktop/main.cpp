#include "desktop/main_window.h"

#include <QApplication>

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    modelharbor::desktop::MainWindow window;
    window.show();
    return application.exec();
}
