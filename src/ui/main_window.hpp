#pragma once

#include <QMainWindow>

namespace pinsel {

class MainWindow : public QMainWindow {
 public:
  explicit MainWindow(QWidget* parent = nullptr);

 private:
  void addMenus();
  void addTools();
  void addPanels();

  QMenu* view_menu_ = nullptr;
};

}  // namespace pinsel
