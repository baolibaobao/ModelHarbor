#include "desktop/theme.h"

#include <QApplication>
#include <QPalette>

namespace modelharbor::desktop {

void applyTheme(QApplication& application, bool darkMode) {
    QPalette palette;
    if (darkMode) {
        palette.setColor(QPalette::Window, QColor(QStringLiteral("#1f252b")));
        palette.setColor(QPalette::Base, QColor(QStringLiteral("#252c33")));
        palette.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#2b333b")));
        palette.setColor(QPalette::Text, QColor(QStringLiteral("#edf2f4")));
        palette.setColor(QPalette::WindowText, QColor(QStringLiteral("#edf2f4")));
        palette.setColor(QPalette::Button, QColor(QStringLiteral("#303a43")));
        palette.setColor(QPalette::ButtonText, QColor(QStringLiteral("#edf2f4")));
        palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#2a9d8f")));
        palette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#ffffff")));
    } else {
        palette.setColor(QPalette::Window, QColor(QStringLiteral("#f5f7f8")));
        palette.setColor(QPalette::Base, QColor(QStringLiteral("#ffffff")));
        palette.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#eef2f3")));
        palette.setColor(QPalette::Text, QColor(QStringLiteral("#1f2933")));
        palette.setColor(QPalette::WindowText, QColor(QStringLiteral("#1f2933")));
        palette.setColor(QPalette::Button, QColor(QStringLiteral("#e8eef0")));
        palette.setColor(QPalette::ButtonText, QColor(QStringLiteral("#1f2933")));
        palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#2a9d8f")));
        palette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#ffffff")));
    }
    application.setPalette(palette);
    application.setStyleSheet(
        QStringLiteral("QMainWindow { background: palette(window); }"
                       "QListWidget { border: 0; padding: 6px; background: palette(window); }"
                       "QListWidget::item { padding: 9px 10px; border-radius: 5px; }"
                       "QListWidget::item:selected { background: #2a9d8f; color: white; }"
                       "QToolButton, QPushButton { min-height: 30px; padding: 0 10px; border: 1px "
                       "solid palette(mid); border-radius: 5px; }"
                       "QToolButton:hover, QPushButton:hover { border-color: #2a9d8f; }"
                       "QLabel#pageTitle { font-size: 18px; font-weight: 600; }"
                       "QLabel#muted { color: #64727c; }"));
}

} // namespace modelharbor::desktop
