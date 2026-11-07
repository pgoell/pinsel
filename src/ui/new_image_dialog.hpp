#pragma once

#include <QColor>
#include <QSize>
#include <optional>

class QWidget;

namespace pinsel {

struct NewImage {
  QSize size;
  QColor background;
};

// Asks for the size and background of a new image, starting at 1024 x 1024,
// the size SDXL generates, on white. Empty when the person cancels.
std::optional<NewImage> askNewImage(QWidget* parent);

}  // namespace pinsel
