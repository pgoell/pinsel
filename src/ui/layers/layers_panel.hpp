#pragma once

#include <QWidget>
#include <cstddef>

class QListWidget;
class QToolButton;

namespace pinsel {

class Canvas;

// Lists the canvas's layers, top first, each with a box that shows or hides
// it. Selecting a row makes its layer current, dragging a row moves its layer,
// and buttons below add and delete layers.
class LayersPanel : public QWidget {
 public:
  explicit LayersPanel(Canvas* canvas, QWidget* parent = nullptr);

  // Shows the layers as they are now. Call it after anything changes them.
  void refresh();

 private:
  [[nodiscard]] std::size_t layerAt(int row) const;
  [[nodiscard]] int rowOf(std::size_t layer) const;

  Canvas* canvas_;
  QListWidget* list_;
  QToolButton* add_;
  QToolButton* remove_;
  // Set while refresh changes the list, so the list's signals stay with it.
  bool refreshing_ = false;
};

}  // namespace pinsel
