#include <gtest/gtest.h>

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QPolygonF>
#include <cstddef>
#include <tuple>
#include <vector>

#include "brush/brush.hpp"
#include "tiles/tile_store.hpp"

namespace {

using pinsel::BrushSettings;
using pinsel::fillMask;
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

void strokeAcross(TileStore& store, QColor color) {
  Stroke stroke(store, {.size = 20, .opacity = 1.0, .color = color, .erase = false});
  stroke.moveTo(QPointF(100, 150));
  stroke.moveTo(QPointF(700, 150));
}

TEST(Stroke, EachStrokeIsOneUndoStep) {
  TileStore store = whiteStore();
  strokeAcross(store, Qt::red);
  strokeAcross(store, Qt::blue);
  ASSERT_TRUE(store.undo());
  EXPECT_EQ(at(store, QPoint(600, 150)), QColor(Qt::red));
  ASSERT_TRUE(store.undo());
  EXPECT_EQ(at(store, QPoint(600, 150)), QColor(Qt::white));
  EXPECT_FALSE(store.undo());
}

TEST(Stroke, OnTheMaskLeavesTheImageAlone) {
  TileStore store = whiteStore();
  constexpr auto kMask = TileStore::Plane::Mask;
  {
    Stroke stroke(store, {.size = 20, .opacity = 0.5, .color = Qt::blue, .erase = false}, kMask);
    stroke.moveTo(QPointF(200, 150));
    stroke.moveTo(QPointF(600, 150));
  }
  EXPECT_EQ(at(store, QPoint(550, 150)), QColor(Qt::white));
  EXPECT_NEAR(store.toImage(kMask).pixelColor(550, 150).alpha(), 128, 1);
  EXPECT_EQ(store.toImage(kMask).pixelColor(550, 100).alpha(), 0);
  {
    Stroke stroke(store, {.size = 20, .opacity = 1.0, .color = Qt::blue, .erase = true}, kMask);
    stroke.moveTo(QPointF(550, 150));
  }
  EXPECT_EQ(store.toImage(kMask).pixelColor(550, 150).alpha(), 0);
  EXPECT_NEAR(store.toImage(kMask).pixelColor(300, 150).alpha(), 128, 1);
}

int maskAt(const TileStore& store, QPoint point) {
  return store.toImage(TileStore::Plane::Mask).pixelColor(point).alpha();
}

// A rectangle across the seam between the first two tiles.
QPolygonF acrossTiles() {
  return {{QPointF(400, 50), QPointF(700, 50), QPointF(700, 250), QPointF(400, 250)}};
}

TEST(FillMask, FillsInsideTheOutlineAcrossTiles) {
  TileStore store = whiteStore();
  fillMask(store, acrossTiles(), 1.0, false);
  EXPECT_EQ(maskAt(store, QPoint(450, 100)), 255);
  EXPECT_EQ(maskAt(store, QPoint(650, 200)), 255);
  EXPECT_EQ(maskAt(store, QPoint(350, 100)), 0);
  EXPECT_EQ(maskAt(store, QPoint(750, 200)), 0);
  EXPECT_EQ(at(store, QPoint(500, 100)), QColor(Qt::white));
  EXPECT_EQ(store.takeDirty(TileStore::Plane::Mask), (std::vector<std::size_t>{0, 1}));
}

TEST(FillMask, FillsWhereTheOutlineCrossesItself) {
  TileStore store = whiteStore();
  const QPolygonF star{
      {QPointF(150, 20), QPointF(240, 280), QPointF(20, 110), QPointF(280, 110), QPointF(60, 280)}};
  fillMask(store, star, 1.0, false);
  EXPECT_EQ(maskAt(store, QPoint(150, 150)), 255);
  EXPECT_EQ(maskAt(store, QPoint(150, 50)), 255);
}

TEST(FillMask, EraseClearsOnlyInsideTheOutline) {
  TileStore store = whiteStore();
  fillMask(store, QPolygonF{{QPointF(0, 0), QPointF(1000, 0), QPointF(1000, 300), QPointF(0, 300)}},
           0.5, false);
  fillMask(store, acrossTiles(), 1.0, true);
  EXPECT_EQ(maskAt(store, QPoint(500, 150)), 0);
  EXPECT_NEAR(maskAt(store, QPoint(100, 150)), 128, 1);
}

TEST(FillMask, EachFillIsOneUndoStep) {
  TileStore store = whiteStore();
  fillMask(store, acrossTiles(), 1.0, false);
  fillMask(store, acrossTiles(), 1.0, true);
  EXPECT_EQ(maskAt(store, QPoint(500, 150)), 0);
  ASSERT_TRUE(store.undo());
  EXPECT_EQ(maskAt(store, QPoint(500, 150)), 255);
  ASSERT_TRUE(store.undo());
  EXPECT_EQ(maskAt(store, QPoint(500, 150)), 0);
  EXPECT_FALSE(store.undo());
}

TEST(FillMask, IgnoresAnOutlineWithoutArea) {
  TileStore store = whiteStore();
  fillMask(store, QPolygonF{{QPointF(10, 10), QPointF(200, 200)}}, 1.0, false);
  fillMask(store, acrossTiles(), 1.0, true);
  EXPECT_FALSE(store.canUndo());
  EXPECT_FALSE(store.hasMask());
}

}  // namespace
