#include "tiles/tile_store.hpp"

#include <QPainter>
#include <cstddef>
#include <utility>

namespace pinsel {

namespace {

std::size_t tilesAcross(int pixels) {
  return static_cast<std::size_t>((pixels + TileStore::kTileSize - 1) / TileStore::kTileSize);
}

}  // namespace

TileStore::TileStore(const QImage& image) {
  if (image.isNull()) {
    return;
  }
  size_ = image.size();
  columns_ = tilesAcross(image.width());
  const std::size_t count = columns_ * tilesAcross(image.height());
  planes_.at(planeIndex(Plane::Image)).reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    // Converting tile by tile, not the whole image first, keeps the peak at the
    // source plus the tiles. The 32-bit formats convert in place.
    QImage tile = image.copy(tileRect(index));
    tile.convertTo(kFormat);
    if (tile.isNull() || tile.format() != kFormat) {
      *this = TileStore();
      return;
    }
    planes_.at(planeIndex(Plane::Image)).push_back(std::move(tile));
  }
  planes_.at(planeIndex(Plane::Mask)).resize(count);
  for (std::size_t plane = 0; plane < kPlanes; ++plane) {
    dirty_.at(plane).assign(count, plane == planeIndex(Plane::Image));
    saved_.at(plane).assign(count, false);
  }
}

QRect TileStore::tileRect(std::size_t index) const {
  const auto column = static_cast<int>(index % columns_);
  const auto row = static_cast<int>(index / columns_);
  return QRect(column * kTileSize, row * kTileSize, kTileSize, kTileSize)
      .intersected(QRect(QPoint(), size_));
}

QImage TileStore::toImage(Plane plane) const {
  QImage image(size_, plane == Plane::Image ? kFormat : kMaskFormat);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.setCompositionMode(QPainter::CompositionMode_Source);
  const std::vector<QImage>& tiles = planes_.at(planeIndex(plane));
  for (std::size_t index = 0; index < tiles.size(); ++index) {
    painter.drawImage(tileRect(index).topLeft(), tiles.at(index));
  }
  return image;
}

void TileStore::save(Plane plane, std::size_t index) {
  std::vector<bool>& saved = saved_.at(planeIndex(plane));
  if (!saved.at(index)) {
    open_.push_back({.plane = plane, .index = index, .image = tile(index, plane)});
    saved.at(index) = true;
  }
}

void TileStore::paint(QRect area, const std::function<void(QPainter&, std::size_t tile)>& draw,
                      Plane plane) {
  const QRect bounded = area.intersected(QRect(QPoint(), size_));
  if (bounded.isEmpty()) {
    return;
  }
  const auto first_column = static_cast<std::size_t>(bounded.left() / kTileSize);
  const auto last_column = static_cast<std::size_t>(bounded.right() / kTileSize);
  const auto first_row = static_cast<std::size_t>(bounded.top() / kTileSize);
  const auto last_row = static_cast<std::size_t>(bounded.bottom() / kTileSize);
  for (std::size_t row = first_row; row <= last_row; ++row) {
    for (std::size_t column = first_column; column <= last_column; ++column) {
      const std::size_t index = (row * columns_) + column;
      save(plane, index);
      const QRect rect = tileRect(index);
      QImage& tile = planes_.at(planeIndex(plane)).at(index);
      if (plane == Plane::Mask && tile.isNull()) {
        tile = QImage(rect.size(), kMaskFormat);
        tile.fill(Qt::transparent);
      }
      QPainter painter(&tile);
      painter.translate(-rect.topLeft());
      painter.setClipRect(bounded.intersected(rect));
      draw(painter, index);
      dirty_.at(planeIndex(plane)).at(index) = true;
    }
  }
}

void TileStore::commit() {
  if (open_.empty()) {
    return;
  }
  for (const SavedTile& saved : open_) {
    saved_.at(planeIndex(saved.plane)).at(saved.index) = false;
  }
  undo_.push_back(std::exchange(open_, {}));
  redo_.clear();
  trimUndo();
}

void TileStore::clearMask() {
  commit();
  std::vector<QImage>& mask = planes_.at(planeIndex(Plane::Mask));
  for (std::size_t index = 0; index < mask.size(); ++index) {
    if (!mask.at(index).isNull()) {
      save(Plane::Mask, index);
      mask.at(index) = QImage();
      dirty_.at(planeIndex(Plane::Mask)).at(index) = true;
    }
  }
  commit();
}

void TileStore::setUndoBudget(std::size_t bytes) {
  undo_budget_ = bytes;
  trimUndo();
}

std::size_t TileStore::bytes() const {
  std::size_t bytes = 0;
  for (const std::vector<QImage>& tiles : planes_) {
    for (const QImage& tile : tiles) {
      bytes += static_cast<std::size_t>(tile.sizeInBytes());
    }
  }
  return bytes;
}

std::size_t TileStore::undoBytes() const {
  std::size_t bytes = 0;
  for (const Step& step : undo_) {
    for (const SavedTile& saved : step) {
      bytes += static_cast<std::size_t>(saved.image.sizeInBytes());
    }
  }
  return bytes;
}

void TileStore::trimUndo() {
  std::size_t bytes = undoBytes();
  std::size_t dropped = 0;
  while (bytes > undo_budget_ && undo_.size() - dropped > 1) {
    for (const SavedTile& saved : undo_.at(dropped)) {
      bytes -= static_cast<std::size_t>(saved.image.sizeInBytes());
    }
    ++dropped;
  }
  undo_.erase(undo_.begin(), undo_.begin() + static_cast<std::ptrdiff_t>(dropped));
}

bool TileStore::undo() {
  commit();
  if (undo_.empty()) {
    return false;
  }
  swapIn(undo_.back());
  redo_.push_back(std::move(undo_.back()));
  undo_.pop_back();
  return true;
}

bool TileStore::redo() {
  commit();
  if (redo_.empty()) {
    return false;
  }
  swapIn(redo_.back());
  undo_.push_back(std::move(redo_.back()));
  redo_.pop_back();
  return true;
}

bool TileStore::canUndo() const {
  return !open_.empty() || !undo_.empty();
}

bool TileStore::canRedo() const {
  return open_.empty() && !redo_.empty();
}

std::vector<std::size_t> TileStore::takeDirty(Plane plane) {
  std::vector<bool>& flags = dirty_.at(planeIndex(plane));
  std::vector<std::size_t> dirty;
  for (std::size_t index = 0; index < flags.size(); ++index) {
    if (flags.at(index)) {
      dirty.push_back(index);
      flags.at(index) = false;
    }
  }
  return dirty;
}

void TileStore::swapIn(Step& step) {
  for (SavedTile& saved : step) {
    std::swap(planes_.at(planeIndex(saved.plane)).at(saved.index), saved.image);
    dirty_.at(planeIndex(saved.plane)).at(saved.index) = true;
  }
}

}  // namespace pinsel
