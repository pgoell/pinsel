#include <gtest/gtest.h>

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QRect>
#include <QSize>
#include <QString>
#include <cstddef>
#include <optional>

#include "inpaint/inpaint.hpp"
#include "tiles/tile_store.hpp"

namespace {

using pinsel::InpaintInput;
using pinsel::TileStore;

TileStore whiteStore(QSize size) {
  QImage image(size, TileStore::kFormat);
  image.fill(Qt::white);
  return TileStore(image);
}

void maskRect(TileStore& store, QRect area) {
  store.paint(
      area, [&](QPainter& painter, std::size_t) { painter.fillRect(area, Qt::black); },
      TileStore::Plane::Mask);
  store.commit();
}

TEST(PlanInpaint, PadsTheMaskAndScalesASmallBoxUpToTheModelSize) {
  const auto crop = pinsel::planInpaint(QRect(400, 400, 100, 100), QSize(2000, 2000));
  ASSERT_TRUE(crop);
  EXPECT_EQ(crop->area, QRect(336, 336, 228, 228));
  EXPECT_EQ(crop->size, QSize(1024, 1024));
}

TEST(PlanInpaint, KeepsTheBoxInsideTheImage) {
  const auto crop = pinsel::planInpaint(QRect(0, 0, 50, 50), QSize(800, 600));
  ASSERT_TRUE(crop);
  EXPECT_EQ(crop->area, QRect(0, 0, 114, 114));
}

TEST(PlanInpaint, KeepsTheShapeInSidesOfWholeSixtyFours) {
  const auto crop = pinsel::planInpaint(QRect(100, 100, 1800, 600), QSize(4000, 4000));
  ASSERT_TRUE(crop);
  EXPECT_EQ(crop->area, QRect(-350, -350, 2700, 1500).intersected(QRect(0, 0, 4000, 4000)));
  EXPECT_EQ(crop->size.width() % 64, 0);
  EXPECT_EQ(crop->size.height() % 64, 0);
  const double ratio = static_cast<double>(crop->area.width()) / crop->area.height();
  EXPECT_NEAR(static_cast<double>(crop->size.width()) / crop->size.height(), ratio, 0.1);
}

TEST(PlanInpaint, KeepsALongThinBoxWithinTheModelsLimits) {
  const auto crop = pinsel::planInpaint(QRect(0, 0, 8000, 10), QSize(8000, 10));
  ASSERT_TRUE(crop);
  EXPECT_EQ(crop->size, QSize(2048, 512));
}

TEST(PlanInpaint, AnEmptyMaskPlansNothing) {
  EXPECT_FALSE(pinsel::planInpaint(QRect(), QSize(100, 100)));
  EXPECT_FALSE(pinsel::planInpaint(QRect(200, 200, 10, 10), QSize(100, 100)));
}

TEST(InpaintInput, NeedsAMask) {
  const TileStore store = whiteStore(QSize(600, 600));
  EXPECT_FALSE(pinsel::inpaintInput(store));
}

TEST(InpaintInput, CropsAroundTheMaskAcrossTiles) {
  TileStore store = whiteStore(QSize(1200, 800));
  maskRect(store, QRect(480, 300, 80, 40));
  const std::optional<InpaintInput> input = pinsel::inpaintInput(store);
  ASSERT_TRUE(input);
  EXPECT_EQ(input->crop.area, QRect(416, 236, 208, 168));
  EXPECT_EQ(input->image.size(), input->crop.size);
  EXPECT_EQ(input->mask.size(), input->crop.size);
  EXPECT_EQ(input->area_mask.size(), input->crop.area.size());
  EXPECT_EQ(input->image.pixelColor(0, 0), QColor(Qt::white));
  const QPoint middle(input->mask.width() / 2, input->mask.height() / 2);
  EXPECT_EQ(qGray(input->mask.pixel(middle)), 255);
  EXPECT_EQ(qGray(input->mask.pixel(0, 0)), 0);
}

TEST(InpaintLayer, KeepsTheResultOnlyInTheMaskAndOneUndoRemovesIt) {
  TileStore store = whiteStore(QSize(1200, 800));
  maskRect(store, QRect(480, 300, 80, 40));
  const std::optional<InpaintInput> input = pinsel::inpaintInput(store);
  ASSERT_TRUE(input);
  QImage result(input->crop.size, QImage::Format_RGB32);
  result.fill(Qt::red);

  const auto layer = pinsel::inpaintLayer(*input, result, QStringLiteral("Inpaint"));
  ASSERT_TRUE(layer);
  ASSERT_TRUE(store.addLayer(*layer));
  EXPECT_EQ(store.layerCount(), 2U);
  EXPECT_EQ(store.currentLayer(), 1U);
  EXPECT_EQ(store.layerName(1), QStringLiteral("Inpaint"));
  const QImage image = store.toImage();
  EXPECT_EQ(image.pixelColor(500, 320), QColor(Qt::red));
  EXPECT_EQ(image.pixelColor(470, 320), QColor(Qt::white));

  ASSERT_TRUE(store.undo());
  EXPECT_EQ(store.layerCount(), 1U);
  EXPECT_EQ(store.toImage().pixelColor(500, 320), QColor(Qt::white));
  EXPECT_TRUE(store.hasMask());
  ASSERT_TRUE(store.redo());
  EXPECT_EQ(store.toImage().pixelColor(500, 320), QColor(Qt::red));
}

TEST(InpaintLayer, ANullResultMakesNoLayer) {
  TileStore store = whiteStore(QSize(600, 600));
  maskRect(store, QRect(10, 10, 20, 20));
  const std::optional<InpaintInput> input = pinsel::inpaintInput(store);
  ASSERT_TRUE(input);
  EXPECT_FALSE(pinsel::inpaintLayer(*input, QImage(), QStringLiteral("Inpaint")));
}

}  // namespace
