#pragma once

#include <QMainWindow>
#include <QString>
#include <functional>

namespace pinsel {

class Canvas;

class MainWindow : public QMainWindow {
 public:
  explicit MainWindow(QWidget* parent = nullptr);

  // Reads the image at path off the UI thread, then shows it on the canvas and
  // calls done with an empty string, or with why it failed. A newer call wins:
  // an older one still loading then never calls done.
  void openImage(const QString& path, std::function<void(const QString& error)> done);

 private:
  void chooseImage();
  void addMenus();
  void addTools();
  void addPanels();

  Canvas* canvas_ = nullptr;
  int latest_open_ = 0;
  QMenu* view_menu_ = nullptr;
};

}  // namespace pinsel
