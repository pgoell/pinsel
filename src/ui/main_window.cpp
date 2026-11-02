#include "ui/main_window.hpp"

#include <QApplication>
#include <QStatusBar>

namespace pinsel {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  setWindowTitle(QApplication::applicationName());
  resize(1280, 800);
  statusBar()->showMessage(QStringLiteral("No image open"));
}

}  // namespace pinsel
