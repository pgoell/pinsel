#include "tiles/tile_store.hpp"

#include <QPainter>
#include <utility>

namespace pinsel {

namespace {

std::size_t tilesAcross(int pixels) {
  return static_cast<std::size_t>((pixels + TileStore::kTileSize - 1) / TileStore::kTileSize);
}

}  // namespace

TileStore::TileStore(const QImage& image) {
  const QImage source = image.convertToFormat(kFormat);
  if (source.isNull()) {
    return;
  }
  size_ = source.size();
  columns_ = tilesAcross(source.width());
  const std::size_t count = columns_ * tilesAcross(source.height());
  tiles_.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    tiles_.push_back(source.copy(tileRect(index)));
  }
  dirty_.assign(count, true);
  saved_.assign(count, false);
}

QRect TileStore::tileRect(std::size_t index) const {
  const auto column = static_cast<int>(index % columns_);
  const auto row = static_cast<int>(index / columns_);
  return QRect(column * kTileSize, row * kTileSize, kTileSize, kTileSize)
      .intersected(QRect(QPoint(), size_));
}

QImage TileStore::toImage() const {
  QImage image(size_, kFormat);
  QPainter painter(&image);
  painter.setCompositionMode(QPainter::CompositionMode_Source);
  for (std::size_t index = 0; index < tiles_.size(); ++index) {
    painter.drawImage(tileRect(index).topLeft(), tiles_.at(index));
  }
  return image;
}

void TileStore::paint(QRect area, const std::function<void(QPainter&)>& draw) {
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
      if (!saved_.at(index)) {
        open_.push_back({.index = index, .image = tiles_.at(index)});
        saved_.at(index) = true;
      }
      const QRect rect = tileRect(index);
      QPainter painter(&tiles_.at(index));
      painter.translate(-rect.topLeft());
      painter.setClipRect(bounded.intersected(rect));
      draw(painter);
      dirty_.at(index) = true;
    }
  }
}

void TileStore::commit() {
  if (open_.empty()) {
    return;
  }
  for (const SavedTile& saved : open_) {
    saved_.at(saved.index) = false;
  }
  undo_.push_back(std::exchange(open_, {}));
  redo_.clear();
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

std::vector<std::size_t> TileStore::takeDirty() {
  std::vector<std::size_t> dirty;
  for (std::size_t index = 0; index < dirty_.size(); ++index) {
    if (dirty_.at(index)) {
      dirty.push_back(index);
      dirty_.at(index) = false;
    }
  }
  return dirty;
}

void TileStore::swapIn(Step& step) {
  for (SavedTile& saved : step) {
    std::swap(tiles_.at(saved.index), saved.image);
    dirty_.at(saved.index) = true;
  }
}

}  // namespace pinsel
