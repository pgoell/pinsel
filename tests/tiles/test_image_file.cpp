#include <gtest/gtest.h>

#include <QBuffer>
#include <QByteArray>
#include <QColor>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QRect>
#include <QString>
#include <QTemporaryDir>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <tuple>
#include <vector>

#include "tiles/tile_store.hpp"
#include "tiles/zip.hpp"

namespace {

using pinsel::TileStore;
using BlendMode = TileStore::BlendMode;

constexpr int kTile = TileStore::kTileSize;
constexpr std::uint32_t kAnySize = std::numeric_limits<std::uint32_t>::max();

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

void fill(TileStore& store, QRect area, QColor color,
          TileStore::Plane plane = TileStore::Plane::Image) {
  store.paint(
      area, [&](QPainter& painter, std::size_t /*tile*/) { painter.fillRect(area, color); }, plane);
}

QByteArray png(const QImage& image) {
  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  image.save(&buffer, "png");
  return bytes;
}

TileStore::LayerContent content(const QString& name, QImage image, QPoint offset = {}) {
  TileStore::LayerContent layer;
  layer.name = name;
  layer.image = std::move(image);
  layer.offset = offset;
  return layer;
}

class ImageFile : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(dir_.isValid());
  }
  [[nodiscard]] QString path(const QString& name) const {
    return dir_.filePath(name);
  }

 private:
  QTemporaryDir dir_;
};

// Three layers, each set apart from the defaults, and a mask.
TileStore layeredStore() {
  TileStore store = cleanStore();
  fill(store, QRect(kTile - 20, 30, 40, kTile), Qt::red);
  store.addLayer();
  fill(store, QRect(10, 10, 100, 100), QColor(0, 0, 255, 128));
  store.setLayerOpacity(1, 0.25);
  store.setLayerBlendMode(1, BlendMode::Multiply);
  store.addLayer();
  fill(store, QRect((2 * kTile) + 10, kTile + 10, 20, 20), Qt::green);
  store.setLayerVisible(2, false);
  store.setLayerBlendMode(2, BlendMode::Overlay);
  fill(store, QRect(kTile, 0, 30, 30), QColor(0, 0, 0, 200), TileStore::Plane::Mask);
  store.setCurrentLayer(1);
  store.commit();
  return store;
}

TEST_F(ImageFile, APaintedImageComesBackFromAPngUnchanged) {
  TileStore store = cleanStore();
  fill(store, QRect(kTile - 20, 30, 40, kTile), Qt::red);
  fill(store, QRect(0, 0, 5, 5), Qt::transparent);
  store.commit();

  ASSERT_EQ(pinsel::writeImage(store, path(QStringLiteral("painted.png"))), QString());
  const pinsel::ReadImage read = pinsel::readImage(path(QStringLiteral("painted.png")));

  ASSERT_EQ(read.error, QString());
  EXPECT_EQ(read.tiles.toImage(), store.toImage());
}

// Empty when there is no memory for the content.
TileStore::LayerContent contentOf(const TileStore& store, std::size_t layer) {
  const std::optional<TileStore::LayerContent> content = store.layerContent(layer);
  return content ? *content : TileStore::LayerContent{};
}

auto attributesOf(const TileStore& store, std::size_t layer) {
  return std::tuple(store.layerName(layer), store.layerVisible(layer), store.layerOpacity(layer),
                    store.layerBlendMode(layer));
}

void expectSameLayer(const TileStore& got, const TileStore& expected, std::size_t layer) {
  SCOPED_TRACE(layer);
  EXPECT_EQ(attributesOf(got, layer), attributesOf(expected, layer));
  EXPECT_EQ(contentOf(got, layer).image, contentOf(expected, layer).image);
  EXPECT_EQ(contentOf(got, layer).offset, contentOf(expected, layer).offset);
}

TEST_F(ImageFile, OpenRasterKeepsEveryLayer) {
  const TileStore store = layeredStore();

  ASSERT_EQ(pinsel::writeImage(store, path(QStringLiteral("layers.ora"))), QString());
  const pinsel::ReadImage read = pinsel::readImage(path(QStringLiteral("layers.ora")));

  ASSERT_EQ(read.error, QString());
  ASSERT_EQ(read.tiles.layerCount(), 3);
  expectSameLayer(read.tiles, store, 0);
  expectSameLayer(read.tiles, store, 1);
  expectSameLayer(read.tiles, store, 2);
}

TEST_F(ImageFile, OpenRasterKeepsTheMaskAndTheCurrentLayerAndStartsANewHistory) {
  const TileStore store = layeredStore();

  ASSERT_EQ(pinsel::writeImage(store, path(QStringLiteral("layers.ora"))), QString());
  const pinsel::ReadImage read = pinsel::readImage(path(QStringLiteral("layers.ora")));

  ASSERT_EQ(read.error, QString());
  EXPECT_EQ(read.tiles.currentLayer(), 1);
  EXPECT_EQ(read.tiles.toImage(TileStore::Plane::Mask), store.toImage(TileStore::Plane::Mask));
  EXPECT_EQ(read.tiles.toImage(), store.toImage());
  EXPECT_FALSE(read.tiles.canUndo());
}

TEST_F(ImageFile, OpenRasterStoresTheMimetypeFirstAndUncompressed) {
  ASSERT_EQ(pinsel::writeImage(cleanStore(), path(QStringLiteral("plain.ora"))), QString());
  QFile file(path(QStringLiteral("plain.ora")));
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  const QByteArray start = file.read(54);

  EXPECT_TRUE(start.startsWith("PK\x03\x04"));
  EXPECT_EQ(start.mid(30), QByteArray("mimetypeimage/openraster"));
  const pinsel::ZipReader zip(path(QStringLiteral("plain.ora")));
  EXPECT_TRUE(zip.read(QStringLiteral("mergedimage.png"), kAnySize).has_value());
  const QImage thumbnail = QImage::fromData(
      zip.read(QStringLiteral("Thumbnails/thumbnail.png"), kAnySize).value_or(QByteArray()));
  EXPECT_EQ(thumbnail.width(), 256);
  EXPECT_FALSE(zip.read(QStringLiteral("data/mask.png"), kAnySize).has_value());
}

// Another editor's file: deflated entries, groups, layer offsets, a mode
// pinsel lacks, and values out of range.
QString writeOtherEditorsFile(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return file.errorString();
  }
  pinsel::ZipWriter zip(file);
  const bool written =
      zip.add(QStringLiteral("mimetype"), "image/openraster", false) &&
      zip.add(QStringLiteral("stack.xml"), R"(<?xml version="1.0"?>
<image version="0.0.3" w="600" h="400">
  <stack>
    <stack x="2147483647">
      <layer name="Odd" src="data/base.png" opacity="nan" visibility="hidden" x="1"/>
    </stack>
    <stack name="Group" opacity="0.5" visibility="hidden" x="100" y="0">
      <layer name="Inner" src="data/inner.png" x="10" y="20" composite-op="svg:darken"/>
    </stack>
    <layer name="Base" src="data/base.png" composite-op="svg:screen" opacity="0.75"/>
  </stack>
</image>)",
              true) &&
      zip.add(QStringLiteral("data/inner.png"), png(filled(QSize(700, 50), Qt::blue)), true) &&
      zip.add(QStringLiteral("data/base.png"), png(filled(QSize(600, 400), Qt::white)), true) &&
      zip.finish();
  return written ? QString() : QStringLiteral("Could not write the zip");
}

TEST_F(ImageFile, OpenRasterGroupsMeltIntoTheirLayers) {
  ASSERT_EQ(writeOtherEditorsFile(path(QStringLiteral("other.ora"))), QString());

  const pinsel::ReadImage read = pinsel::readImage(path(QStringLiteral("other.ora")));

  ASSERT_EQ(read.tiles.layerCount(), 3);
  EXPECT_EQ(attributesOf(read.tiles, 0),
            std::tuple(QStringLiteral("Base"), true, 0.75, BlendMode::Screen));
  EXPECT_EQ(attributesOf(read.tiles, 1),
            std::tuple(QStringLiteral("Inner"), false, 0.5, BlendMode::Normal));
  EXPECT_EQ(attributesOf(read.tiles, 2),
            std::tuple(QStringLiteral("Odd"), false, 1.0, BlendMode::Normal));
  EXPECT_EQ(read.tiles.currentLayer(), 2);
}

TEST_F(ImageFile, OpenRasterLayersSitAtTheirOffsetsCutToTheImage) {
  ASSERT_EQ(writeOtherEditorsFile(path(QStringLiteral("other.ora"))), QString());

  const pinsel::ReadImage read = pinsel::readImage(path(QStringLiteral("other.ora")));

  ASSERT_EQ(read.tiles.layerCount(), 3);
  const TileStore::LayerContent inner = contentOf(read.tiles, 1);
  const QImage cut = inner.image.copy(QRect(-inner.offset, QSize(600, 400)));
  EXPECT_EQ(cut.pixelColor(109, 20), QColor(Qt::transparent));
  EXPECT_EQ(cut.pixelColor(110, 20), QColor(Qt::blue));
  EXPECT_EQ(cut.pixelColor(599, 69), QColor(Qt::blue));
  EXPECT_EQ(cut.pixelColor(110, 70), QColor(Qt::transparent));
}

// Changes a byte in the first PNG's pixel data, so the zip's checksum fails.
bool breakFirstPng(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadWrite)) {
    return false;
  }
  QByteArray bytes = file.readAll();
  const auto at = bytes.indexOf("IDAT") + 8;
  const char changed = bytes.at(at) == 'x' ? 'y' : 'x';
  bytes.replace(at, 1, &changed, 1);
  return file.seek(0) && file.write(bytes) == bytes.size();
}

TEST_F(ImageFile, OpenRasterWithABrokenLayerSaysWhich) {
  ASSERT_EQ(pinsel::writeImage(cleanStore(), path(QStringLiteral("plain.ora"))), QString());
  ASSERT_TRUE(breakFirstPng(path(QStringLiteral("plain.ora"))));

  const pinsel::ReadImage read = pinsel::readImage(path(QStringLiteral("plain.ora")));

  EXPECT_EQ(read.error, QStringLiteral("Could not read data/layer0.png"));
  EXPECT_TRUE(read.tiles.size().isEmpty());
}

TEST_F(ImageFile, OpenRasterRefusesASizePastThePixelBudget) {
  {
    QFile file(path(QStringLiteral("huge.ora")));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    pinsel::ZipWriter zip(file);
    ASSERT_TRUE(zip.add(QStringLiteral("mimetype"), "image/openraster", false));
    ASSERT_TRUE(zip.add(QStringLiteral("stack.xml"),
                        R"(<image w="2147483647" h="2147483647"><stack>
<layer src="data/layer0.png"/></stack></image>)",
                        true));
    ASSERT_TRUE(
        zip.add(QStringLiteral("data/layer0.png"), png(filled(QSize(1, 1), Qt::red)), false));
    ASSERT_TRUE(zip.finish());
  }

  const pinsel::ReadImage read = pinsel::readImage(path(QStringLiteral("huge.ora")));

  EXPECT_EQ(read.error,
            QStringLiteral("The image is 2147483647 x 2147483647 pixels, more than pinsel opens"));
  EXPECT_TRUE(read.tiles.size().isEmpty());
}

TEST_F(ImageFile, OpenRasterReadsGroupsNestedDeeperThanAThreadStack) {
  constexpr int kDepth = 200000;
  QByteArray stack = R"(<image w="1" h="1"><stack>)";
  stack += QByteArray("<stack>").repeated(kDepth);
  stack += R"(<layer name="Deep" src="data/layer0.png"/>)";
  stack += QByteArray("</stack>").repeated(kDepth + 1);
  stack += "</image>";
  {
    QFile file(path(QStringLiteral("deep.ora")));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    pinsel::ZipWriter zip(file);
    ASSERT_TRUE(zip.add(QStringLiteral("mimetype"), "image/openraster", false));
    ASSERT_TRUE(zip.add(QStringLiteral("stack.xml"), stack, true));
    ASSERT_TRUE(
        zip.add(QStringLiteral("data/layer0.png"), png(filled(QSize(1, 1), Qt::red)), false));
    ASSERT_TRUE(zip.finish());
  }

  const pinsel::ReadImage read = pinsel::readImage(path(QStringLiteral("deep.ora")));

  ASSERT_EQ(read.error, QString());
  EXPECT_EQ(read.tiles.layerName(0), QStringLiteral("Deep"));
}

TEST_F(ImageFile, APngNamedOraIsNoOpenRasterFile) {
  ASSERT_EQ(pinsel::writeImage(cleanStore(), path(QStringLiteral("flat.png"))), QString());
  ASSERT_TRUE(QFile::rename(path(QStringLiteral("flat.png")), path(QStringLiteral("flat.ora"))));

  const pinsel::ReadImage read = pinsel::readImage(path(QStringLiteral("flat.ora")));

  EXPECT_NE(read.error, QString());
  EXPECT_TRUE(read.tiles.size().isEmpty());
}

TEST_F(ImageFile, WritingATypePinselCannotWriteSaysWhy) {
  EXPECT_NE(pinsel::writeImage(cleanStore(), path(QStringLiteral("photo.jpg"))), QString());
  EXPECT_FALSE(QFile::exists(path(QStringLiteral("photo.jpg"))));
}

TEST_F(ImageFile, WritingToAMissingFolderSaysWhy) {
  EXPECT_NE(pinsel::writeImage(cleanStore(), path(QStringLiteral("missing/out.png"))), QString());
  EXPECT_NE(pinsel::writeImage(cleanStore(), path(QStringLiteral("missing/out.ora"))), QString());
}

TEST_F(ImageFile, AZipEntryPastItsCapIsNotRead) {
  {
    QFile file(path(QStringLiteral("big.zip")));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    pinsel::ZipWriter zip(file);
    ASSERT_TRUE(zip.add(QStringLiteral("zeros"), QByteArray(1000, '\0'), true));
    ASSERT_TRUE(zip.finish());
  }
  const pinsel::ZipReader zip(path(QStringLiteral("big.zip")));

  EXPECT_FALSE(zip.read(QStringLiteral("zeros"), 999).has_value());
  EXPECT_EQ(zip.read(QStringLiteral("zeros"), 1000).value_or(QByteArray()), QByteArray(1000, '\0'));
}

TEST(ImageFileNames, OnlyPngAndOpenRasterAreWritable) {
  EXPECT_EQ(pinsel::imageFormatOf(QStringLiteral("a.PNG")).value_or(pinsel::ImageFormat{}).suffix,
            QLatin1StringView("png"));
  EXPECT_TRUE(
      pinsel::imageFormatOf(QStringLiteral("a.ora")).value_or(pinsel::ImageFormat{}).layered);
  EXPECT_FALSE(pinsel::imageFormatOf(QStringLiteral("a.jpg")).has_value());
  EXPECT_FALSE(pinsel::imageFormatOf(QStringLiteral("ora")).has_value());
}

TEST(ImageFileNames, ReadingAMissingFileSaysWhyAndLeavesTheStoreEmpty) {
  for (const QString& name : {QStringLiteral("no/such/image.png"), QStringLiteral("no/such.ora")}) {
    const pinsel::ReadImage read = pinsel::readImage(name);
    EXPECT_NE(read.error, QString());
    EXPECT_TRUE(read.tiles.size().isEmpty());
  }
}

// A red square that hangs over the top edge of the second tile, above an
// empty layer.
TileStore offsetStore() {
  const std::vector<TileStore::LayerContent> layers{
      content(QStringLiteral("Bottom"), filled(QSize(10, 10), Qt::red), QPoint(kTile + 5, -5)),
      content(QStringLiteral("Empty"), QImage()),
  };
  return {QSize(2 * kTile, kTile), layers.size(),
          [&](std::size_t layer) { return std::optional(layers.at(layer)); }, QImage(), 5};
}

TEST(TileStoreContent, LayersLoadAtTheirOffsetCutToTheImage) {
  const TileStore store = offsetStore();
  ASSERT_EQ(store.layerCount(), 2);
  EXPECT_EQ(store.layerTile(0, 1).pixelColor(5, 4), QColor(Qt::red));
  EXPECT_EQ(store.layerTile(0, 1).pixelColor(5, 5), QColor(Qt::transparent));
  EXPECT_EQ(contentOf(store, 0).offset, QPoint(kTile, 0));
}

TEST(TileStoreContent, TilesNoLayerCoversStayNull) {
  const TileStore store = offsetStore();
  ASSERT_EQ(store.layerCount(), 2);
  EXPECT_TRUE(store.layerTile(0, 0).isNull());
  EXPECT_TRUE(store.layerTile(1, 0).isNull());
  EXPECT_TRUE(contentOf(store, 1).image.isNull());
  EXPECT_FALSE(store.hasMask());
}

TEST(TileStoreContent, ACurrentLayerPastTheTopBecomesTheTop) {
  EXPECT_EQ(offsetStore().currentLayer(), 1);
}

TEST(TileStoreContent, AFailedLoadLeavesTheStoreEmpty) {
  const TileStore store(
      QSize(10, 10), 2,
      [](std::size_t layer) -> std::optional<TileStore::LayerContent> {
        if (layer == 0) {
          return content(QStringLiteral("Fine"), filled(QSize(10, 10), Qt::red));
        }
        return std::nullopt;
      },
      QImage(), 0);
  EXPECT_TRUE(store.size().isEmpty());
  EXPECT_EQ(store.layerCount(), 0);
}

}  // namespace
