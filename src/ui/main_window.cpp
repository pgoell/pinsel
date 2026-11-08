#include "ui/main_window.hpp"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QColorDialog>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QIcon>
#include <QImageReader>
#include <QKeySequence>
#include <QMenuBar>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStringList>
#include <QToolBar>
#include <QtConcurrentRun>
#include <algorithm>
#include <array>
#include <optional>
#include <tuple>
#include <utility>

#include "canvas/canvas.hpp"
#include "tiles/tile_store.hpp"
#include "ui/dialogs/new_image_dialog.hpp"
#include "ui/engine/engine_status.hpp"
#include "ui/layers/layers_panel.hpp"

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

ImageFormat formatOfFilter(const QString& filter) {
  for (const ImageFormat& format : kImageFormats) {
    if (format.filter == filter) {
      return format;
    }
  }
  return kImageFormats.front();
}

ImageFormat firstFormat(bool layered) {
  for (const ImageFormat& format : kImageFormats) {
    if (format.layered == layered) {
      return format;
    }
  }
  return kImageFormats.front();
}

}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      canvas_(new Canvas(this)),
      engine_status_(new EngineStatus(this)),
      save_watcher_(new QFutureWatcher<QString>(this)) {
  connect(save_watcher_, &QFutureWatcher<QString>::finished, this, &MainWindow::finishSave);
  resize(1280, 800);
  setCentralWidget(canvas_);
  statusBar()->addPermanentWidget(engine_status_, 1);
  addMenus();
  addTools();
  addPanels();
  updateTitle();
}

void MainWindow::addMenus() {
  QMenu* file = menuBar()->addMenu(QStringLiteral("&File"));
  replace_actions_ = {
      file->addAction(QStringLiteral("&New..."), QKeySequence::New, this,
                      &MainWindow::chooseNewImage),
      file->addAction(QStringLiteral("&Open..."), QKeySequence::Open, this,
                      &MainWindow::chooseImage),
  };
  save_actions_ = {
      file->addAction(QStringLiteral("&Save"), QKeySequence::Save, this,
                      [this] { save([](bool) {}); }),
      file->addAction(QStringLiteral("Save &As..."), QKeySequence::SaveAs, this,
                      [this] { saveAs([](bool) {}); }),
  };
  file->addSeparator();
  file->addAction(QStringLiteral("&Quit"), QKeySequence::Quit, this, &QWidget::close);

  QMenu* edit = menuBar()->addMenu(QStringLiteral("&Edit"));
  QAction* undo =
      edit->addAction(QStringLiteral("&Undo"), QKeySequence::Undo, canvas_, &Canvas::undo);
  QAction* redo =
      edit->addAction(QStringLiteral("&Redo"), QKeySequence::Redo, canvas_, &Canvas::redo);
  edit->addSeparator();
  edit->addAction(QStringLiteral("Clear &Mask"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M),
                  canvas_, &Canvas::eraseMask);
  const auto updateHistory = [this, undo, redo] {
    undo->setEnabled(canvas_->tiles().canUndo());
    redo->setEnabled(canvas_->tiles().canRedo());
    if (layers_ != nullptr) {
      layers_->refresh();
    }
    updateTitle();
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

  QMenu* diffusion = menuBar()->addMenu(QStringLiteral("&Diffusion"));
  diffusion->addAction(QStringLiteral("&Load Model..."), this, &MainWindow::chooseModel);
  diffusion->addAction(QStringLiteral("&Cancel"), engine_status_, &EngineStatus::cancel);
}

void MainWindow::loadModel(const QString& path,
                           std::function<void(const std::optional<EngineFailure>&)> ended) {
  engine_status_->loadModel(path, std::move(ended));
}

void MainWindow::chooseModel() {
  const QString path =
      QFileDialog::getOpenFileName(this, QStringLiteral("Load Model"), QString(),
                                   QStringLiteral("SDXL checkpoints (*.safetensors *.ckpt)"));
  if (!path.isEmpty()) {
    loadModel(path, {});
  }
}

void MainWindow::openImage(const QString& path, std::function<void(const QString& error)> done) {
  const int id = ++latest_open_;
  auto* watcher = new QFutureWatcher<ReadImage>(this);
  connect(watcher, &QFutureWatcher<ReadImage>::finished, this,
          [this, watcher, id, path, done = std::move(done)] {
            ReadImage read = watcher->result();
            watcher->deleteLater();
            if (id != latest_open_) {
              return;
            }
            if (read.error.isEmpty()) {
              showImage(std::move(read.tiles), path);
            }
            done(read.error);
          });
  watcher->setFuture(QtConcurrent::run(readImage, path));
}

bool MainWindow::newImage(QSize size, const QColor& background) {
  QImage image(size, TileStore::kFormat);
  if (image.isNull()) {
    return false;
  }
  image.fill(background);
  TileStore tiles(image);
  if (tiles.size().isEmpty()) {
    return false;
  }
  ++latest_open_;
  showImage(std::move(tiles), QString());
  return true;
}

void MainWindow::addLayers(int count) {
  canvas_->edit([count](TileStore& tiles) {
    for (int added = 0; added < count; ++added) {
      tiles.addLayer();
    }
  });
}

void MainWindow::fillMask(const QPolygonF& outline) {
  canvas_->edit([&outline](TileStore& tiles) { pinsel::fillMask(tiles, outline, 1.0, false); });
}

void MainWindow::showImage(TileStore tiles, const QString& path) {
  canvas_->setImage(std::move(tiles));
  path_ = path;
  has_image_ = true;
  saved_revision_ = canvas_->tiles().revision();
  updateTitle();
}

bool MainWindow::isModified() const {
  return has_image_ && canvas_->tiles().revision() != saved_revision_;
}

void MainWindow::updateTitle() {
  const QString app = QApplication::applicationName();
  if (!has_image_) {
    setWindowTitle(app);
  } else {
    const QString name = path_.isEmpty() ? QStringLiteral("Untitled") : QFileInfo(path_).fileName();
    setWindowTitle(QStringLiteral("%1[*] - %2").arg(name, app));
  }
  setWindowModified(isModified());
  for (QAction* action : std::as_const(save_actions_)) {
    action->setEnabled(has_image_ && !pending_save_);
  }
  for (QAction* action : std::as_const(replace_actions_)) {
    action->setEnabled(!pending_save_);
  }
}

void MainWindow::whenChangesAreSafe(const std::function<void()>& proceed) {
  if (!isModified()) {
    proceed();
    return;
  }
  const QString name = path_.isEmpty() ? QStringLiteral("Untitled") : QFileInfo(path_).fileName();
  const auto answer = QMessageBox::warning(
      this, QStringLiteral("Unsaved Changes"),
      QStringLiteral("Save the changes to %1? Without saving, they are lost.").arg(name),
      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
  if (answer == QMessageBox::Discard) {
    proceed();
  } else if (answer == QMessageBox::Save) {
    save([proceed](bool saved) {
      if (saved) {
        proceed();
      }
    });
  }
}

void MainWindow::save(const std::function<void(bool saved)>& done) {
  // Save never writes into a file of a type pinsel cannot write, such as JPEG.
  if (imageFormatOf(path_)) {
    saveTo(path_, done);
  } else {
    saveAs(done);
  }
}

void MainWindow::saveAs(const std::function<void(bool saved)>& done) {
  // A PNG keeps one layer and no mask, so an image with more starts as
  // OpenRaster.
  const TileStore& tiles = canvas_->tiles();
  const QFileInfo current(path_);
  const std::optional<ImageFormat> current_format = imageFormatOf(path_);
  const ImageFormat format = firstFormat(tiles.layerCount() > 1 || tiles.hasMask() ||
                                         (current_format && current_format->layered));
  const QString base =
      path_.isEmpty() ? QDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation))
                            .filePath(QStringLiteral("Untitled"))
                      : current.dir().filePath(current.completeBaseName());
  QStringList filters;
  for (const ImageFormat& each : kImageFormats) {
    filters.append(QString(each.filter));
  }
  QFileDialog dialog(this, QStringLiteral("Save Image"), base + u'.' + format.suffix);
  dialog.setNameFilters(filters);
  dialog.selectNameFilter(QString(format.filter));
  dialog.setAcceptMode(QFileDialog::AcceptSave);
  // Adds the suffix before the dialog's own overwrite check, so the check sees
  // the file that will be written.
  dialog.setDefaultSuffix(QString(format.suffix));
  connect(&dialog, &QFileDialog::filterSelected, &dialog, [&dialog](const QString& filter) {
    dialog.setDefaultSuffix(QString(formatOfFilter(filter).suffix));
  });
  if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) {
    done(false);
    return;
  }
  QString path = dialog.selectedFiles().constFirst();
  if (!imageFormatOf(path)) {
    // A name with another suffix, such as photo.jpg, gets the chosen type's
    // suffix on top, which the dialog never checked.
    path += u'.' + formatOfFilter(dialog.selectedNameFilter()).suffix;
    if (QFileInfo::exists(path) &&
        QMessageBox::question(
            this, QStringLiteral("Save Image"),
            QStringLiteral("%1 already exists. Replace it?").arg(QFileInfo(path).fileName())) !=
            QMessageBox::Yes) {
      done(false);
      return;
    }
  }
  saveTo(path, done);
}

void MainWindow::saveTo(const QString& path, const std::function<void(bool saved)>& done) {
  if (!has_image_ || pending_save_) {
    done(false);
    return;
  }
  pending_save_ = PendingSave{
      .path = path,
      .revision = canvas_->tiles().revision(),
      .document = latest_open_,
      .done = done,
  };
  save_watcher_->setFuture(
      QtConcurrent::run([tiles = canvas_->tiles(), path] { return writeImage(tiles, path); }));
  updateTitle();
}

void MainWindow::finishSave() {
  if (!pending_save_) {
    return;
  }
  const PendingSave saved = std::move(*pending_save_);
  pending_save_.reset();
  const QString error = save_watcher_->result();
  if (error.isEmpty() && saved.document == latest_open_) {
    path_ = saved.path;
    saved_revision_ = saved.revision;
  }
  updateTitle();
  if (!error.isEmpty()) {
    QMessageBox::warning(this, QStringLiteral("Save Image"),
                         QStringLiteral("Could not save %1: %2").arg(saved.path, error));
  }
  saved.done(error.isEmpty());
  if (std::exchange(close_after_save_, false)) {
    close();
  }
}

void MainWindow::closeEvent(QCloseEvent* event) {
  if (close_approved_) {
    event->accept();
    return;
  }
  event->ignore();
  if (pending_save_) {
    close_after_save_ = true;
    return;
  }
  whenChangesAreSafe([this] {
    close_approved_ = true;
    close();
  });
}

void MainWindow::chooseNewImage() {
  const std::optional<NewImage> chosen = askNewImage(this);
  if (!chosen) {
    return;
  }
  whenChangesAreSafe([this, chosen] {
    if (!newImage(chosen->size, chosen->background)) {
      QMessageBox::warning(this, QStringLiteral("New Image"),
                           QStringLiteral("Not enough memory for a %1 x %2 image.")
                               .arg(chosen->size.width())
                               .arg(chosen->size.height()));
    }
  });
}

void MainWindow::chooseImage() {
  whenChangesAreSafe([this] { askAndOpen(); });
}

void MainWindow::askAndOpen() {
  QStringList patterns;
  for (const QByteArray& format : QImageReader::supportedImageFormats()) {
    patterns.append(QStringLiteral("*.") + QString::fromLatin1(format));
  }
  for (const ImageFormat& format : kImageFormats) {
    patterns.append(QStringLiteral("*.") + format.suffix);
  }
  patterns.removeDuplicates();
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
      std::tuple{"Lasso", "lasso", Qt::Key_L, Canvas::Tool::Lasso},
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
  layers_ = new LayersPanel(canvas_, layers);
  layers->setWidget(layers_);
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
