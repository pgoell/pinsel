#pragma once

#include <QPointF>
#include <QRhiWidget>
#include <functional>
#include <memory>

#include "canvas/view.hpp"
#include "tiles/tile_store.hpp"

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

  void setImage(TileStore tiles);
  // Calls change, then redraws the tiles it marked dirty.
  void edit(const std::function<void(TileStore&)>& change);
  void zoomIn();
  void zoomOut();
  void fitToWindow();

  // GPU time of the frame before the latest render() call, in seconds. Zero
  // unless the QRhi records timestamps, which QSG_RHI_PROFILE=1 turns on.
  [[nodiscard]] double previousGpuFrameSeconds() const {
    return previous_gpu_seconds_;
  }

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
  TileStore tiles_;
  bool image_changed_ = false;
  View view_;
  QPointF drag_from_;
  double previous_gpu_seconds_ = 0.0;
};

}  // namespace pinsel
