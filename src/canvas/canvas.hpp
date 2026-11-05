#pragma once

#include <QImage>
#include <QPointF>
#include <QRhiWidget>
#include <memory>

#include "canvas/view.hpp"

namespace pinsel {

// Draws the image as GPU tiles. The middle mouse button pans and the wheel zooms
// at the cursor.
class Canvas : public QRhiWidget {
 public:
  explicit Canvas(QWidget* parent = nullptr);
  ~Canvas() override;
  Canvas(const Canvas&) = delete;
  Canvas& operator=(const Canvas&) = delete;
  Canvas(Canvas&&) = delete;
  Canvas& operator=(Canvas&&) = delete;

  // QRhiWidget's render(QRhiCommandBuffer*) hides these.
  using QWidget::render;

  void setImage(const QImage& image);
  void zoomIn();
  void zoomOut();
  void fitToWindow();

 protected:
  void initialize(QRhiCommandBuffer* commands) override;
  void render(QRhiCommandBuffer* commands) override;
  void releaseResources() override;

  void resizeEvent(QResizeEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;

 private:
  struct Gpu;

  void zoomAtCentre(double factor);

  std::unique_ptr<Gpu> gpu_;
  QImage image_;
  bool image_changed_ = false;
  View view_;
  QPointF drag_from_;
};

}  // namespace pinsel
