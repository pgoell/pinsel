#include "tiles/openraster.hpp"

#include <QBuffer>
#include <QImageWriter>
#include <QPainter>
#include <QSaveFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <algorithm>
#include <array>
#include <optional>
#include <utility>
#include <vector>

#include "tiles/zip.hpp"

namespace pinsel {

namespace {

using BlendMode = TileStore::BlendMode;

constexpr QByteArrayView kMimeType("image/openraster");
constexpr QLatin1StringView kNamespace("https://github.com/pgoell/pinsel");
constexpr QLatin1StringView kNoMemory("Not enough memory for the image");
// Readers expect a thumbnail of at most this many pixels a side.
constexpr int kThumbnailSize = 256;

struct CompositeOp {
  BlendMode mode;
  QLatin1StringView name;
};
constexpr std::array kCompositeOps{
    CompositeOp{.mode = BlendMode::Normal, .name = QLatin1StringView("svg:src-over")},
    CompositeOp{.mode = BlendMode::Multiply, .name = QLatin1StringView("svg:multiply")},
    CompositeOp{.mode = BlendMode::Screen, .name = QLatin1StringView("svg:screen")},
    CompositeOp{.mode = BlendMode::Overlay, .name = QLatin1StringView("svg:overlay")},
};

QLatin1StringView compositeOpName(BlendMode mode) {
  const auto* const found = std::ranges::find(kCompositeOps, mode, &CompositeOp::mode);
  return found->name;
}

// An operation pinsel has no mode for, such as svg:darken, draws as Normal.
BlendMode blendModeOf(QStringView name) {
  const auto* const found = std::ranges::find_if(
      kCompositeOps, [name](const CompositeOp& op) { return op.name == name; });
  return found == kCompositeOps.end() ? BlendMode::Normal : found->mode;
}

// A zip in a file that only replaces the old file once everything is in.
class Archive {
 public:
  explicit Archive(const QString& path) : file_(path), zip_(file_) {
    if (!file_.open(QIODevice::WriteOnly)) {
      error_ = file_.errorString();
    }
  }

  [[nodiscard]] const QString& error() const {
    return error_;
  }

  // Keeps the first error.
  bool failWith(const QString& error) {
    if (error_.isEmpty()) {
      error_ = error;
      file_.cancelWriting();
    }
    return false;
  }

  bool add(const QString& name, const QByteArray& data, bool deflate) {
    if (error_.isEmpty() && !zip_.add(name, data, deflate)) {
      fail();
    }
    return error_.isEmpty();
  }

  // PNG is compressed already, so the zip stores it as it is.
  bool addPng(const QString& name, const QImage& image) {
    if (!error_.isEmpty()) {
      return false;
    }
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, "png");
    if (!writer.write(image)) {
      return failWith(writer.errorString());
    }
    return add(name, png, false);
  }

  bool commit() {
    if (error_.isEmpty() && !zip_.finish()) {
      fail();
    }
    if (error_.isEmpty() && !file_.commit()) {
      error_ = file_.errorString();
    }
    return error_.isEmpty();
  }

 private:
  void fail() {
    failWith(file_.error() != QFileDevice::NoError
                 ? file_.errorString()
                 : QStringLiteral("The file would pass 4 GiB, which needs zip64"));
  }

  QSaveFile file_;
  ZipWriter zip_;
  QString error_;
};

// The mask in the red that the canvas tints it with, so it reads the same in
// another editor.
QImage maskLayerImage(const QImage& mask) {
  QImage image(mask.size(), QImage::Format_ARGB32_Premultiplied);
  if (image.isNull()) {
    return image;
  }
  image.fill(Qt::red);
  QPainter painter(&image);
  painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
  painter.drawImage(0, 0, mask);
  return image;
}

void writeLayer(QXmlStreamWriter& xml, const TileStore::LayerContent& content, const QString& src) {
  xml.writeEmptyElement(QStringLiteral("layer"));
  xml.writeAttribute(QStringLiteral("name"), content.name);
  xml.writeAttribute(QStringLiteral("src"), src);
  xml.writeAttribute(QStringLiteral("x"), QString::number(content.offset.x()));
  xml.writeAttribute(QStringLiteral("y"), QString::number(content.offset.y()));
  xml.writeAttribute(QStringLiteral("opacity"), QString::number(content.opacity));
  xml.writeAttribute(QStringLiteral("visibility"),
                     content.visible ? QStringLiteral("visible") : QStringLiteral("hidden"));
  xml.writeAttribute(QStringLiteral("composite-op"), compositeOpName(content.blend));
}

bool addMask(const TileStore& tiles, Archive& archive, QXmlStreamWriter& xml) {
  const QString src = QStringLiteral("data/mask.png");
  const QImage mask = maskLayerImage(tiles.toImage(TileStore::Plane::Mask));
  if (mask.isNull()) {
    return archive.failWith(kNoMemory);
  }
  TileStore::LayerContent content;
  content.name = QStringLiteral("Mask");
  content.visible = false;
  writeLayer(xml, content, src);
  xml.writeAttribute(kNamespace, QStringLiteral("mask"), QStringLiteral("true"));
  return archive.addPng(src, mask);
}

bool addLayer(const TileStore& tiles, std::size_t layer, Archive& archive, QXmlStreamWriter& xml) {
  std::optional<TileStore::LayerContent> content = tiles.layerContent(layer);
  if (!content) {
    return archive.failWith(kNoMemory);
  }
  // Every layer needs an image, so an empty one gets a clear pixel.
  if (content->image.isNull()) {
    content->image = QImage(1, 1, TileStore::kFormat);
    content->image.fill(Qt::transparent);
  }
  const QString src = QStringLiteral("data/layer%1.png").arg(layer);
  writeLayer(xml, *content, src);
  if (layer == tiles.currentLayer()) {
    xml.writeAttribute(QStringLiteral("selected"), QStringLiteral("true"));
  }
  return archive.addPng(src, content->image);
}

bool addMerged(const TileStore& tiles, Archive& archive) {
  const QImage merged = tiles.toImage();
  if (merged.isNull()) {
    return archive.failWith(kNoMemory);
  }
  QImage thumbnail = merged;
  if (merged.width() > kThumbnailSize || merged.height() > kThumbnailSize) {
    thumbnail = merged.scaled(kThumbnailSize, kThumbnailSize, Qt::KeepAspectRatio,
                              Qt::SmoothTransformation);
  }
  return archive.addPng(QStringLiteral("mergedimage.png"), merged) &&
         archive.addPng(QStringLiteral("Thumbnails/thumbnail.png"), thumbnail);
}

}  // namespace

QString writeOpenRaster(const TileStore& tiles, const QString& path) {
  Archive archive(path);
  // The spec wants mimetype first and stored, so a reader can tell the type
  // from the first bytes.
  archive.add(QStringLiteral("mimetype"), kMimeType.toByteArray(), false);
  QByteArray stack;
  QXmlStreamWriter xml(&stack);
  xml.setAutoFormatting(true);
  xml.writeStartDocument();
  xml.writeNamespace(kNamespace, QStringLiteral("pinsel"));
  xml.writeStartElement(QStringLiteral("image"));
  xml.writeAttribute(QStringLiteral("version"), QStringLiteral("0.0.6"));
  xml.writeAttribute(QStringLiteral("w"), QString::number(tiles.size().width()));
  xml.writeAttribute(QStringLiteral("h"), QString::number(tiles.size().height()));
  xml.writeStartElement(QStringLiteral("stack"));
  if (tiles.hasMask()) {
    addMask(tiles, archive, xml);
  }
  for (std::size_t layer = tiles.layerCount(); layer-- > 0;) {
    addLayer(tiles, layer, archive, xml);
  }
  xml.writeEndDocument();
  addMerged(tiles, archive);
  archive.add(QStringLiteral("stack.xml"), stack, true);
  archive.commit();
  return archive.error();
}

namespace {

struct StackLayer {
  TileStore::LayerContent content;
  QString src;
  bool mask = false;
  bool selected = false;
};

// What a layer takes from the stacks around it.
struct Group {
  bool visible = true;
  double opacity = 1.0;
  QPoint offset;
};

Group groupOf(const QXmlStreamAttributes& attributes, const Group& outer) {
  bool has_opacity = false;
  const double opacity = attributes.value(QStringLiteral("opacity")).toDouble(&has_opacity);
  return {
      .visible = outer.visible && attributes.value(QStringLiteral("visibility")) != u"hidden",
      .opacity = outer.opacity * (has_opacity ? std::clamp(opacity, 0.0, 1.0) : 1.0),
      .offset = outer.offset + QPoint(attributes.value(QStringLiteral("x")).toInt(),
                                      attributes.value(QStringLiteral("y")).toInt()),
  };
}

// Groups melt into the layers they hold: a group's opacity multiplies theirs,
// and a hidden group hides them. That matches the spec for a group that is not
// isolated; an isolated one would first merge its layers by their own modes.
// Appends the layers top first, as stack.xml lists them.
void readStack(QXmlStreamReader& xml, const Group& outer, std::vector<StackLayer>& layers) {
  while (xml.readNextStartElement()) {
    const QXmlStreamAttributes attributes = xml.attributes();
    const Group group = groupOf(attributes, outer);
    if (xml.name() == u"stack") {
      readStack(xml, group, layers);
      continue;
    }
    if (xml.name() == u"layer") {
      layers.push_back({
          .content =
              {
                  .name = attributes.value(QStringLiteral("name")).toString(),
                  .visible = group.visible,
                  .opacity = group.opacity,
                  .blend = blendModeOf(attributes.value(QStringLiteral("composite-op"))),
                  .image = {},
                  .offset = group.offset,
              },
          .src = attributes.value(QStringLiteral("src")).toString(),
          .mask = attributes.value(kNamespace, QStringLiteral("mask")) == u"true",
          .selected = attributes.value(QStringLiteral("selected")) == u"true",
      });
    }
    xml.skipCurrentElement();
  }
}

struct Stack {
  QSize size;
  // Bottom first, as the store counts them.
  std::vector<StackLayer> layers;
  QString error;
};

Stack readStackXml(const QByteArray& data) {
  QXmlStreamReader xml(data);
  Stack stack;
  if (!xml.readNextStartElement() || xml.name() != u"image") {
    stack.error = QStringLiteral("stack.xml has no image");
    return stack;
  }
  stack.size = QSize(xml.attributes().value(QStringLiteral("w")).toInt(),
                     xml.attributes().value(QStringLiteral("h")).toInt());
  if (xml.readNextStartElement() && xml.name() == u"stack") {
    const Group image;
    readStack(xml, image, stack.layers);
  }
  if (xml.hasError()) {
    stack.error = QStringLiteral("stack.xml is broken: %1").arg(xml.errorString());
  } else if (stack.size.isEmpty()) {
    stack.error = QStringLiteral("stack.xml gives the image no size");
  }
  std::ranges::reverse(stack.layers);
  return stack;
}

ReadImage failed(const QString& error) {
  return {.tiles = {}, .error = error};
}

// Reads the layer PNGs and remembers the first that fails.
class LayerFiles {
 public:
  explicit LayerFiles(const ZipReader& zip) : zip_(&zip) {}

  QImage read(const QString& src) {
    const std::optional<QByteArray> data = zip_->read(src);
    QImage image = data ? QImage::fromData(*data) : QImage();
    if (image.isNull() && error_.isEmpty()) {
      error_ = QStringLiteral("Could not read %1").arg(src);
    }
    return image;
  }

  // Without a failed read, what failed was memory.
  [[nodiscard]] QString error() const {
    return error_.isEmpty() ? QString(kNoMemory) : error_;
  }

 private:
  const ZipReader* zip_;
  QString error_;
};

// Takes the mask layer out of the stack and returns it as a mask over the
// whole image: null when there is none, and nothing when it cannot be read.
std::optional<QImage> takeMask(Stack& stack, LayerFiles& files) {
  const auto found = std::ranges::find(stack.layers, true, &StackLayer::mask);
  if (found == stack.layers.end()) {
    return QImage();
  }
  const QImage image = files.read(found->src);
  QImage mask(stack.size, TileStore::kMaskFormat);
  if (image.isNull() || mask.isNull()) {
    return std::nullopt;
  }
  mask.fill(Qt::transparent);
  QPainter painter(&mask);
  painter.setCompositionMode(QPainter::CompositionMode_Source);
  painter.drawImage(found->content.offset, image);
  painter.end();
  stack.layers.erase(found);
  return mask;
}

// The layer marked as selected, or else the top one.
std::size_t selectedLayer(const std::vector<StackLayer>& layers) {
  const auto found = std::ranges::find(layers, true, &StackLayer::selected);
  return found == layers.end() ? layers.size() - 1
                               : static_cast<std::size_t>(found - layers.begin());
}

}  // namespace

ReadImage readOpenRaster(const QString& path) {
  const ZipReader zip(path);
  if (!zip.error().isEmpty()) {
    return failed(zip.error());
  }
  if (zip.firstName() != u"mimetype" ||
      zip.read(QStringLiteral("mimetype")).value_or(QByteArray()) != kMimeType) {
    return failed(QStringLiteral("Not an OpenRaster file"));
  }
  const std::optional<QByteArray> xml = zip.read(QStringLiteral("stack.xml"));
  if (!xml) {
    return failed(QStringLiteral("The file has no stack.xml"));
  }
  Stack stack = readStackXml(*xml);
  if (!stack.error.isEmpty()) {
    return failed(stack.error);
  }
  LayerFiles files(zip);
  const std::optional<QImage> mask = takeMask(stack, files);
  if (!mask) {
    return failed(files.error());
  }
  if (stack.layers.empty()) {
    return failed(QStringLiteral("The file has no layers"));
  }
  TileStore tiles(
      stack.size, stack.layers.size(),
      [&](std::size_t layer) -> std::optional<TileStore::LayerContent> {
        StackLayer& source = stack.layers.at(layer);
        source.content.image = files.read(source.src);
        if (source.content.image.isNull()) {
          return std::nullopt;
        }
        return std::exchange(source.content, {});
      },
      *mask, selectedLayer(stack.layers));
  if (tiles.size().isEmpty()) {
    return failed(files.error());
  }
  return {.tiles = std::move(tiles), .error = {}};
}

}  // namespace pinsel
