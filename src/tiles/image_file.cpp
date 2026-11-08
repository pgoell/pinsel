#include <QImageReader>
#include <QImageWriter>
#include <QSaveFile>
#include <utility>

#include "tiles/openraster.hpp"
#include "tiles/tile_store.hpp"

namespace pinsel {

namespace {

bool isOpenRaster(const QString& path) {
  return path.endsWith(QStringLiteral(".ora"), Qt::CaseInsensitive);
}

bool isPng(const QString& path) {
  return path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive);
}

QString writePng(const QImage& image, const QString& path) {
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return file.errorString();
  }
  QImageWriter writer(&file, "png");
  if (!writer.write(image)) {
    file.cancelWriting();
    return writer.errorString();
  }
  if (!file.commit()) {
    return file.errorString();
  }
  return {};
}

}  // namespace

ReadImage readImage(const QString& path) {
  if (isOpenRaster(path)) {
    return readOpenRaster(path);
  }
  QImageReader reader(path);
  reader.setAutoTransform(true);
  const QImage image = reader.read();
  if (image.isNull()) {
    return {.tiles = {}, .error = reader.errorString()};
  }
  TileStore tiles(image);
  if (tiles.size().isEmpty()) {
    return {.tiles = {}, .error = QStringLiteral("Not enough memory to convert the image")};
  }
  return {.tiles = std::move(tiles), .error = {}};
}

bool canWrite(const QString& path) {
  return isPng(path) || isOpenRaster(path);
}

QString writeImage(const TileStore& tiles, const QString& path) {
  if (isOpenRaster(path)) {
    return writeOpenRaster(tiles, path);
  }
  const QImage image = tiles.toImage();
  if (image.isNull()) {
    return QStringLiteral("Not enough memory to merge the layers");
  }
  return writePng(image, path);
}

}  // namespace pinsel
