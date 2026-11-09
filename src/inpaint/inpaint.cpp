#include "inpaint/inpaint.hpp"

#include <QColor>
#include <QPainter>
#include <QPoint>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <ranges>
#include <span>
#include <utility>

namespace pinsel {

namespace {

constexpr double kPixels = 1024.0 * 1024.0;
constexpr int kStep = 64;
constexpr int kMinSide = 512;
constexpr int kMaxSide = 2048;
// Context around the mask, so the model sees what the edit must match.
constexpr int kMinPadding = 64;
constexpr int kPaddingDivisor = 4;

// The painted pixels of the mask, found tile by tile, skipping null tiles.
QRect maskBounds(const TileStore& tiles) {
  QRect bounds;
  for (std::size_t index = 0; index < tiles.tileCount(); ++index) {
    const QImage& tile = tiles.tile(index, TileStore::Plane::Mask);
    if (tile.isNull()) {
      continue;
    }
    const QPoint origin = tiles.tileRect(index).topLeft();
    const auto painted = [](uchar alpha) { return alpha != 0; };
    for (int y = 0; y < tile.height(); ++y) {
      const std::span<const uchar> line(tile.constScanLine(y),
                                        static_cast<std::size_t>(tile.width()));
      const auto first = std::ranges::find_if(line, painted);
      if (first == line.end()) {
        continue;
      }
      const auto last = std::ranges::find_if(line | std::views::reverse, painted);
      const auto left = static_cast<int>(first - line.begin());
      const auto right = static_cast<int>(line.rend() - last) - 1;
      bounds |= QRect(origin + QPoint(left, y), origin + QPoint(right, y));
    }
  }
  return bounds;
}

int modelSide(int side, double scale) {
  const auto steps = static_cast<int>(std::lround(side * scale / kStep));
  return std::clamp(steps * kStep, kMinSide, kMaxSide);
}

// The alpha of an Alpha8 image as gray, so a PNG reader sees the mask.
QImage grayOf(const QImage& alpha) {
  QImage gray(alpha.size(), QImage::Format_Grayscale8);
  if (gray.isNull()) {
    return gray;
  }
  for (int y = 0; y < alpha.height(); ++y) {
    std::memcpy(gray.scanLine(y), alpha.constScanLine(y), static_cast<std::size_t>(alpha.width()));
  }
  return gray;
}

}  // namespace

std::optional<InpaintCrop> planInpaint(QRect bounds, QSize image) {
  const QRect area = bounds.intersected(QRect(QPoint(), image));
  if (area.isEmpty()) {
    return std::nullopt;
  }
  const int padding =
      std::max(kMinPadding, std::max(area.width(), area.height()) / kPaddingDivisor);
  const QRect padded =
      area.adjusted(-padding, -padding, padding, padding).intersected(QRect(QPoint(), image));
  const double scale = std::sqrt(kPixels / (static_cast<double>(padded.width()) * padded.height()));
  return InpaintCrop{
      .area = padded,
      .size = {modelSide(padded.width(), scale), modelSide(padded.height(), scale)},
  };
}

std::optional<InpaintInput> inpaintInput(const TileStore& tiles) {
  const std::optional<InpaintCrop> crop = planInpaint(maskBounds(tiles), tiles.size());
  if (!crop) {
    return std::nullopt;
  }
  QImage area_mask = tiles.toImage(TileStore::Plane::Mask).copy(crop->area);
  QImage image(crop->area.size(), QImage::Format_RGB888);
  const QImage layers = tiles.toImage();
  if (area_mask.isNull() || image.isNull() || layers.isNull()) {
    return std::nullopt;
  }
  // The model draws opaque pixels, so clear ones show as white.
  image.fill(Qt::white);
  {
    QPainter painter(&image);
    painter.drawImage(QPoint(), layers, crop->area);
  }
  const QImage gray = grayOf(area_mask);
  InpaintInput input{
      .crop = *crop,
      .image = image.scaled(crop->size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation),
      .mask = gray.scaled(crop->size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation),
      .area_mask = std::move(area_mask),
  };
  if (input.image.isNull() || input.mask.isNull()) {
    return std::nullopt;
  }
  return input;
}

std::optional<TileStore::LayerContent> inpaintLayer(const InpaintInput& input, const QImage& result,
                                                    const QString& name) {
  if (result.isNull()) {
    return std::nullopt;
  }
  QImage layer =
      result.scaled(input.crop.area.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
          .convertToFormat(TileStore::kFormat);
  if (layer.isNull()) {
    return std::nullopt;
  }
  {
    QPainter painter(&layer);
    painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    painter.drawImage(QPoint(), input.area_mask);
  }
  return TileStore::LayerContent{
      .name = name,
      .visible = true,
      .opacity = 1.0,
      .blend = TileStore::BlendMode::Normal,
      .image = std::move(layer),
      .offset = input.crop.area.topLeft(),
  };
}

}  // namespace pinsel
