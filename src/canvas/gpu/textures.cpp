#include <QFile>
#include <algorithm>
#include <array>
#include <tracy/Tracy.hpp>
#include <tuple>
#include <utility>

#include "canvas/gpu/gpu.hpp"

#ifdef Q_OS_WIN
#include <dxgi1_3.h>
#include <wrl/client.h>
#endif

namespace pinsel {

namespace {

// Per vertex: x and y in image pixels, then u and v in the tile.
constexpr int kFloatsPerVertex = 4;

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

constexpr std::array<float, 12> kClipQuad{-1, -1, 1, -1, -1, 1, -1, 1, 1, -1, 1, 1};

}  // namespace

std::unique_ptr<QRhiShaderResourceBindings> Canvas::Gpu::bind(QRhiTexture* texture,
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

std::unique_ptr<QRhiGraphicsPipeline> Canvas::Gpu::makePipeline(
    QRhiRenderPassDescriptor* pass, int sampleCount, QRhiShaderResourceBindings* bindings,
    bool tiles, const char* fragment, bool blend) const {
  std::unique_ptr<QRhiGraphicsPipeline> made(rhi->newGraphicsPipeline());
  QRhiGraphicsPipeline::TargetBlend over;
  over.enable = blend;
  over.srcColor = QRhiGraphicsPipeline::One;
  over.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
  over.srcAlpha = QRhiGraphicsPipeline::One;
  over.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
  made->setTargetBlends({over});
  made->setSampleCount(sampleCount);
  const QString vertex = QLatin1StringView(tiles ? "tile" : "quad");
  made->setShaderStages({
      {QRhiShaderStage::Vertex, loadShader(QStringLiteral(":/shaders/%1.vert.qsb").arg(vertex))},
      {
          QRhiShaderStage::Fragment,
          loadShader(QStringLiteral(":/shaders/%1.frag.qsb").arg(QLatin1StringView(fragment))),
      },
  });
  QRhiVertexInputLayout input;
  if (tiles) {
    input.setBindings({{kFloatsPerVertex * sizeof(float)}});
    input.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float2, 0},
        {0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float)},
    });
  } else {
    input.setBindings({{2 * sizeof(float)}});
    input.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0}});
  }
  made->setVertexInputLayout(input);
  made->setShaderResourceBindings(bindings);
  made->setRenderPassDescriptor(pass);
  made->create();
  return made;
}

void Canvas::Gpu::createPipeline(QRhiRenderPassDescriptor* pass, int sampleCount) {
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
  nearest.reset(rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                                QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
  nearest->create();
  layout_texture.reset(rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
  layout_texture->create();
  empty_mask.reset(rhi->newTexture(QRhiTexture::R8, QSize(1, 1)));
  empty_mask->create();
  // Mipmapped only to match the sampler; one pixel has one level.
  clear_image.reset(rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1), 1, QRhiTexture::MipMapped));
  clear_image->create();
  quad.reset(rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer,
                            static_cast<quint32>(sizeof(kClipQuad))));
  quad->create();
  layout = bind(layout_texture.get(), empty_mask.get());
  pipeline = makePipeline(pass, sampleCount, layout.get(), true, "tile", true);
}

void Canvas::Gpu::createTiles(TileStore& store, QRhiResourceUpdateBatch* updates) {
  ZoneScoped;
  layers.clear();
  mask_tiles.clear();
  vertices.reset();
  rects.clear();
  // A new texture's contents are undefined until the first upload.
  const std::array<quint8, 4> clear{};
  updates->uploadTexture(
      empty_mask.get(),
      QRhiTextureUploadEntry(0, 0, QRhiTextureSubresourceUploadDescription(clear.data(), 1)));
  updates->uploadTexture(
      clear_image.get(),
      QRhiTextureUploadEntry(0, 0, QRhiTextureSubresourceUploadDescription(clear.data(), 4)));
  updates->uploadStaticBuffer(quad.get(), kClipQuad.data());
  std::vector<float> data;
  for (std::size_t index = 0; index < store.tileCount(); ++index) {
    rects.push_back(store.tileRect(index));
    appendQuad(data, rects.back());
  }
  mask_tiles.resize(rects.size());
  for (std::size_t index = 0; index < rects.size(); ++index) {
    uploadMask(store.tile(index, TileStore::Plane::Mask), index, updates);
  }
  syncLayers(store, updates);
  if (data.empty()) {
    return;
  }
  const auto bytes = static_cast<quint32>(data.size() * sizeof(float));
  vertices.reset(rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, bytes));
  vertices->create();
  updates->uploadStaticBuffer(vertices.get(), data.data());
}

void Canvas::Gpu::syncLayers(TileStore& store, QRhiResourceUpdateBatch* updates) {
  std::vector<Layer> synced;
  synced.reserve(store.layerCount());
  for (std::size_t position = 0; position < store.layerCount(); ++position) {
    const std::uint64_t id = store.layerId(position);
    const auto found = std::ranges::find(layers, id, &Layer::id);
    if (found != layers.end()) {
      synced.push_back(std::move(*found));
    } else {
      synced.push_back({.id = id, .tiles = std::vector<Tile>(rects.size())});
      std::ignore = store.takeLayerDirty(position);
      for (std::size_t index = 0; index < rects.size(); ++index) {
        upload(store.layerTile(position, index), synced.back().tiles.at(index), index, updates);
      }
      continue;
    }
    for (const std::size_t index : store.takeLayerDirty(position)) {
      upload(store.layerTile(position, index), synced.back().tiles.at(index), index, updates);
    }
  }
  layers = std::move(synced);
}

// The D3D11 driver copies each upload into staging memory in this process
// and keeps it for reuse: about 45 MB after a 4K image, more than the image's
// own tiles. IDXGIDevice3::Trim gives it back; the next upload takes it again.
// https://learn.microsoft.com/windows/win32/api/dxgi1_3/nf-dxgi1_3-idxgidevice3-trim
void Canvas::Gpu::releaseUploadMemory() const {
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

void Canvas::Gpu::upload(const QImage& image, Tile& tile, std::size_t index,
                         QRhiResourceUpdateBatch* updates) {
  if (image.isNull()) {
    tile = {};
    return;
  }
  if (!tile.texture) {
    tile.texture.reset(rhi->newTexture(QRhiTexture::RGBA8, rects.at(index).size(), 1,
                                       QRhiTexture::MipMapped | QRhiTexture::UsedWithGenerateMips));
    tile.texture->create();
    tile.bindings = bind(tile.texture.get(), empty_mask.get());
  }
  updates->uploadTexture(
      tile.texture.get(),
      QRhiTextureUploadEntry(0, 0, QRhiTextureSubresourceUploadDescription(image)));
  updates->generateMips(tile.texture.get());
}

void Canvas::Gpu::uploadMask(const QImage& image, std::size_t index,
                             QRhiResourceUpdateBatch* updates) {
  Tile& tile = mask_tiles.at(index);
  if (image.isNull()) {
    tile = {};
    return;
  }
  if (!tile.texture) {
    tile.texture.reset(rhi->newTexture(QRhiTexture::R8, rects.at(index).size()));
    tile.texture->create();
    tile.bindings = bind(clear_image.get(), tile.texture.get());
  }
  // Raw bytes with a stride, since QRhi reads a QImage as four bytes a pixel.
  QRhiTextureSubresourceUploadDescription source(image.constBits(),
                                                 static_cast<quint32>(image.sizeInBytes()));
  source.setDataStride(static_cast<quint32>(image.bytesPerLine()));
  source.setSourceSize(image.size());
  updates->uploadTexture(tile.texture.get(), QRhiTextureUploadEntry(0, 0, source));
}

}  // namespace pinsel
