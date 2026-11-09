#include <QImageReader>
#include <QImageWriter>
#include <QSaveFile>
#include <optional>
#include <utility>

#include "tiles/openraster.hpp"
#include "tiles/tile_store.hpp"

namespace pinsel {

namespace {

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
  const std::optional<ImageFormat> format = imageFormatOf(path);
  if (format && format->layered) {
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

std::optional<ImageFormat> imageFormatOf(const QString& path) {
  for (const ImageFormat& format : kImageFormats) {
    if (path.endsWith(u'.' + format.suffix, Qt::CaseInsensitive)) {
      return format;
    }
  }
  return std::nullopt;
}

QString writeImage(const TileStore& tiles, const QString& path) {
  const std::optional<ImageFormat> format = imageFormatOf(path);
  if (!format) {
    return QStringLiteral("pinsel cannot write this type of file");
  }
  if (format->layered) {
    return writeOpenRaster(tiles, path);
  }
  const QImage image = tiles.toImage();
  if (image.isNull()) {
    return QStringLiteral("Not enough memory to merge the layers");
  }
  return writePng(image, path);
}

}  // namespace pinsel
