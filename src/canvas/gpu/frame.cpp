#include <cstdint>
#include <optional>

#include "canvas/gpu/gpu.hpp"

namespace pinsel {

namespace {

constexpr int kVerticesPerTile = 6;
constexpr int kQuadVertices = 6;

// The Blend block of blend.frag and present.frag, in std140 layout.
struct BlendUniforms {
  float width;
  float height;
  float opacity;
  std::int32_t mode;
};

// blend.frag compares mode with these numbers; any other mode draws as normal.
static_assert(static_cast<int>(TileStore::BlendMode::Normal) == 0);
static_assert(static_cast<int>(TileStore::BlendMode::Multiply) == 1);
static_assert(static_cast<int>(TileStore::BlendMode::Screen) == 2);
static_assert(static_cast<int>(TileStore::BlendMode::Overlay) == 3);

quint32 slotStride(const QRhi* rhi) {
  return static_cast<quint32>(rhi->ubufAligned(sizeof(BlendUniforms)));
}

bool shows(const TileStore& store, std::size_t layer) {
  return store.layerVisible(layer) && store.layerOpacity(layer) > 0.0;
}

std::optional<std::size_t> topBlending(const TileStore& store) {
  for (std::size_t layer = store.layerCount(); layer-- > 0;) {
    if (shows(store, layer) && !store.layerPlain(layer)) {
      return layer;
    }
  }
  return std::nullopt;
}

}  // namespace

std::unique_ptr<QRhiShaderResourceBindings> Canvas::Gpu::bindBlend(QRhiBuffer* values,
                                                                   QRhiTexture* layer,
                                                                   QRhiTexture* backdrop) const {
  std::unique_ptr<QRhiShaderResourceBindings> bindings(rhi->newShaderResourceBindings());
  bindings->setBindings({
      QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(
          0, QRhiShaderResourceBinding::FragmentStage, values, sizeof(BlendUniforms)),
      QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, layer,
                                                nearest.get()),
      QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage,
                                                backdrop, nearest.get()),
  });
  bindings->create();
  return bindings;
}

std::unique_ptr<QRhiShaderResourceBindings> Canvas::Gpu::bindPresent(QRhiBuffer* values,
                                                                     QRhiTexture* composite) const {
  std::unique_ptr<QRhiShaderResourceBindings> bindings(rhi->newShaderResourceBindings());
  bindings->setBindings({
      QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::FragmentStage, values,
                                               0, sizeof(BlendUniforms)),
      QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                composite, nearest.get()),
  });
  bindings->create();
  return bindings;
}

void Canvas::Gpu::createTargets(QSize pixels) {
  for (Target* target : {&composites.at(0), &composites.at(1), &layer_target}) {
    target->texture.reset(
        rhi->newTexture(QRhiTexture::RGBA8, pixels, 1, QRhiTexture::RenderTarget));
    target->texture->create();
    target->target.reset(rhi->newTextureRenderTarget({target->texture.get()}));
    if (!offscreen_pass) {
      offscreen_pass.reset(target->target->newCompatibleRenderPassDescriptor());
    }
    target->target->setRenderPassDescriptor(offscreen_pass.get());
    target->target->create();
  }
}

void Canvas::Gpu::prepareOffscreen(QRhiRenderTarget* screen, std::size_t layerCount) {
  const QSize pixels = screen->pixelSize();
  const bool resized = !layer_target.texture || layer_target.texture->pixelSize() != pixels;
  if (resized) {
    createTargets(pixels);
  }
  if (!offscreen_pipeline) {
    // The layouts hold the canvas's own uniform buffer, which lives as long as
    // the pipelines, while blend_uniforms grows with the stack.
    blend_layout = bindBlend(uniforms.get(), layout_texture.get(), layout_texture.get());
    present_layout = bindPresent(uniforms.get(), layout_texture.get());
    offscreen_pipeline = makePipeline(offscreen_pass.get(), 1, layout.get(), true, "tile", true);
    blend_pipeline =
        makePipeline(offscreen_pass.get(), 1, blend_layout.get(), false, "blend", false);
    present_pipeline = makePipeline(screen->renderPassDescriptor(), screen->sampleCount(),
                                    present_layout.get(), false, "present", true);
  }
  const quint32 needed = static_cast<quint32>(layerCount) * slotStride(rhi);
  const bool grown = !blend_uniforms || blend_uniforms->size() < needed;
  if (grown) {
    blend_uniforms.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, needed));
    blend_uniforms->create();
  }
  if (!resized && !grown) {
    return;
  }
  for (std::size_t index = 0; index < composites.size(); ++index) {
    QRhiTexture* composite = composites.at(index).texture.get();
    blend_bindings.at(index) =
        bindBlend(blend_uniforms.get(), layer_target.texture.get(), composite);
    present_bindings.at(index) = bindPresent(blend_uniforms.get(), composite);
  }
}

void Canvas::Gpu::releaseOffscreen() {
  blend_bindings = {};
  present_bindings = {};
  composites = {};
  layer_target = {};
}

void Canvas::Gpu::writeBlendUniforms(const TileStore& store, QSize pixels,
                                     QRhiResourceUpdateBatch* updates) const {
  const quint32 stride = slotStride(rhi);
  for (std::size_t layer = 0; layer < store.layerCount(); ++layer) {
    const BlendUniforms values{
        .width = static_cast<float>(pixels.width()),
        .height = static_cast<float>(pixels.height()),
        .opacity = static_cast<float>(store.layerOpacity(layer)),
        .mode = static_cast<std::int32_t>(store.layerBlendMode(layer)),
    };
    updates->updateDynamicBuffer(blend_uniforms.get(), static_cast<quint32>(layer) * stride,
                                 sizeof(values), &values);
  }
}

void Canvas::Gpu::drawTiles(QRhiCommandBuffer* commands, QRhiGraphicsPipeline* with,
                            const std::vector<Tile>& plane, QSize pixels, QRectF visible) const {
  commands->setGraphicsPipeline(with);
  commands->setViewport(
      QRhiViewport(0, 0, static_cast<float>(pixels.width()), static_cast<float>(pixels.height())));
  const QRhiCommandBuffer::VertexInput input(vertices.get(), 0);
  commands->setVertexInput(0, 1, &input);
  quint32 first = 0;
  for (std::size_t index = 0; index < plane.size(); ++index) {
    const Tile& tile = plane.at(index);
    if (tile.bindings && visible.intersects(rects.at(index))) {
      commands->setShaderResources(tile.bindings.get());
      commands->draw(kVerticesPerTile, 1, first);
    }
    first += kVerticesPerTile;
  }
}

void Canvas::Gpu::drawQuad(QRhiCommandBuffer* commands, QRhiGraphicsPipeline* with,
                           QRhiShaderResourceBindings* bindings, QSize pixels,
                           std::optional<std::size_t> slot) const {
  commands->setGraphicsPipeline(with);
  commands->setViewport(
      QRhiViewport(0, 0, static_cast<float>(pixels.width()), static_cast<float>(pixels.height())));
  const QRhiCommandBuffer::DynamicOffset offset(
      0, static_cast<quint32>(slot.value_or(0)) * slotStride(rhi));
  commands->setShaderResources(bindings, slot ? 1 : 0, &offset);
  const QRhiCommandBuffer::VertexInput input(quad.get(), 0);
  commands->setVertexInput(0, 1, &input);
  commands->draw(kQuadVertices);
}

std::size_t Canvas::Gpu::composite(QRhiCommandBuffer* commands, const TileStore& store,
                                   const Frame& frame, std::size_t top,
                                   QRhiResourceUpdateBatch* updates) {
  const QSize pixels = layer_target.texture->pixelSize();
  std::size_t result = 0;
  commands->beginPass(composites.at(result).target.get(), Qt::transparent, {1.0F, 0}, updates);
  for (std::size_t position = 0; position <= top; ++position) {
    if (!shows(store, position)) {
      continue;
    }
    const std::vector<Tile>& tiles = layers.at(position).tiles;
    if (store.layerPlain(position)) {
      drawTiles(commands, offscreen_pipeline.get(), tiles, pixels, frame.visible);
      continue;
    }
    commands->endPass();
    commands->beginPass(layer_target.target.get(), Qt::transparent, {1.0F, 0});
    drawTiles(commands, offscreen_pipeline.get(), tiles, pixels, frame.visible);
    commands->endPass();
    // The quad covers every pixel, so the target needs no clear or copy first.
    const std::size_t backdrop = result;
    result = 1 - result;
    commands->beginPass(composites.at(result).target.get(), Qt::transparent, {1.0F, 0});
    drawQuad(commands, blend_pipeline.get(), blend_bindings.at(backdrop).get(), pixels, position);
  }
  commands->endPass();
  return result;
}

void Canvas::Gpu::drawFrame(QRhiCommandBuffer* commands, QRhiRenderTarget* screen,
                            const TileStore& store, const Frame& frame,
                            QRhiResourceUpdateBatch* updates) {
  updates->updateDynamicBuffer(uniforms.get(), 0, sizeof(float) * 16, frame.transform.constData());
  const QSize pixels = screen->pixelSize();
  const std::optional<std::size_t> top = vertices ? topBlending(store) : std::nullopt;
  std::optional<std::size_t> result;
  if (top) {
    prepareOffscreen(screen, store.layerCount());
    writeBlendUniforms(store, pixels, updates);
    result = composite(commands, store, frame, *top, updates);
    updates = nullptr;
  } else {
    releaseOffscreen();
  }

  commands->beginPass(screen, frame.clear, {1.0F, 0}, updates);
  if (vertices) {
    if (result) {
      drawQuad(commands, present_pipeline.get(), present_bindings.at(*result).get(), pixels,
               std::nullopt);
    }
    for (std::size_t position = top ? *top + 1 : 0; position < layers.size(); ++position) {
      if (shows(store, position)) {
        drawTiles(commands, pipeline.get(), layers.at(position).tiles, pixels, frame.visible);
      }
    }
    drawTiles(commands, pipeline.get(), mask_tiles, pixels, frame.visible);
  }
  commands->endPass();
}

}  // namespace pinsel
