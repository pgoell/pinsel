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
    std::unique_ptr<QRhiShaderResourceBindings> bindings;
  };

  QRhi* rhi = nullptr;
  std::unique_ptr<QRhiBuffer> uniforms;
  std::unique_ptr<QRhiSampler> sampler;
  // Every tile's bindings share one layout. The pipeline is made with this
  // set, so it holds a texture of its own that lives as long as the pipeline.
  std::unique_ptr<QRhiTexture> layout_texture;
  std::unique_ptr<QRhiShaderResourceBindings> layout;
  std::unique_ptr<QRhiGraphicsPipeline> pipeline;
  std::unique_ptr<QRhiBuffer> vertices;
  std::vector<Tile> tiles;

  [[nodiscard]] std::unique_ptr<QRhiShaderResourceBindings> bind(QRhiTexture* texture) const {
    std::unique_ptr<QRhiShaderResourceBindings> bindings(rhi->newShaderResourceBindings());
    bindings->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage,
                                                 uniforms.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                  texture, sampler.get()),
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
    layout_texture.reset(rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
    layout_texture->create();
    layout = bind(layout_texture.get());

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

  // Makes a texture for each tile and uploads them all.
  void createTiles(const TileStore& store, QRhiResourceUpdateBatch* updates) {
    ZoneScoped;
    tiles.clear();
    vertices.reset();
    std::vector<float> data;
    for (std::size_t index = 0; index < store.tileCount(); ++index) {
      Tile tile;
      tile.rect = store.tileRect(index);
      tile.texture.reset(
          rhi->newTexture(QRhiTexture::RGBA8, tile.rect.size(), 1,
                          QRhiTexture::MipMapped | QRhiTexture::UsedWithGenerateMips));
      tile.texture->create();
      tile.bindings = bind(tile.texture.get());
      appendQuad(data, tile.rect);
      tiles.push_back(std::move(tile));
      upload(store, index, updates);
    }
    if (data.empty()) {
      return;
    }
    const auto bytes = static_cast<quint32>(data.size() * sizeof(float));
    vertices.reset(rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, bytes));
    vertices->create();
    updates->uploadStaticBuffer(vertices.get(), data.data());
  }

  void upload(const TileStore& store, std::size_t index, QRhiResourceUpdateBatch* updates) {
    QRhiTexture* texture = tiles.at(index).texture.get();
    const QRhiTextureSubresourceUploadDescription source(store.tile(index));
    updates->uploadTexture(texture, QRhiTextureUploadEntry(0, 0, source));
    updates->generateMips(texture);
  }
};

Canvas::Canvas(QWidget* parent) : QRhiWidget(parent), gpu_(std::make_unique<Gpu>()) {}

Canvas::~Canvas() = default;

void Canvas::setImage(TileStore tiles) {
  tiles_ = std::move(tiles);
  image_changed_ = true;
  view_.setViewportSize(QSizeF(size()));
  view_.setImageSize(tiles_.size());
  update();
}

void Canvas::edit(const std::function<void(TileStore&)>& change) {
  change(tiles_);
  update();
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
  QRhiResourceUpdateBatch* updates = rhi()->nextResourceUpdateBatch();
  if (image_changed_) {
    gpu_->createTiles(tiles_, updates);
    std::ignore = tiles_.takeDirty();
    image_changed_ = false;
  } else {
    for (const std::size_t index : tiles_.takeDirty()) {
      gpu_->upload(tiles_, index, updates);
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
  if (event->button() == Qt::MiddleButton) {
    drag_from_ = event->position();
  }
}

void Canvas::mouseMoveEvent(QMouseEvent* event) {
  if (event->buttons().testFlag(Qt::MiddleButton)) {
    view_.panBy(event->position() - drag_from_);
    drag_from_ = event->position();
    update();
  }
}

void Canvas::wheelEvent(QWheelEvent* event) {
  view_.zoomAt(event->position(), std::pow(kZoomStep, event->angleDelta().y() / kWheelNotch));
  update();
}

}  // namespace pinsel
