#include <QPainter>
#include <QPainterPath>
#include <QRect>
#include <cstddef>

#include "brush/brush.hpp"

namespace pinsel {

void fillMask(TileStore& store, const QPolygonF& outline, double opacity, bool erase) {
  if (outline.size() < 3 || (erase && !store.hasMask())) {
    return;
  }
  QPainterPath path;
  path.addPolygon(outline);
  path.closeSubpath();
  path.setFillRule(Qt::WindingFill);
  store.commit();
  store.paint(
      path.boundingRect().toAlignedRect(),
      [&](QPainter& painter, std::size_t /*tile*/) {
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setCompositionMode(erase ? QPainter::CompositionMode_DestinationOut
                                         : QPainter::CompositionMode_SourceOver);
        painter.setOpacity(opacity);
        painter.fillPath(path, Qt::black);
      },
      TileStore::Plane::Mask);
  store.commit();
}

}  // namespace pinsel
