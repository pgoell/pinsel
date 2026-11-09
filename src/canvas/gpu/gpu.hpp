#pragma once

#include <rhi/qrhi.h>

#include <QColor>
#include <QMatrix4x4>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "canvas/canvas.hpp"
#include "tiles/tile_store.hpp"

namespace pinsel {

// The canvas's GPU side: a texture for each painted tile of each layer and of
// the mask, and the passes that draw a frame from them.
//
// A plain layer (normal and fully opaque) draws straight onto the widget. Any
// other layer needs the pixels below it, which a draw onto the widget cannot
// read. So the layers up to the topmost such one composite off screen, in two
// textures the size of the widget that take turns as backdrop and target. Each
// of those layers draws into a third texture, and a pass over the whole widget
// mixes that with the backdrop into the target. The result goes onto the
// widget, and the layers above it draw over it. The three textures exist only
// while a layer needs them.
struct Canvas::Gpu {
  // One plane's texture for one tile. Null while the store's tile is null.
  struct Tile {
    std::unique_ptr<QRhiTexture> texture;
    std::unique_ptr<QRhiShaderResourceBindings> bindings;
  };
  struct Layer {
    std::uint64_t id = 0;
    std::vector<Tile> tiles;
  };
  struct Target {
    std::unique_ptr<QRhiTexture> texture;
    std::unique_ptr<QRhiTextureRenderTarget> target;
  };
  struct Frame {
    // Image pixels to clip space.
    QMatrix4x4 transform;
    QRectF visible;
    QColor clear;
  };

  QRhi* rhi = nullptr;
  std::unique_ptr<QRhiBuffer> uniforms;
  std::unique_ptr<QRhiSampler> sampler;
  std::unique_ptr<QRhiSampler> mask_sampler;
  // Every tile's bindings share one layout. The pipelines are made with these
  // sets, so they hold a texture of their own that lives as long as they do.
  std::unique_ptr<QRhiTexture> layout_texture;
  std::unique_ptr<QRhiShaderResourceBindings> layout;
  // A layer tile draws with empty_mask, and a mask tile with clear_image, so
  // one shader draws both.
  std::unique_ptr<QRhiTexture> empty_mask;
  std::unique_ptr<QRhiTexture> clear_image;
  std::unique_ptr<QRhiGraphicsPipeline> pipeline;
  std::unique_ptr<QRhiBuffer> vertices;
  // Two triangles over the whole target, in clip space.
  std::unique_ptr<QRhiBuffer> quad;
  std::vector<QRect> rects;
  // Bottom to top, as in the store.
  std::vector<Layer> layers;
  std::vector<Tile> mask_tiles;
  // Set by a frame that uploaded a whole image, for the frame after it.
  bool trim_pending = false;

  // Off screen compositing. The pipelines and the pass stay once made; the
  // textures and their bindings go when no layer needs them.
  std::unique_ptr<QRhiSampler> nearest;
  // One slot per layer, for the pass that mixes it.
  std::unique_ptr<QRhiBuffer> blend_uniforms;
  std::unique_ptr<QRhiShaderResourceBindings> blend_layout;
  std::unique_ptr<QRhiShaderResourceBindings> present_layout;
  std::unique_ptr<QRhiRenderPassDescriptor> offscreen_pass;
  std::unique_ptr<QRhiGraphicsPipeline> offscreen_pipeline;
  std::unique_ptr<QRhiGraphicsPipeline> blend_pipeline;
  std::unique_ptr<QRhiGraphicsPipeline> present_pipeline;
  std::array<Target, 2> composites;
  Target layer_target;
  // Index i reads composites[i]: blend as the backdrop, present as the result.
  std::array<std::unique_ptr<QRhiShaderResourceBindings>, 2> blend_bindings;
  std::array<std::unique_ptr<QRhiShaderResourceBindings>, 2> present_bindings;

  // textures.cpp
  void createPipeline(QRhiRenderPassDescriptor* pass, int sampleCount);
  void createTiles(TileStore& store, QRhiResourceUpdateBatch* updates);
  // Matches the layers to the store's stack: keeps the textures of a layer it
  // already has, wherever it moved, and uploads a new one whole. Then uploads
  // each layer's dirty tiles.
  void syncLayers(TileStore& store, QRhiResourceUpdateBatch* updates);
  void uploadMask(const QImage& image, std::size_t index, QRhiResourceUpdateBatch* updates);
  void releaseUploadMemory() const;

  // frame.cpp
  void drawFrame(QRhiCommandBuffer* commands, QRhiRenderTarget* screen, const TileStore& store,
                 const Frame& frame, QRhiResourceUpdateBatch* updates);

 private:
  [[nodiscard]] std::unique_ptr<QRhiShaderResourceBindings> bind(QRhiTexture* texture,
                                                                 QRhiTexture* mask) const;
  [[nodiscard]] std::unique_ptr<QRhiShaderResourceBindings> bindBlend(QRhiBuffer* values,
                                                                      QRhiTexture* layer,
                                                                      QRhiTexture* backdrop) const;
  [[nodiscard]] std::unique_ptr<QRhiShaderResourceBindings> bindPresent(
      QRhiBuffer* values, QRhiTexture* composite) const;
  [[nodiscard]] std::unique_ptr<QRhiGraphicsPipeline> makePipeline(
      QRhiRenderPassDescriptor* pass, int sampleCount, QRhiShaderResourceBindings* bindings,
      bool tiles, const char* fragment, bool blend) const;
  void upload(const QImage& image, Tile& tile, std::size_t index, QRhiResourceUpdateBatch* updates);

  void createTargets(QSize pixels);
  void prepareOffscreen(QRhiRenderTarget* screen, std::size_t layerCount);
  void releaseOffscreen();
  void writeBlendUniforms(const TileStore& store, QSize pixels,
                          QRhiResourceUpdateBatch* updates) const;
  // Returns the index of the composite that holds the result.
  std::size_t composite(QRhiCommandBuffer* commands, const TileStore& store, const Frame& frame,
                        std::size_t top, QRhiResourceUpdateBatch* updates);
  void drawTiles(QRhiCommandBuffer* commands, QRhiGraphicsPipeline* with,
                 const std::vector<Tile>& plane, QSize pixels, QRectF visible) const;
  // With a slot for a binding set whose uniform buffer takes a dynamic offset.
  void drawQuad(QRhiCommandBuffer* commands, QRhiGraphicsPipeline* with,
                QRhiShaderResourceBindings* bindings, QSize pixels,
                std::optional<std::size_t> slot) const;
};

}  // namespace pinsel
