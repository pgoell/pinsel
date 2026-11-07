#include <QImageReader>
#include <QImageWriter>
#include <QSaveFile>
#include <utility>

#include "tiles/tile_store.hpp"

namespace pinsel {

ReadImage readImage(const QString& path) {
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

}  // namespace pinsel
