#include "canvas/canvas.hpp"

#include <rhi/qrhi.h>

#include <QMatrix4x4>
#include <QMouseEvent>
#include <QWheelEvent>
#include <cmath>
#include <cstddef>
#include <tracy/Tracy.hpp>
#include <tuple>
#include <utility>

#include "canvas/gpu/gpu.hpp"

namespace pinsel {

namespace {

constexpr double kZoomStep = 1.25;
constexpr double kWheelNotch = 120.0;

}  // namespace

Canvas::Canvas(QWidget* parent) : QRhiWidget(parent), gpu_(std::make_unique<Gpu>()) {}

Canvas::~Canvas() = default;

void Canvas::setImage(TileStore tiles) {
  stroke_.reset();
  tiles_ = std::move(tiles);
  image_changed_ = true;
  view_.setViewportSize(QSizeF(size()));
  view_.setImageSize(tiles_.size());
  update();
  historyChanged();
}

void Canvas::setTool(Tool tool) {
  endStroke();
  tool_ = tool;
}

void Canvas::setBrush(const BrushSettings& brush) {
  brush_ = brush;
}

void Canvas::eraseMask() {
  edit(&TileStore::clearMask);
}

void Canvas::undo() {
  edit([](TileStore& tiles) { tiles.undo(); });
}

void Canvas::redo() {
  edit([](TileStore& tiles) { tiles.redo(); });
}

void Canvas::edit(const std::function<void(TileStore&)>& change) {
  endStroke();
  change(tiles_);
  update();
  historyChanged();
}

void Canvas::setHistoryChanged(std::function<void()> changed) {
  history_changed_ = std::move(changed);
}

void Canvas::historyChanged() const {
  if (history_changed_) {
    history_changed_();
  }
}

void Canvas::endStroke() {
  if (!stroke_) {
    return;
  }
  stroke_.reset();
  tiles_.commit();
  historyChanged();
}

void Canvas::zoomIn() {
  zoomAtCentre(kZoomStep);
}

void Canvas::zoomOut() {
  zoomAtCentre(1.0 / kZoomStep);
}

void Canvas::fitToWindow() {
  view_.fit();
  update();
}

void Canvas::zoomAtCentre(double factor) {
  view_.zoomAt(QPointF(width() / 2.0, height() / 2.0), factor);
  update();
}

void Canvas::initialize(QRhiCommandBuffer* /*commands*/) {
  if (gpu_->rhi != rhi()) {
    gpu_ = std::make_unique<Gpu>();
    gpu_->rhi = rhi();
    image_changed_ = true;
  }
  if (!gpu_->pipeline) {
    gpu_->createPipeline(renderTarget()->renderPassDescriptor(), sampleCount());
  }
}

void Canvas::render(QRhiCommandBuffer* commands) {
  ZoneScoped;
  // QRhiWidget draws in offscreen frames, which wait for the GPU to finish, so
  // the time is ready by the next frame.
  previous_gpu_seconds_ = commands->lastCompletedGpuTime();
  TracyPlot("gpu_ms", previous_gpu_seconds_ * 1000.0);
  if (gpu_->trim_pending) {
    gpu_->releaseUploadMemory();
    gpu_->trim_pending = false;
  }
  QRhiResourceUpdateBatch* updates = rhi()->nextResourceUpdateBatch();
  if (image_changed_) {
    std::ignore = tiles_.takeDirty(TileStore::Plane::Mask);
    gpu_->createTiles(tiles_, updates);
    image_changed_ = false;
    gpu_->trim_pending = true;
    // Draws a second frame, even if nothing else changes, to trim in.
    update();
  } else {
    gpu_->syncLayers(tiles_, updates);
    for (const std::size_t index : tiles_.takeDirty(TileStore::Plane::Mask)) {
      gpu_->uploadMask(tiles_.tile(index, TileStore::Plane::Mask), index, updates);
    }
  }

  // Widget pixels to clip space; the widget may draw at a higher device pixel ratio.
  QMatrix4x4 transform = rhi()->clipSpaceCorrMatrix();
  transform.ortho(0.0F, static_cast<float>(width()), static_cast<float>(height()), 0.0F, -1.0F,
                  1.0F);
  transform.translate(static_cast<float>(view_.offset().x()),
                      static_cast<float>(view_.offset().y()));
  const auto scale = static_cast<float>(view_.scale());
  transform.scale(scale, scale);
  gpu_->drawFrame(commands, renderTarget(), tiles_,
                  {
                      .transform = transform,
                      .visible = view_.visibleImageRect(),
                      .clear = palette().base().color(),
                  },
                  updates);
}

void Canvas::releaseResources() {
  gpu_ = std::make_unique<Gpu>();
  image_changed_ = true;
}

void Canvas::resizeEvent(QResizeEvent* event) {
  view_.setViewportSize(QSizeF(size()));
  QRhiWidget::resizeEvent(event);
}

void Canvas::mousePressEvent(QMouseEvent* event) {
  const bool pan = event->button() == Qt::MiddleButton ||
                   (event->button() == Qt::LeftButton && tool_ == Tool::Pan);
  if (pan) {
    pan_button_ = event->button();
    drag_from_ = event->position();
  } else if (event->button() == Qt::LeftButton && tool_ != Tool::Pan && tiles_.tileCount() > 0) {
    BrushSettings brush = brush_;
    const bool mask = tool_ == Tool::Mask;
    brush.erase = tool_ == Tool::Eraser || (mask && event->modifiers().testFlag(Qt::AltModifier));
    stroke_.emplace(tiles_, brush, mask ? TileStore::Plane::Mask : TileStore::Plane::Image);
    stroke_->moveTo(view_.toImage(event->position()));
    update();
  }
}

void Canvas::mouseMoveEvent(QMouseEvent* event) {
  if (pan_button_) {
    view_.panBy(event->position() - drag_from_);
    drag_from_ = event->position();
    update();
  } else if (stroke_) {
    stroke_->moveTo(view_.toImage(event->position()));
    update();
  }
}

void Canvas::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == pan_button_) {
    pan_button_.reset();
  }
  if (event->button() == Qt::LeftButton) {
    endStroke();
  }
}

void Canvas::wheelEvent(QWheelEvent* event) {
  view_.zoomAt(event->position(), std::pow(kZoomStep, event->angleDelta().y() / kWheelNotch));
  update();
}

}  // namespace pinsel
