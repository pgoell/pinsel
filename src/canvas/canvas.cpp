#include "canvas/canvas.hpp"

#include <rhi/qrhi.h>

#include <QFile>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QWheelEvent>
#include <array>
#include <cmath>
#include <cstddef>
#include <tracy/Tracy.hpp>
#include <tuple>
#include <utility>
#include <vector>

#ifdef Q_OS_WIN
#include <dxgi1_3.h>
#include <wrl/client.h>
#endif

namespace pinsel {

namespace {

constexpr double kZoomStep = 1.25;
constexpr double kWheelNotch = 120.0;
// Per vertex: x and y in image pixels, then u and v in the tile.
constexpr int kFloatsPerVertex = 4;
constexpr int kVerticesPerTile = 6;

QShader loadShader(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    qFatal("Shader %s is missing from the resources", qPrintable(path));
  }
  return QShader::fromSerialized(file.readAll());
}

// Two triangles over the tile's rectangle.
void appendQuad(std::vector<float>& vertices, QRect rect) {
  const auto left = static_cast<float>(rect.left());
  const auto top = static_cast<float>(rect.top());
  const auto right = static_cast<float>(rect.left() + rect.width());
  const auto bottom = static_cast<float>(rect.top() + rect.height());
  const std::array quad{
      left, top,    0.0F, 0.0F, right, top, 1.0F, 0.0F, left,  bottom, 0.0F, 1.0F,
      left, bottom, 0.0F, 1.0F, right, top, 1.0F, 0.0F, right, bottom, 1.0F, 1.0F,
  };
  vertices.insert(vertices.end(), quad.begin(), quad.end());
}

}  // namespace

struct Canvas::Gpu {
  struct Tile {
    QRect rect;
    std::unique_ptr<QRhiTexture> texture;
    // Null while the tile has no mask; the bindings then use empty_mask.
    std::unique_ptr<QRhiTexture> mask;
    std::unique_ptr<QRhiShaderResourceBindings> bindings;
  };

  QRhi* rhi = nullptr;
  std::unique_ptr<QRhiBuffer> uniforms;
  std::unique_ptr<QRhiSampler> sampler;
  std::unique_ptr<QRhiSampler> mask_sampler;
  // Every tile's bindings share one layout. The pipeline is made with this
  // set, so it holds a texture of its own that lives as long as the pipeline.
  std::unique_ptr<QRhiTexture> layout_texture;
  std::unique_ptr<QRhiShaderResourceBindings> layout;
  std::unique_ptr<QRhiTexture> empty_mask;
  std::unique_ptr<QRhiGraphicsPipeline> pipeline;
  std::unique_ptr<QRhiBuffer> vertices;
  std::vector<Tile> tiles;
  // Set by a frame that uploaded a whole image, for the frame after it.
  bool trim_pending = false;

  [[nodiscard]] std::unique_ptr<QRhiShaderResourceBindings> bind(QRhiTexture* texture,
                                                                 QRhiTexture* mask) const {
    std::unique_ptr<QRhiShaderResourceBindings> bindings(rhi->newShaderResourceBindings());
    bindings->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage,
                                                 uniforms.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                  texture, sampler.get()),
        QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage, mask,
                                                  mask_sampler.get()),
    });
    bindings->create();
    return bindings;
  }

  void createPipeline(QRhiRenderPassDescriptor* pass, int sampleCount) {
    uniforms.reset(
        rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(float) * 16));
    uniforms->create();
    // Nearest when zoomed in, so pixels stay sharp and tile edges never blend;
    // mipmaps when zoomed out, so a 4K image fitted to the window does not alias.
    sampler.reset(rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Linear, QRhiSampler::Linear,
                                  QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    sampler->create();
    // The mask has no mipmaps, so it may not use a sampler that reads them.
    mask_sampler.reset(rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Linear, QRhiSampler::None,
                                       QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    mask_sampler->create();
    layout_texture.reset(rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
    layout_texture->create();
    empty_mask.reset(rhi->newTexture(QRhiTexture::R8, QSize(1, 1)));
    empty_mask->create();
    layout = bind(layout_texture.get(), empty_mask.get());

    pipeline.reset(rhi->newGraphicsPipeline());
    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    blend.srcColor = QRhiGraphicsPipeline::One;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    pipeline->setTargetBlends({blend});
    pipeline->setSampleCount(sampleCount);
    pipeline->setShaderStages({
        {QRhiShaderStage::Vertex, loadShader(QStringLiteral(":/shaders/canvas.vert.qsb"))},
        {QRhiShaderStage::Fragment, loadShader(QStringLiteral(":/shaders/canvas.frag.qsb"))},
    });
    QRhiVertexInputLayout input;
    input.setBindings({{kFloatsPerVertex * sizeof(float)}});
    input.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float2, 0},
        {0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float)},
    });
    pipeline->setVertexInputLayout(input);
    pipeline->setShaderResourceBindings(layout.get());
    pipeline->setRenderPassDescriptor(pass);
    pipeline->create();
  }

  void createTiles(const TileStore& store, QRhiResourceUpdateBatch* updates) {
    ZoneScoped;
    tiles.clear();
    vertices.reset();
    // A new texture's contents are undefined until the first upload.
    const std::array<quint8, 4> clear{};
    updates->uploadTexture(
        empty_mask.get(),
        QRhiTextureUploadEntry(0, 0, QRhiTextureSubresourceUploadDescription(clear.data(), 1)));
    std::vector<float> data;
    for (std::size_t index = 0; index < store.tileCount(); ++index) {
      Tile tile;
      tile.rect = store.tileRect(index);
      tile.texture.reset(
          rhi->newTexture(QRhiTexture::RGBA8, tile.rect.size(), 1,
                          QRhiTexture::MipMapped | QRhiTexture::UsedWithGenerateMips));
      tile.texture->create();
      tile.bindings = bind(tile.texture.get(), empty_mask.get());
      appendQuad(data, tile.rect);
      tiles.push_back(std::move(tile));
      upload(store, index, updates);
      uploadMask(store, index, updates);
    }
    if (data.empty()) {
      return;
    }
    const auto bytes = static_cast<quint32>(data.size() * sizeof(float));
    vertices.reset(rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, bytes));
    vertices->create();
    updates->uploadStaticBuffer(vertices.get(), data.data());
  }

  // The D3D11 driver copies each upload into staging memory in this process
  // and keeps it for reuse: about 45 MB after a 4K image, more than the image's
  // own tiles. IDXGIDevice3::Trim gives it back; the next upload takes it again.
  // https://learn.microsoft.com/windows/win32/api/dxgi1_3/nf-dxgi1_3-idxgidevice3-trim
  void releaseUploadMemory() const {
#ifdef Q_OS_WIN
    if (rhi->backend() != QRhi::D3D11) {
      return;
    }
    const auto* handles = static_cast<const QRhiD3D11NativeHandles*>(rhi->nativeHandles());
    Microsoft::WRL::ComPtr<IDXGIDevice3> device;
    if (SUCCEEDED(
            Microsoft::WRL::ComPtr<IUnknown>(static_cast<IUnknown*>(handles->dev)).As(&device))) {
      device->Trim();
    }
#endif
  }

  void upload(const TileStore& store, std::size_t index, QRhiResourceUpdateBatch* updates) {
    QRhiTexture* texture = tiles.at(index).texture.get();
    const QRhiTextureSubresourceUploadDescription source(store.tile(index));
    updates->uploadTexture(texture, QRhiTextureUploadEntry(0, 0, source));
    updates->generateMips(texture);
  }

  void uploadMask(const TileStore& store, std::size_t index, QRhiResourceUpdateBatch* updates) {
    Tile& tile = tiles.at(index);
    const QImage& mask = store.tile(index, TileStore::Plane::Mask);
    if (mask.isNull()) {
      if (tile.mask) {
        tile.bindings = bind(tile.texture.get(), empty_mask.get());
        tile.mask.reset();
      }
      return;
    }
    if (!tile.mask) {
      tile.mask.reset(rhi->newTexture(QRhiTexture::R8, tile.rect.size()));
      tile.mask->create();
      tile.bindings = bind(tile.texture.get(), tile.mask.get());
    }
    // Raw bytes with a stride, since QRhi reads a QImage as four bytes a pixel.
    QRhiTextureSubresourceUploadDescription source(mask.constBits(),
                                                   static_cast<quint32>(mask.sizeInBytes()));
    source.setDataStride(static_cast<quint32>(mask.bytesPerLine()));
    source.setSourceSize(mask.size());
    updates->uploadTexture(tile.mask.get(), QRhiTextureUploadEntry(0, 0, source));
  }
};

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
  endStroke();
  tiles_.clearMask();
  update();
  historyChanged();
}

void Canvas::undo() {
  endStroke();
  tiles_.undo();
  update();
  historyChanged();
}

void Canvas::redo() {
  endStroke();
  tiles_.redo();
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
    gpu_->createTiles(tiles_, updates);
    std::ignore = tiles_.takeDirty();
    std::ignore = tiles_.takeDirty(TileStore::Plane::Mask);
    image_changed_ = false;
    gpu_->trim_pending = true;
    // Draws a second frame, even if nothing else changes, to trim in.
    update();
  } else {
    for (const std::size_t index : tiles_.takeDirty()) {
      gpu_->upload(tiles_, index, updates);
    }
    for (const std::size_t index : tiles_.takeDirty(TileStore::Plane::Mask)) {
      gpu_->uploadMask(tiles_, index, updates);
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
  updates->updateDynamicBuffer(gpu_->uniforms.get(), 0, sizeof(float) * 16, transform.constData());

  const QColor clear = palette().base().color();
  commands->beginPass(renderTarget(), clear, {1.0F, 0}, updates);
  if (gpu_->vertices) {
    commands->setGraphicsPipeline(gpu_->pipeline.get());
    const QSize pixels = renderTarget()->pixelSize();
    commands->setViewport(QRhiViewport(0, 0, static_cast<float>(pixels.width()),
                                       static_cast<float>(pixels.height())));
    const QRhiCommandBuffer::VertexInput input(gpu_->vertices.get(), 0);
    commands->setVertexInput(0, 1, &input);
    const QRectF visible = view_.visibleImageRect();
    quint32 first = 0;
    for (const Gpu::Tile& tile : gpu_->tiles) {
      if (visible.intersects(tile.rect)) {
        commands->setShaderResources(tile.bindings.get());
        commands->draw(kVerticesPerTile, 1, first);
      }
      first += kVerticesPerTile;
    }
  }
  commands->endPass();
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
