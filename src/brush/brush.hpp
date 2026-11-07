#pragma once

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <cstddef>
#include <optional>
#include <vector>

#include "tiles/tile_store.hpp"

namespace pinsel {

struct BrushSettings {
  // Diameter in image pixels.
  double size = 40.0;
  // 0 to 1, for the whole stroke: where the stroke crosses itself it stays at
  // this opacity rather than building up.
  double opacity = 1.0;
  QColor color = Qt::black;
  bool erase = false;
};

// One stroke of a round brush, from press to release. It commits the store's
// open undo step when it starts, so the stroke becomes a step of its own. It
// lays dabs along the path a quarter of the diameter apart. The stroke draws
// its coverage into a mask per tile and composes each touched tile from that
// mask and the tile as it was before the stroke, so opacity holds for the
// whole stroke.
class Stroke {
 public:
  // The store must outlive the stroke.
  Stroke(TileStore& store, BrushSettings settings);

  // In image pixels. The first call lays the first dab.
  void moveTo(QPointF point);

 private:
  void dab(QPointF centre);

  TileStore* store_;
  BrushSettings settings_;
  double spacing_;
  std::optional<QPointF> last_;
  // Tiles as they were before the stroke, and the stroke's coverage of each,
  // null until the stroke reaches that tile.
  std::vector<QImage> before_;
  std::vector<QImage> coverage_;
};

}  // namespace pinsel
