#include "brush/brush.hpp"

#include <QLineF>
#include <QPainter>
#include <QRect>
#include <algorithm>

namespace pinsel {

namespace {

constexpr double kSpacing = 0.25;

}  // namespace

Stroke::Stroke(TileStore& store, BrushSettings settings, TileStore::Plane plane)
    : store_(&store),
      settings_(settings),
      plane_(plane),
      spacing_(std::max(1.0, settings.size * kSpacing)) {
  store_->commit();
  before_.reserve(store_->tileCount());
  for (std::size_t index = 0; index < store_->tileCount(); ++index) {
    before_.push_back(store_->tile(index, plane_));
  }
  coverage_.resize(store_->tileCount());
}

void Stroke::moveTo(QPointF point) {
  if (!last_) {
    dab(point);
    last_ = point;
    return;
  }
  const QLineF path(*last_, point);
  const double length = path.length();
  if (length < spacing_) {
    return;
  }
  const auto steps = static_cast<int>(length / spacing_);
  for (int step = 1; step <= steps; ++step) {
    dab(path.pointAt(step * spacing_ / length));
  }
  last_ = path.pointAt(steps * spacing_ / length);
}

void Stroke::dab(QPointF centre) {
  const double radius = settings_.size / 2.0;
  const QRectF circle(centre.x() - radius, centre.y() - radius, settings_.size, settings_.size);
  const QRect area = circle.toAlignedRect();
  const auto draw = [&](QPainter& painter, std::size_t index) {
    const QRect rect = store_->tileRect(index);
    QImage& coverage = coverage_.at(index);
    if (coverage.isNull()) {
      coverage = QImage(rect.size(), QImage::Format_Alpha8);
      coverage.fill(Qt::transparent);
    }
    {
      QPainter mask(&coverage);
      mask.setRenderHint(QPainter::Antialiasing);
      mask.translate(-rect.topLeft());
      mask.setPen(Qt::NoPen);
      mask.setBrush(Qt::black);
      mask.drawEllipse(circle);
    }
    const QRect region = area.intersected(rect);
    QImage paint(region.size(), QImage::Format_ARGB32_Premultiplied);
    paint.fill(plane_ == TileStore::Plane::Mask ? QColor(Qt::black) : settings_.color);
    {
      QPainter shape(&paint);
      shape.setCompositionMode(QPainter::CompositionMode_DestinationIn);
      shape.drawImage(QPoint(), coverage, region.translated(-rect.topLeft()));
    }
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    if (before_.at(index).isNull()) {
      painter.fillRect(rect, Qt::transparent);
    } else {
      painter.drawImage(rect.topLeft(), before_.at(index));
    }
    painter.setCompositionMode(settings_.erase ? QPainter::CompositionMode_DestinationOut
                                               : QPainter::CompositionMode_SourceOver);
    painter.setOpacity(settings_.opacity);
    painter.drawImage(region.topLeft(), paint);
  };
  store_->paint(area, draw, plane_);
}

}  // namespace pinsel
