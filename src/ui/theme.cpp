#include "ui/theme.hpp"

#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QStyleFactory>

namespace pinsel {

namespace {

const QColor kPanel(43, 43, 43);
const QColor kBar(37, 37, 38);
const QColor kField(30, 30, 30);
const QColor kText(216, 216, 216);
const QColor kMuted(128, 128, 128);
const QColor kAccent(47, 111, 208);

QPalette darkPalette() {
  QPalette palette;
  palette.setColor(QPalette::Window, kPanel);
  palette.setColor(QPalette::WindowText, kText);
  palette.setColor(QPalette::Base, kField);
  palette.setColor(QPalette::AlternateBase, kPanel);
  palette.setColor(QPalette::Text, kText);
  palette.setColor(QPalette::Button, kPanel);
  palette.setColor(QPalette::ButtonText, kText);
  palette.setColor(QPalette::ToolTipBase, kBar);
  palette.setColor(QPalette::ToolTipText, kText);
  palette.setColor(QPalette::PlaceholderText, kMuted);
  palette.setColor(QPalette::Highlight, kAccent);
  palette.setColor(QPalette::HighlightedText, Qt::white);
  palette.setColor(QPalette::Disabled, QPalette::Text, kMuted);
  palette.setColor(QPalette::Disabled, QPalette::ButtonText, kMuted);
  palette.setColor(QPalette::Disabled, QPalette::WindowText, kMuted);
  return palette;
}

const char* const kStyleSheet = R"(
QMenuBar, QStatusBar, QToolBar#tools, QDockWidget::title { background: #252526; }
QMenuBar::item { padding: 6px 10px; }
QMenuBar::item:selected, QMenu::item:selected { background: #3a3a3a; }
QMenu { background: #252526; border: 1px solid #3a3a3a; padding: 4px; }
QMenu::item { padding: 6px 24px 6px 12px; }
QToolBar#tools { border: none; padding: 6px 4px; spacing: 4px; }
QToolBar#tools QToolButton { border-radius: 6px; padding: 6px; }
QToolBar#tools QToolButton:hover { background: #3a3a3a; }
QToolBar#tools QToolButton:checked { background: #2f6fd0; }
QDockWidget::title { padding: 8px 10px; }
QMainWindow::separator { background: #1b1b1b; width: 1px; height: 1px; }
QListWidget { border: none; }
QSpinBox { border: 1px solid #3a3a3a; border-radius: 4px; padding: 2px 4px; }
)";

}  // namespace

void applyTheme(QApplication& app) {
  QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
  QApplication::setPalette(darkPalette());
  app.setStyleSheet(QString::fromLatin1(kStyleSheet));
}

}  // namespace pinsel
