#include "tiles/tile_store.hpp"

#include <QPainter>
#include <algorithm>
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
  tile_count_ = columns_ * tilesAcross(image.height());
  Layer background = emptyLayer();
  background.id = ++last_layer_id_;
  background.name = QStringLiteral("Background");
  for (std::size_t index = 0; index < tile_count_; ++index) {
    // Converting tile by tile, not the whole image first, keeps the peak at the
    // source plus the tiles. The 32-bit formats convert in place.
    QImage tile = image.copy(tileRect(index));
    tile.convertTo(kFormat);
    if (tile.isNull() || tile.format() != kFormat) {
      *this = TileStore();
      return;
    }
    background.tiles.at(index) = std::move(tile);
  }
  background.dirty.assign(tile_count_, true);
  layers_.push_back(std::move(background));
  mask_ = emptyLayer();
}

TileStore::Layer TileStore::emptyLayer() const {
  Layer layer;
  layer.tiles.resize(tile_count_);
  layer.dirty.assign(tile_count_, false);
  layer.saved.assign(tile_count_, false);
  return layer;
}

TileStore::Layer& TileStore::target(Plane plane) {
  return plane == Plane::Mask ? mask_ : layers_.at(current_);
}

const TileStore::Layer& TileStore::target(Plane plane) const {
  return plane == Plane::Mask ? mask_ : layers_.at(current_);
}

TileStore::Layer* TileStore::find(std::uint64_t id) {
  if (id == kMaskId) {
    return &mask_;
  }
  const auto found = std::ranges::find(layers_, id, &Layer::id);
  return found == layers_.end() ? nullptr : &*found;
}

QRect TileStore::tileRect(std::size_t index) const {
  const auto column = static_cast<int>(index % columns_);
  const auto row = static_cast<int>(index / columns_);
  return QRect(column * kTileSize, row * kTileSize, kTileSize, kTileSize)
      .intersected(QRect(QPoint(), size_));
}

const QImage& TileStore::tile(std::size_t index, Plane plane) const {
  return target(plane).tiles.at(index);
}

QImage TileStore::toImage(Plane plane) const {
  QImage image(size_, plane == Plane::Image ? kFormat : kMaskFormat);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  const auto draw = [&](const Layer& layer) {
    for (std::size_t index = 0; index < layer.tiles.size(); ++index) {
      if (!layer.tiles.at(index).isNull()) {
        painter.drawImage(tileRect(index).topLeft(), layer.tiles.at(index));
      }
    }
  };
  if (plane == Plane::Mask) {
    draw(mask_);
    return image;
  }
  for (const Layer& layer : layers_) {
    if (layer.visible) {
      draw(layer);
    }
  }
  return image;
}

void TileStore::setCurrentLayer(std::size_t layer) {
  if (layer < layers_.size()) {
    current_ = layer;
  }
}

void TileStore::restack(const std::function<void()>& change) {
  commit();
  Step step;
  step.stack = Stack{.layers = layers_, .current = current_};
  change();
  step.revision = ++last_revision_;
  step.stack_bytes = lostBytes(step.stack->layers);
  pushStep(std::move(step));
}

void TileStore::addLayer() {
  if (layers_.empty()) {
    return;
  }
  restack([this] {
    Layer layer = emptyLayer();
    layer.id = ++last_layer_id_;
    layer.name = QStringLiteral("Layer %1").arg(layer.id - 1);
    ++current_;
    layers_.insert(layers_.begin() + static_cast<std::ptrdiff_t>(current_), std::move(layer));
  });
}

bool TileStore::removeLayer() {
  if (layers_.size() < 2) {
    return false;
  }
  restack([this] {
    layers_.erase(layers_.begin() + static_cast<std::ptrdiff_t>(current_));
    current_ = current_ > 0 ? current_ - 1 : 0;
  });
  return true;
}

void TileStore::moveLayer(std::size_t from, std::size_t to) {
  if (from == to || from >= layers_.size() || to >= layers_.size()) {
    return;
  }
  restack([this, from, to] {
    const auto first = layers_.begin();
    const auto at = [first](std::size_t position) {
      return first + static_cast<std::ptrdiff_t>(position);
    };
    if (from < to) {
      std::rotate(at(from), at(from + 1), at(to + 1));
    } else {
      std::rotate(at(to), at(from), at(from + 1));
    }
    current_ = to;
  });
}

void TileStore::setLayerVisible(std::size_t layer, bool visible) {
  if (layer >= layers_.size() || layers_.at(layer).visible == visible) {
    return;
  }
  restack([this, layer, visible] { layers_.at(layer).visible = visible; });
}

void TileStore::save(Layer& layer, std::size_t index) {
  if (!layer.saved.at(index)) {
    open_.tiles.push_back({.layer = layer.id, .index = index, .image = layer.tiles.at(index)});
    layer.saved.at(index) = true;
  }
}

void TileStore::paint(QRect area, const std::function<void(QPainter&, std::size_t tile)>& draw,
                      Plane plane) {
  const QRect bounded = area.intersected(QRect(QPoint(), size_));
  if (bounded.isEmpty() || layers_.empty()) {
    return;
  }
  Layer& layer = target(plane);
  open_.revision = ++last_revision_;
  const auto first_column = static_cast<std::size_t>(bounded.left() / kTileSize);
  const auto last_column = static_cast<std::size_t>(bounded.right() / kTileSize);
  const auto first_row = static_cast<std::size_t>(bounded.top() / kTileSize);
  const auto last_row = static_cast<std::size_t>(bounded.bottom() / kTileSize);
  for (std::size_t row = first_row; row <= last_row; ++row) {
    for (std::size_t column = first_column; column <= last_column; ++column) {
      const std::size_t index = (row * columns_) + column;
      save(layer, index);
      const QRect rect = tileRect(index);
      QImage& tile = layer.tiles.at(index);
      if (tile.isNull()) {
        tile = QImage(rect.size(), plane == Plane::Mask ? kMaskFormat : kFormat);
        tile.fill(Qt::transparent);
      }
      QPainter painter(&tile);
      painter.translate(-rect.topLeft());
      painter.setClipRect(bounded.intersected(rect));
      draw(painter, index);
      layer.dirty.at(index) = true;
    }
  }
}

void TileStore::pushStep(Step step) {
  undo_.push_back(std::move(step));
  redo_.clear();
  trimUndo();
}

void TileStore::commit() {
  if (open_.tiles.empty()) {
    return;
  }
  for (const SavedTile& saved : open_.tiles) {
    if (Layer* layer = find(saved.layer)) {
      layer->saved.at(saved.index) = false;
    }
  }
  pushStep(std::exchange(open_, {}));
}

void TileStore::clearMask() {
  commit();
  for (std::size_t index = 0; index < mask_.tiles.size(); ++index) {
    if (!mask_.tiles.at(index).isNull()) {
      save(mask_, index);
      mask_.tiles.at(index) = QImage();
      mask_.dirty.at(index) = true;
    }
  }
  if (!open_.tiles.empty()) {
    open_.revision = ++last_revision_;
  }
  commit();
}

void TileStore::setUndoBudget(std::size_t bytes) {
  undo_budget_ = bytes;
  trimUndo();
}

std::size_t TileStore::bytes() const {
  std::size_t bytes = layerBytes(mask_);
  for (const Layer& layer : layers_) {
    bytes += layerBytes(layer);
  }
  return bytes;
}

std::size_t TileStore::layerBytes(const Layer& layer) {
  std::size_t bytes = 0;
  for (const QImage& tile : layer.tiles) {
    bytes += static_cast<std::size_t>(tile.sizeInBytes());
  }
  return bytes;
}

namespace {

template <typename Step>
std::size_t stepBytes(const Step& step) {
  std::size_t bytes = step.stack_bytes;
  for (const auto& saved : step.tiles) {
    bytes += static_cast<std::size_t>(saved.image.sizeInBytes());
  }
  return bytes;
}

}  // namespace

std::size_t TileStore::undoBytes() const {
  std::size_t bytes = 0;
  for (const Step& step : undo_) {
    bytes += stepBytes(step);
  }
  return bytes;
}

void TileStore::trimUndo() {
  std::size_t bytes = undoBytes();
  std::size_t dropped = 0;
  while (bytes > undo_budget_ && undo_.size() - dropped > 1) {
    bytes -= stepBytes(undo_.at(dropped));
    base_revision_ = undo_.at(dropped).revision;
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
  return !open_.tiles.empty() || !undo_.empty();
}

bool TileStore::canRedo() const {
  return open_.tiles.empty() && !redo_.empty();
}

std::uint64_t TileStore::revision() const {
  if (!open_.tiles.empty()) {
    return open_.revision;
  }
  return undo_.empty() ? base_revision_ : undo_.back().revision;
}

std::vector<std::size_t> TileStore::takeDirty(Plane plane) {
  if (plane == Plane::Image && layers_.empty()) {
    return {};
  }
  return takeFlags(target(plane).dirty);
}

std::vector<std::size_t> TileStore::takeLayerDirty(std::size_t layer) {
  return takeFlags(layers_.at(layer).dirty);
}

std::vector<std::size_t> TileStore::takeFlags(std::vector<bool>& flags) {
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
  if (step.stack) {
    step.stack_bytes = swapStack(*step.stack);
  }
  for (SavedTile& saved : step.tiles) {
    if (Layer* layer = find(saved.layer)) {
      std::swap(layer->tiles.at(saved.index), saved.image);
      layer->dirty.at(saved.index) = true;
    }
  }
}

// A layer in both stacks has the same tiles in each, since a change to the
// stack is a step of its own, so it keeps its dirty tiles. A layer that comes
// back has all of its painted tiles dirty.
std::size_t TileStore::swapStack(Stack& stack) {
  for (Layer& incoming : stack.layers) {
    if (const Layer* present = find(incoming.id)) {
      incoming.dirty = present->dirty;
    } else {
      for (std::size_t index = 0; index < incoming.tiles.size(); ++index) {
        incoming.dirty.at(index) = !incoming.tiles.at(index).isNull();
      }
    }
  }
  std::swap(layers_, stack.layers);
  std::swap(current_, stack.current);
  return lostBytes(stack.layers);
}

std::size_t TileStore::lostBytes(const std::vector<Layer>& layers) const {
  std::size_t bytes = 0;
  for (const Layer& layer : layers) {
    if (std::ranges::find(layers_, layer.id, &Layer::id) == layers_.end()) {
      bytes += layerBytes(layer);
    }
  }
  return bytes;
}

}  // namespace pinsel
