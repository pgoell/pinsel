#pragma once

#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QSizeF>

namespace pinsel {

// Where the image sits in the widget: a scale and the widget position of the
// image's top left corner. Until the user pans or zooms, the view keeps the
// image fitted to the widget, also when the widget resizes. Fitting stays in the
// scale range too, so a tiny image is not blown up past it.
class View {
 public:
  static constexpr double kMinScale = 1.0 / 32.0;
  static constexpr double kMaxScale = 64.0;

  void setImageSize(QSize size);
  void setViewportSize(QSizeF size);

  void fit();
  void panBy(QPointF delta);
  // Scales by factor, clamped to the scale range, keeping the image point under
  // anchor in place.
  void zoomAt(QPointF anchor, double factor);

  [[nodiscard]] double scale() const {
    return scale_;
  }
  [[nodiscard]] QPointF offset() const {
    return offset_;
  }
  [[nodiscard]] QPointF toImage(QPointF viewPoint) const;
  [[nodiscard]] QRectF visibleImageRect() const;

 private:
  QSize image_;
  QSizeF viewport_;
  double scale_ = 1.0;
  QPointF offset_;
  bool fitted_ = true;
};

}  // namespace pinsel
