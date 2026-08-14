#include <QCoreApplication>
#include <QString>

#include <iostream>

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    const QString version = QString::fromLatin1(qVersion());

    if (version != QStringLiteral("6.11.1")) {
        std::cerr << "Unexpected Qt version: " << version.toStdString() << '\n';
        return 1;
    }

    std::cout << "ModelHarbor toolchain smoke OK; Qt " << version.toStdString() << '\n';
    return 0;
}
