#include <gtest/gtest.h>

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QPointF>
#include <cstddef>
#include <tuple>
#include <vector>

#include "brush/brush.hpp"
#include "tiles/tile_store.hpp"

namespace {

using pinsel::BrushSettings;
using pinsel::Stroke;
using pinsel::TileStore;

TileStore whiteStore() {
  QImage image(1000, 300, TileStore::kFormat);
  image.fill(Qt::white);
  TileStore store(image);
  std::ignore = store.takeDirty();
  return store;
}

QColor at(const TileStore& store, QPoint point) {
  return store.toImage().pixelColor(point);
}

TEST(Stroke, DrawsAnUnbrokenLineAcrossTiles) {
  TileStore store = whiteStore();
  {
    Stroke stroke(store, {.size = 10, .opacity = 1.0, .color = Qt::black, .erase = false});
    stroke.moveTo(QPointF(100, 150));
    stroke.moveTo(QPointF(900, 150));
  }
  const QImage image = store.toImage();
  int gaps = 0;
  for (int x = 100; x <= 900; ++x) {
    gaps += image.pixelColor(x, 150) == QColor(Qt::black) ? 0 : 1;
  }
  EXPECT_EQ(gaps, 0);
  EXPECT_EQ(image.pixelColor(500, 140), QColor(Qt::white));
  EXPECT_EQ(store.takeDirty(), (std::vector<std::size_t>{0, 1}));
}

TEST(Stroke, KeepsItsOpacityWhereItCrossesItself) {
  TileStore store = whiteStore();
  const BrushSettings half{.size = 20, .opacity = 0.5, .color = Qt::black, .erase = false};
  {
    Stroke stroke(store, half);
    stroke.moveTo(QPointF(100, 150));
    stroke.moveTo(QPointF(300, 150));
    stroke.moveTo(QPointF(100, 150));
  }
  const QColor once = at(store, QPoint(200, 150));
  EXPECT_NEAR(once.red(), 128, 1);

  {
    Stroke stroke(store, half);
    stroke.moveTo(QPointF(200, 150));
  }
  EXPECT_NEAR(at(store, QPoint(200, 150)).red(), 64, 1);
}

TEST(Stroke, EraserClearsToTransparent) {
  TileStore store = whiteStore();
  {
    Stroke stroke(store, {.size = 20, .opacity = 1.0, .color = Qt::black, .erase = true});
    stroke.moveTo(QPointF(200, 150));
  }
  EXPECT_EQ(at(store, QPoint(200, 150)).alpha(), 0);
  EXPECT_EQ(at(store, QPoint(200, 100)), QColor(Qt::white));
}

TEST(Stroke, EachStrokeIsOneUndoStep) {
  TileStore store = whiteStore();
  for (const QColor color : {Qt::red, Qt::blue}) {
    Stroke stroke(store, {.size = 20, .opacity = 1.0, .color = color, .erase = false});
    stroke.moveTo(QPointF(100, 150));
    stroke.moveTo(QPointF(700, 150));
  }
  ASSERT_TRUE(store.undo());
  EXPECT_EQ(at(store, QPoint(600, 150)), QColor(Qt::red));
  ASSERT_TRUE(store.undo());
  EXPECT_EQ(at(store, QPoint(600, 150)), QColor(Qt::white));
  EXPECT_FALSE(store.undo());
}

}  // namespace
