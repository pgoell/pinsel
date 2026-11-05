#pragma once

#include <QMainWindow>
#include <QString>

namespace pinsel {

class Canvas;

class MainWindow : public QMainWindow {
 public:
  explicit MainWindow(QWidget* parent = nullptr);

  // Shows the image at path on the canvas. Returns why it failed, or an empty string.
  QString openImage(const QString& path);

 private:
  void chooseImage();
  void addMenus();
  void addTools();
  void addPanels();

  Canvas* canvas_ = nullptr;
  QMenu* view_menu_ = nullptr;
};

}  // namespace pinsel
