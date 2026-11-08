#pragma once

#include <QColor>
#include <QFutureWatcher>
#include <QList>
#include <QMainWindow>
#include <QPolygonF>
#include <QSize>
#include <QString>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>

#include "brush/brush.hpp"
#include "engine/engine.hpp"
#include "tiles/tile_store.hpp"
#include "ui/engine/engine_status.hpp"
#include "ui/engine/prompt_panel.hpp"

class QAction;
class QCloseEvent;

namespace pinsel {

class Canvas;
class LayersPanel;

class MainWindow : public QMainWindow {
 public:
  explicit MainWindow(QWidget* parent = nullptr);

  // Reads the image at path off the UI thread, then shows it on the canvas and
  // calls done with an empty string, or with why it failed. A newer call wins:
  // an older one still loading then never calls done.
  void openImage(const QString& path, std::function<void(const QString& error)> done);
  // Starts an untitled image of size, filled with background. Returns false
  // when there is no memory for it.
  bool newImage(QSize size, const QColor& background);
  // Puts count empty layers above the current one.
  void addLayers(int count);
  // Fills the closed outline, in image pixels, into the mask, as the lasso does.
  void fillMask(const QPolygonF& outline);
  // Loads an SDXL checkpoint into the engine, and calls ended with the failure,
  // or with none once it is loaded. The status bar shows its progress.
  void loadModel(const QString& path,
                 std::function<void(const std::optional<EngineFailure>&)> ended);
  // Inpaints the mask with prompt, as Generate in the prompt panel does, and
  // calls done with whether a layer was added, unless nothing was sent.
  void inpaint(const QString& prompt, const std::function<void(bool added)>& done);

 protected:
  void closeEvent(QCloseEvent* event) override;

 private:
  void chooseImage();
  void askAndOpen();
  void chooseNewImage();
  void chooseModel();
  void cancelJobs();
  void endJob(int job, const std::optional<EngineFailure>& failure, const QString& output);
  bool runInpaint(const InpaintRequest& request, PromptPanel::Ended ended);
  void showImage(TileStore tiles, const QString& path);
  [[nodiscard]] bool isModified() const;
  void updateTitle();
  // Calls proceed at once when nothing is unsaved. Otherwise asks whether to
  // save, discard or cancel, and calls proceed after a save that worked or a
  // discard.
  void whenChangesAreSafe(const std::function<void()>& proceed);
  // Each calls done with whether the image reached the disk.
  void save(const std::function<void(bool saved)>& done);
  void saveAs(const std::function<void(bool saved)>& done);
  // Writes the image as it is now off the UI thread; edits made meanwhile stay
  // unsaved.
  void saveTo(const QString& path, const std::function<void(bool saved)>& done);
  void finishSave();
  void addMenus();
  void addTools();
  void addPanels();

  Canvas* canvas_ = nullptr;
  LayersPanel* layers_ = nullptr;
  EngineStatus* engine_status_ = nullptr;
  PromptPanel* prompt_ = nullptr;
  std::unique_ptr<Engine> engine_;
  struct EngineJob {
    EngineStatus::Job kind = EngineStatus::Job::Load;
    QString model;
    PromptPanel::Ended ended;
  };
  std::map<int, EngineJob> engine_jobs_;
  // The model the engine holds once its queued loads end.
  QString model_;
  BrushSettings brush_;
  int latest_open_ = 0;
  QMenu* view_menu_ = nullptr;
  // Need an image, and wait while a save runs.
  QList<QAction*> save_actions_;
  // Would replace the image, so wait while a save runs.
  QList<QAction*> replace_actions_;
  // Empty for no image, or for one not yet saved.
  QString path_;
  bool has_image_ = false;
  std::uint64_t saved_revision_ = 0;
  struct PendingSave {
    QString path;
    std::uint64_t revision;
    int document;
    std::function<void(bool saved)> done;
  };
  // Set while a save runs.
  std::optional<PendingSave> pending_save_;
  QFutureWatcher<QString>* save_watcher_ = nullptr;
  bool close_after_save_ = false;
  bool close_approved_ = false;
};

}  // namespace pinsel
