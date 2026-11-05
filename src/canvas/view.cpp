#include "canvas/view.hpp"

#include <algorithm>

namespace pinsel {

void View::setImageSize(QSize size) {
  image_ = size;
  fit();
}

void View::setViewportSize(QSizeF size) {
  viewport_ = size;
  if (fitted_) {
    fit();
  }
}

void View::fit() {
  fitted_ = true;
  if (image_.isEmpty() || viewport_.isEmpty()) {
    return;
  }
  scale_ = std::min(viewport_.width() / image_.width(), viewport_.height() / image_.height());
  const QSizeF shown = QSizeF(image_) * scale_;
  offset_ = QPointF((viewport_.width() - shown.width()) / 2.0,
                    (viewport_.height() - shown.height()) / 2.0);
}

void View::panBy(QPointF delta) {
  fitted_ = false;
  offset_ += delta;
}

void View::zoomAt(QPointF anchor, double factor) {
  fitted_ = false;
  const double scale = std::clamp(scale_ * factor, kMinScale, kMaxScale);
  offset_ = anchor - (anchor - offset_) * (scale / scale_);
  scale_ = scale;
}

QPointF View::toImage(QPointF viewPoint) const {
  return (viewPoint - offset_) / scale_;
}

QRectF View::visibleImageRect() const {
  return {toImage(QPointF(0.0, 0.0)), toImage(QPointF(viewport_.width(), viewport_.height()))};
}

}  // namespace pinsel
