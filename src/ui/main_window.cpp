#include "ui/main_window.hpp"

#include <QActionGroup>
#include <QApplication>
#include <QColorDialog>
#include <QDockWidget>
#include <QFileDialog>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QIcon>
#include <QImageReader>
#include <QKeySequence>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QToolBar>
#include <QtConcurrentRun>
#include <array>
#include <tuple>
#include <utility>

#include "canvas/canvas.hpp"
#include "tiles/tile_store.hpp"

namespace pinsel {

namespace {

struct Range {
  int min;
  int max;
  int value;
};

// A slider with a number field beside it, kept in step both ways. Calls changed
// with each new value, and once with the first.
void addSliderRow(QFormLayout* form, const QString& label, Range range, const QString& suffix,
                  const std::function<void(int)>& changed) {
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
  QObject::connect(slider, &QSlider::valueChanged, row, changed);
  slider->setValue(range.value);
  changed(slider->value());
  layout->addWidget(slider, 1);
  layout->addWidget(field);
  form->addRow(label, row);
}

struct Loaded {
  TileStore tiles;
  QString error;
};

// Runs on a pool thread, so cutting the image into tiles never stalls the UI.
Loaded load(const QString& path) {
  QImageReader reader(path);
  reader.setAutoTransform(true);
  const QImage image = reader.read();
  if (image.isNull()) {
    return {.tiles = {}, .error = reader.errorString()};
  }
  TileStore tiles(image);
  if (tiles.size().isEmpty()) {
    return {.tiles = {}, .error = QStringLiteral("Not enough memory to convert the image")};
  }
  return {.tiles = std::move(tiles), .error = {}};
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), canvas_(new Canvas(this)) {
  setWindowTitle(QApplication::applicationName());
  resize(1280, 800);
  setCentralWidget(canvas_);
  addMenus();
  addTools();
  addPanels();
}

void MainWindow::addMenus() {
  // Actions stay disabled until the feature behind them exists.
  QMenu* file = menuBar()->addMenu(QStringLiteral("&File"));
  file->addAction(QStringLiteral("&Open..."), QKeySequence::Open, this, &MainWindow::chooseImage);
  file->addAction(QStringLiteral("&Save"), QKeySequence::Save)->setEnabled(false);
  file->addAction(QStringLiteral("Save &As..."), QKeySequence::SaveAs)->setEnabled(false);
  file->addSeparator();
  file->addAction(QStringLiteral("&Quit"), QKeySequence::Quit, this, &QWidget::close);

  QMenu* edit = menuBar()->addMenu(QStringLiteral("&Edit"));
  QAction* undo =
      edit->addAction(QStringLiteral("&Undo"), QKeySequence::Undo, canvas_, &Canvas::undo);
  QAction* redo =
      edit->addAction(QStringLiteral("&Redo"), QKeySequence::Redo, canvas_, &Canvas::redo);
  const auto updateHistory = [this, undo, redo] {
    undo->setEnabled(canvas_->tiles().canUndo());
    redo->setEnabled(canvas_->tiles().canRedo());
  };
  canvas_->setHistoryChanged(updateHistory);
  updateHistory();

  view_menu_ = menuBar()->addMenu(QStringLiteral("&View"));
  view_menu_->addAction(QStringLiteral("Zoom &In"), QKeySequence::ZoomIn, canvas_, &Canvas::zoomIn);
  view_menu_->addAction(QStringLiteral("Zoom &Out"), QKeySequence::ZoomOut, canvas_,
                        &Canvas::zoomOut);
  view_menu_->addAction(QStringLiteral("&Fit to Window"), QKeySequence(Qt::CTRL | Qt::Key_0),
                        canvas_, &Canvas::fitToWindow);
  view_menu_->addSeparator();
}

void MainWindow::openImage(const QString& path, std::function<void(const QString& error)> done) {
  const int id = ++latest_open_;
  auto* watcher = new QFutureWatcher<Loaded>(this);
  connect(watcher, &QFutureWatcher<Loaded>::finished, this,
          [this, watcher, id, path, done = std::move(done)] {
            Loaded loaded = watcher->result();
            watcher->deleteLater();
            if (id != latest_open_) {
              return;
            }
            if (loaded.error.isEmpty()) {
              canvas_->setImage(std::move(loaded.tiles));
              setWindowFilePath(path);
            }
            done(loaded.error);
          });
  watcher->setFuture(QtConcurrent::run(load, path));
}

void MainWindow::chooseImage() {
  QStringList patterns;
  for (const QByteArray& format : QImageReader::supportedImageFormats()) {
    patterns.append(QStringLiteral("*.") + QString::fromLatin1(format));
  }
  const QString path = QFileDialog::getOpenFileName(
      this, QStringLiteral("Open Image"), QString(),
      QStringLiteral("Images (%1)").arg(patterns.join(QLatin1Char(' '))));
  if (path.isEmpty()) {
    return;
  }
  openImage(path, [this, path](const QString& error) {
    if (!error.isEmpty()) {
      QMessageBox::warning(this, QStringLiteral("Open Image"),
                           QStringLiteral("Could not open %1: %2").arg(path, error));
    }
  });
}

void MainWindow::addTools() {
  auto* tools = new QToolBar(QStringLiteral("Tools"), this);
  tools->setObjectName(QStringLiteral("tools"));
  tools->setMovable(false);
  tools->setIconSize(QSize(20, 20));
  addToolBar(Qt::LeftToolBarArea, tools);
  auto* group = new QActionGroup(tools);
  const std::array entries{
      std::tuple{"Brush", "brush", Qt::Key_B, Canvas::Tool::Brush},
      std::tuple{"Eraser", "eraser", Qt::Key_E, Canvas::Tool::Eraser},
      std::tuple{"Mask", "square-dashed", Qt::Key_M, Canvas::Tool::Mask},
      std::tuple{"Pan", "hand", Qt::Key_H, Canvas::Tool::Pan},
  };
  for (const auto& [name, icon, key, tool] : entries) {
    QAction* action =
        tools->addAction(QIcon(QStringLiteral(":/icons/%1.svg").arg(QString::fromLatin1(icon))),
                         QString::fromLatin1(name));
    action->setShortcut(key);
    action->setToolTip(
        QStringLiteral("%1 (%2)").arg(QString::fromLatin1(name), QKeySequence(key).toString()));
    action->setCheckable(true);
    connect(action, &QAction::triggered, canvas_, [this, tool] { canvas_->setTool(tool); });
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
               QStringLiteral(" px"), [this](int value) {
                 brush_.size = value;
                 canvas_->setBrush(brush_);
               });
  addSliderRow(form, QStringLiteral("Opacity"), {.min = 1, .max = 100, .value = 100},
               QStringLiteral(" %"), [this](int value) {
                 brush_.opacity = value / 100.0;
                 canvas_->setBrush(brush_);
               });
  auto* color = new QPushButton(settings);
  const auto showColor = [color](const QColor& value) {
    QPixmap swatch(16, 16);
    swatch.fill(value);
    color->setIcon(QIcon(swatch));
    color->setText(value.name());
  };
  showColor(brush_.color);
  connect(color, &QPushButton::clicked, this, [this, showColor] {
    const QColor chosen = QColorDialog::getColor(brush_.color, this, QStringLiteral("Brush Color"));
    if (chosen.isValid()) {
      brush_.color = chosen;
      canvas_->setBrush(brush_);
      showColor(chosen);
    }
  });
  form->addRow(QStringLiteral("Color"), color);
  brush->setWidget(settings);
  addDockWidget(Qt::RightDockWidgetArea, brush);

  view_menu_->addAction(layers->toggleViewAction());
  view_menu_->addAction(brush->toggleViewAction());
}

}  // namespace pinsel
