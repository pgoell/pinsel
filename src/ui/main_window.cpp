#include "ui/main_window.hpp"

#include <QActionGroup>
#include <QApplication>
#include <QDockWidget>
#include <QFormLayout>
#include <QKeySequence>
#include <QListWidget>
#include <QMenuBar>
#include <QSlider>
#include <QStatusBar>
#include <QToolBar>
#include <array>
#include <utility>

#include "canvas/canvas.hpp"

namespace pinsel {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  setWindowTitle(QApplication::applicationName());
  resize(1280, 800);
  setCentralWidget(new Canvas(this));
  addMenus();
  addTools();
  addPanels();
  statusBar()->showMessage(QStringLiteral("No image open"));
}

void MainWindow::addMenus() {
  // Actions stay disabled until the feature behind them exists.
  QMenu* file = menuBar()->addMenu(QStringLiteral("&File"));
  file->addAction(QStringLiteral("&Open..."), QKeySequence::Open)->setEnabled(false);
  file->addAction(QStringLiteral("&Save"), QKeySequence::Save)->setEnabled(false);
  file->addAction(QStringLiteral("Save &As..."), QKeySequence::SaveAs)->setEnabled(false);
  file->addSeparator();
  file->addAction(QStringLiteral("&Quit"), QKeySequence::Quit, this, &QWidget::close);

  QMenu* edit = menuBar()->addMenu(QStringLiteral("&Edit"));
  edit->addAction(QStringLiteral("&Undo"), QKeySequence::Undo)->setEnabled(false);
  edit->addAction(QStringLiteral("&Redo"), QKeySequence::Redo)->setEnabled(false);

  view_menu_ = menuBar()->addMenu(QStringLiteral("&View"));
  QMenu* view = view_menu_;
  view->addAction(QStringLiteral("Zoom &In"), QKeySequence::ZoomIn)->setEnabled(false);
  view->addAction(QStringLiteral("Zoom &Out"), QKeySequence::ZoomOut)->setEnabled(false);
  view->addAction(QStringLiteral("&Fit to Window"), QKeySequence(Qt::CTRL | Qt::Key_0))
      ->setEnabled(false);
  view->addSeparator();
}

void MainWindow::addTools() {
  QToolBar* tools = addToolBar(QStringLiteral("Tools"));
  addToolBar(Qt::LeftToolBarArea, tools);
  tools->setMovable(false);
  auto* group = new QActionGroup(tools);
  const std::array entries{
      std::pair{"Brush", Qt::Key_B},
      std::pair{"Eraser", Qt::Key_E},
      std::pair{"Mask", Qt::Key_M},
      std::pair{"Pan", Qt::Key_H},
  };
  for (const auto& [name, key] : entries) {
    QAction* action = tools->addAction(QString::fromLatin1(name));
    action->setShortcut(key);
    action->setCheckable(true);
    group->addAction(action);
  }
  group->actions().constFirst()->setChecked(true);
}

void MainWindow::addPanels() {
  auto* layers = new QDockWidget(QStringLiteral("Layers"), this);
  layers->setWidget(new QListWidget(layers));
  addDockWidget(Qt::RightDockWidgetArea, layers);

  auto* brush = new QDockWidget(QStringLiteral("Brush"), this);
  auto* settings = new QWidget(brush);
  auto* form = new QFormLayout(settings);
  auto* size = new QSlider(Qt::Horizontal, settings);
  size->setRange(1, 500);
  size->setValue(40);
  form->addRow(QStringLiteral("Size"), size);
  auto* opacity = new QSlider(Qt::Horizontal, settings);
  opacity->setRange(0, 100);
  opacity->setValue(100);
  form->addRow(QStringLiteral("Opacity"), opacity);
  brush->setWidget(settings);
  addDockWidget(Qt::RightDockWidgetArea, brush);

  view_menu_->addAction(layers->toggleViewAction());
  view_menu_->addAction(brush->toggleViewAction());
}

}  // namespace pinsel
