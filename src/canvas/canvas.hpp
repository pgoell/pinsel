#pragma once

#include <QWidget>

namespace pinsel {

// A placeholder until milestone 1, when this becomes a QRhiWidget that draws the image as GPU tiles
// and pans and zooms a 4K image at 60 fps or more.
class Canvas : public QWidget {
 public:
  explicit Canvas(QWidget* parent = nullptr);

 protected:
  void paintEvent(QPaintEvent* event) override;
};

}  // namespace pinsel
