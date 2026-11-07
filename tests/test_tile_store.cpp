#include <gtest/gtest.h>

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QRect>
#include <cstddef>
#include <tuple>
#include <vector>

#include "tiles/tile_store.hpp"

namespace {

using pinsel::TileStore;
using Indices = std::vector<std::size_t>;

constexpr int kTile = TileStore::kTileSize;

QImage filled(QSize size, QColor color) {
  QImage image(size, TileStore::kFormat);
  image.fill(color);
  return image;
}

// A clean 3 x 2 store: the right column and bottom row are partial tiles.
TileStore cleanStore() {
  TileStore store(filled(QSize((2 * kTile) + 100, kTile + 50), Qt::white));
  std::ignore = store.takeDirty();
  return store;
}

void fill(TileStore& store, QRect area, QColor color) {
  store.paint(area,
              [&](QPainter& painter, std::size_t /*tile*/) { painter.fillRect(area, color); });
}

QColor at(const TileStore& store, QPoint point) {
  return store.toImage().pixelColor(point);
}

TEST(TileStore, CutsTheImageIntoTilesClippedAtTheEdges) {
  const TileStore store = cleanStore();
  ASSERT_EQ(store.tileCount(), 6U);
  EXPECT_EQ(store.tileRect(0), QRect(0, 0, kTile, kTile));
  EXPECT_EQ(store.tileRect(2), QRect(2 * kTile, 0, 100, kTile));
  EXPECT_EQ(store.tileRect(5), QRect(2 * kTile, kTile, 100, 50));
  EXPECT_EQ(store.tile(5).size(), QSize(100, 50));
}

TEST(TileStore, PutsTheTilesBackTogetherInTheStoreFormat) {
  QImage image(700, 600, QImage::Format_ARGB32);
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      image.setPixelColor(x, y, QColor(x % 256, y % 256, (x + y) % 256));
    }
  }
  const QImage joined = TileStore(image).toImage();
  EXPECT_EQ(joined.format(), TileStore::kFormat);
  EXPECT_EQ(joined, image.convertToFormat(TileStore::kFormat));
}

TEST(TileStore, StartsWithEveryTileDirtyAndTakingThemCleansThem) {
  TileStore store(filled(QSize(kTile + 1, 1), Qt::white));
  EXPECT_EQ(store.takeDirty(), (Indices{0, 1}));
  EXPECT_TRUE(store.takeDirty().empty());
}

TEST(TileStore, PaintDirtiesOnlyTheTilesItTouches) {
  TileStore store = cleanStore();
  fill(store, QRect(kTile - 10, kTile - 10, 20, 20), Qt::red);
  EXPECT_EQ(store.takeDirty(), (Indices{0, 1, 3, 4}));
  fill(store, QRect(10, 10, 5, 5), Qt::red);
  EXPECT_EQ(store.takeDirty(), (Indices{0}));
}

TEST(TileStore, PaintStaysInsideTheArea) {
  TileStore store = cleanStore();
  const QRect area(kTile - 10, 20, 20, 20);
  store.paint(area, [&](QPainter& painter, std::size_t /*tile*/) {
    painter.fillRect(0, 0, 2000, 2000, Qt::red);
  });
  QImage expected = filled(store.size(), Qt::white);
  QPainter(&expected).fillRect(area, Qt::red);
  EXPECT_EQ(store.toImage(), expected);
  EXPECT_EQ(store.takeDirty(), (Indices{0, 1}));
}

TEST(TileStore, PaintOutsideTheImageChangesNothing) {
  TileStore store = cleanStore();
  fill(store, QRect(-100, -100, 50, 50), Qt::red);
  EXPECT_TRUE(store.takeDirty().empty());
  EXPECT_FALSE(store.canUndo());
}

TEST(TileStore, UndoRestoresTheTilesAndRedoPutsTheChangeBack) {
  TileStore store = cleanStore();
  const QPoint point(kTile + 5, 5);
  fill(store, QRect(point, QSize(1, 1)), Qt::red);
  store.commit();
  std::ignore = store.takeDirty();

  ASSERT_TRUE(store.undo());
  EXPECT_EQ(at(store, point), QColor(Qt::white));
  EXPECT_EQ(store.takeDirty(), (Indices{1}));
  EXPECT_FALSE(store.canUndo());

  ASSERT_TRUE(store.redo());
  EXPECT_EQ(at(store, point), QColor(Qt::red));
  EXPECT_EQ(store.takeDirty(), (Indices{1}));
  EXPECT_FALSE(store.canRedo());
}

TEST(TileStore, OneStepUndoesEveryPaintSinceTheLastCommit) {
  TileStore store = cleanStore();
  const QPoint point(5, 5);
  fill(store, QRect(point, QSize(1, 1)), Qt::red);
  fill(store, QRect(point, QSize(1, 1)), Qt::blue);
  ASSERT_TRUE(store.undo());
  EXPECT_EQ(at(store, point), QColor(Qt::white));
  EXPECT_FALSE(store.undo());
}

TEST(TileStore, StepsUndoInReverseOrder) {
  TileStore store = cleanStore();
  const QPoint point(5, 5);
  fill(store, QRect(point, QSize(1, 1)), Qt::red);
  store.commit();
  fill(store, QRect(point, QSize(1, 1)), Qt::blue);
  store.commit();
  ASSERT_TRUE(store.undo());
  EXPECT_EQ(at(store, point), QColor(Qt::red));
  ASSERT_TRUE(store.undo());
  EXPECT_EQ(at(store, point), QColor(Qt::white));
}

TEST(TileStore, CommitWithoutAChangeAddsNoStep) {
  TileStore store = cleanStore();
  store.commit();
  EXPECT_FALSE(store.canUndo());
  EXPECT_FALSE(store.undo());
}

TEST(TileStore, ANewChangeAfterUndoDropsTheRedoSteps) {
  TileStore store = cleanStore();
  fill(store, QRect(5, 5, 1, 1), Qt::red);
  store.commit();
  ASSERT_TRUE(store.undo());
  fill(store, QRect(5, 5, 1, 1), Qt::blue);
  EXPECT_FALSE(store.canRedo());
  EXPECT_FALSE(store.redo());
  EXPECT_EQ(at(store, QPoint(5, 5)), QColor(Qt::blue));
}

TEST(TileStore, UndoGivesBackTheTileItSavedWithoutACopy) {
  TileStore store = cleanStore();
  const uchar* before = store.tile(0).constBits();
  fill(store, QRect(5, 5, 1, 1), Qt::red);
  EXPECT_NE(store.tile(0).constBits(), before);
  ASSERT_TRUE(store.undo());
  EXPECT_EQ(store.tile(0).constBits(), before);
}

TEST(TileStore, PaintNamesEachTileItDrawsOn) {
  TileStore store = cleanStore();
  Indices drawn;
  store.paint(QRect(kTile - 1, kTile - 1, 2, 2),
              [&](QPainter& /*painter*/, std::size_t tile) { drawn.push_back(tile); });
  EXPECT_EQ(drawn, (Indices{0, 1, 3, 4}));
}

TEST(TileStore, DropsTheOldestStepsOverTheUndoBudget) {
  TileStore store = cleanStore();
  const auto tile_bytes = static_cast<std::size_t>(store.tile(0).sizeInBytes());
  store.setUndoBudget(2 * tile_bytes);
  for (const QColor color : {Qt::red, Qt::green, Qt::blue}) {
    fill(store, QRect(5, 5, 1, 1), color);
    store.commit();
  }
  ASSERT_TRUE(store.undo());
  ASSERT_TRUE(store.undo());
  EXPECT_EQ(at(store, QPoint(5, 5)), QColor(Qt::red));
  EXPECT_FALSE(store.undo());
}

TEST(TileStore, KeepsTheNewestStepEvenOverTheBudget) {
  TileStore store = cleanStore();
  store.setUndoBudget(0);
  fill(store, QRect(5, 5, 1, 1), Qt::red);
  store.commit();
  EXPECT_TRUE(store.undo());
}

}  // namespace
