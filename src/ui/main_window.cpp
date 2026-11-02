#include "ui/main_window.hpp"

#include <QActionGroup>
#include <QApplication>
#include <QDockWidget>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
#include <QListWidget>
#include <QMenuBar>
#include <QSlider>
#include <QSpinBox>
#include <QToolBar>
#include <array>
#include <tuple>

#include "canvas/canvas.hpp"

namespace pinsel {

namespace {

struct Range {
  int min;
  int max;
  int value;
};

// A slider with a number field beside it, kept in step both ways.
void addSliderRow(QFormLayout* form, const QString& label, Range range, const QString& suffix) {
  auto* row = new QWidget(form->parentWidget());
  auto* layout = new QHBoxLayout(row);
  layout->setContentsMargins(0, 0, 0, 0);
  auto* slider = new QSlider(Qt::Horizontal, row);
  auto* field = new QSpinBox(row);
  slider->setRange(range.min, range.max);
  field->setRange(range.min, range.max);
  field->setSuffix(suffix);
  field->setButtonSymbols(QAbstractSpinBox::NoButtons);
  field->setAlignment(Qt::AlignRight);
  field->setFixedWidth(64);
  QObject::connect(slider, &QSlider::valueChanged, field, &QSpinBox::setValue);
  QObject::connect(field, &QSpinBox::valueChanged, slider, &QSlider::setValue);
  slider->setValue(range.value);
  layout->addWidget(slider, 1);
  layout->addWidget(field);
  form->addRow(label, row);
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  setWindowTitle(QApplication::applicationName());
  resize(1280, 800);
  setCentralWidget(new Canvas(this));
  addMenus();
  addTools();
  addPanels();
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
  view_menu_->addAction(QStringLiteral("Zoom &In"), QKeySequence::ZoomIn)->setEnabled(false);
  view_menu_->addAction(QStringLiteral("Zoom &Out"), QKeySequence::ZoomOut)->setEnabled(false);
  view_menu_->addAction(QStringLiteral("&Fit to Window"), QKeySequence(Qt::CTRL | Qt::Key_0))
      ->setEnabled(false);
  view_menu_->addSeparator();
}

void MainWindow::addTools() {
  auto* tools = new QToolBar(QStringLiteral("Tools"), this);
  tools->setObjectName(QStringLiteral("tools"));
  tools->setMovable(false);
  tools->setIconSize(QSize(20, 20));
  addToolBar(Qt::LeftToolBarArea, tools);
  auto* group = new QActionGroup(tools);
  const std::array entries{
      std::tuple{"Brush", "brush", Qt::Key_B},
      std::tuple{"Eraser", "eraser", Qt::Key_E},
      std::tuple{"Mask", "square-dashed", Qt::Key_M},
      std::tuple{"Pan", "hand", Qt::Key_H},
  };
  for (const auto& [name, icon, key] : entries) {
    QAction* action =
        tools->addAction(QIcon(QStringLiteral(":/icons/%1.svg").arg(QString::fromLatin1(icon))),
                         QString::fromLatin1(name));
    action->setShortcut(key);
    action->setToolTip(
        QStringLiteral("%1 (%2)").arg(QString::fromLatin1(name), QKeySequence(key).toString()));
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
  addSliderRow(form, QStringLiteral("Size"), {.min = 1, .max = 500, .value = 40},
               QStringLiteral(" px"));
  addSliderRow(form, QStringLiteral("Opacity"), {.min = 0, .max = 100, .value = 100},
               QStringLiteral(" %"));
  brush->setWidget(settings);
  addDockWidget(Qt::RightDockWidgetArea, brush);

  view_menu_->addAction(layers->toggleViewAction());
  view_menu_->addAction(brush->toggleViewAction());
}

}  // namespace pinsel
