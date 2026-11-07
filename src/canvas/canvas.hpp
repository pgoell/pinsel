#pragma once

#include <QPointF>
#include <QRhiWidget>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

#include "brush/brush.hpp"
#include "canvas/view.hpp"
#include "tiles/tile_store.hpp"

namespace pinsel {

// Draws the image as GPU tiles. The left mouse button uses the tool, the middle
// button pans and the wheel zooms at the cursor.
class Canvas : public QRhiWidget {
 public:
  enum class Tool : std::uint8_t { Brush, Eraser, Mask, Pan };

  explicit Canvas(QWidget* parent = nullptr);
  ~Canvas() override;
  Canvas(const Canvas&) = delete;
  Canvas& operator=(const Canvas&) = delete;
  Canvas(Canvas&&) = delete;
  Canvas& operator=(Canvas&&) = delete;

  // QRhiWidget's render(QRhiCommandBuffer*) hides these.
  using QWidget::render;

  void setImage(TileStore tiles);
  [[nodiscard]] const TileStore& tiles() const {
    return tiles_;
  }
  void setTool(Tool tool);
  // The brush and the eraser share these; the eraser ignores the color.
  void setBrush(const BrushSettings& brush);
  void undo();
  void redo();
  // Called whenever what undo and redo would do may have changed.
  void setHistoryChanged(std::function<void()> changed);
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
  void mouseReleaseEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;

 private:
  struct Gpu;

  void zoomAtCentre(double factor);
  void endStroke();
  void historyChanged() const;

  std::unique_ptr<Gpu> gpu_;
  TileStore tiles_;
  bool image_changed_ = false;
  View view_;
  std::optional<Qt::MouseButton> pan_button_;
  QPointF drag_from_;
  Tool tool_ = Tool::Brush;
  BrushSettings brush_;
  std::optional<Stroke> stroke_;
  std::function<void()> history_changed_;
  double previous_gpu_seconds_ = 0.0;
};

}  // namespace pinsel
