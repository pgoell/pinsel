#pragma once

#include <QImage>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

class QPainter;

namespace pinsel {

// The image as a stack of layers over one grid of tiles, each tile a QImage of
// its own, with undo and redo. Tiles count from the top left, row by row, and
// layers from the bottom. An edit marks the tiles it touches as dirty until
// takeDirty collects them, so the canvas uploads only those.
//
// Above the stack sits the mask, which marks the region inpainting may change.
// Layer tiles, mask tiles and changes to the stack share one undo history. A
// tile of a new layer, and a mask tile, stays null, and costs nothing, until
// something paints on it.
//
// Undo keeps whole tiles from before each change, and a change to the stack
// keeps the whole stack from before it. QImage shares its pixels between copies
// until one of them is written, so keeping a tile costs nothing until the edit
// detaches it.
class TileStore {
 public:
  static constexpr int kTileSize = 512;
  // RGBA8 textures take bytes in this order on every GPU backend.
  static constexpr QImage::Format kFormat = QImage::Format_RGBA8888_Premultiplied;
  // One byte per pixel: 0 leaves the pixel alone, 255 masks it fully.
  static constexpr QImage::Format kMaskFormat = QImage::Format_Alpha8;
  static constexpr std::size_t kDefaultUndoBudget = std::size_t{1} << 30U;

  // Image is the current layer.
  enum class Plane : std::uint8_t { Image, Mask };
  // How a layer mixes with the layers below it: the separable modes of the W3C
  // Compositing and Blending spec, as QPainter's composition modes of the same
  // names compute them. https://www.w3.org/TR/compositing-1/#blending
  enum class BlendMode : std::uint8_t { Normal, Multiply, Screen, Overlay };

  // A layer as a file holds it: its pixels, which may be null, and where in
  // the image they sit.
  struct LayerContent {
    QString name;
    bool visible = true;
    double opacity = 1.0;
    BlendMode blend = BlendMode::Normal;
    QImage image;
    QPoint offset;
  };
  // Loads the layer at an index, counted from the bottom. Null stops the load.
  using LoadLayer = std::function<std::optional<LayerContent>(std::size_t layer)>;

  TileStore() = default;
  // One layer, converted to kFormat. Every tile starts dirty. The store stays
  // empty when there is no memory for the tiles. At its peak the store needs
  // memory for the tiles and one more tile on top of image.
  explicit TileStore(const QImage& image);
  // A stack of count layers over an image of size, with the alpha of mask as
  // the mask. Loads one layer at a time, so at its peak the store needs memory
  // for its tiles and one loaded layer. What falls outside size is cut, and
  // tiles that no layer image covers stay null. Every tile starts dirty. The
  // store stays empty when count or size is zero, a load fails or there is no
  // memory.
  TileStore(QSize size, std::size_t count, const LoadLayer& load, const QImage& mask,
            std::size_t current);

  [[nodiscard]] QSize size() const {
    return size_;
  }
  [[nodiscard]] std::size_t tileCount() const {
    return tile_count_;
  }
  [[nodiscard]] QRect tileRect(std::size_t index) const;
  // Null for a tile that nothing has painted.
  [[nodiscard]] const QImage& tile(std::size_t index, Plane plane = Plane::Image) const;
  // Image is the visible layers, one over the other.
  [[nodiscard]] QImage toImage(Plane plane = Plane::Image) const;

  [[nodiscard]] std::size_t layerCount() const {
    return layers_.size();
  }
  [[nodiscard]] std::size_t currentLayer() const {
    return current_;
  }
  // Paint and Plane::Image go to the current layer. Choosing one is no edit.
  void setCurrentLayer(std::size_t layer);
  // Stays with the layer for its whole life, through moves, undo and redo.
  [[nodiscard]] std::uint64_t layerId(std::size_t layer) const {
    return layers_.at(layer).id;
  }
  [[nodiscard]] const QString& layerName(std::size_t layer) const {
    return layers_.at(layer).name;
  }
  [[nodiscard]] bool layerVisible(std::size_t layer) const {
    return layers_.at(layer).visible;
  }
  // From 0, which hides the layer, to 1.
  [[nodiscard]] double layerOpacity(std::size_t layer) const {
    return layers_.at(layer).opacity;
  }
  [[nodiscard]] BlendMode layerBlendMode(std::size_t layer) const {
    return layers_.at(layer).blend;
  }
  // Normal at full opacity: the layer simply covers the ones below it.
  [[nodiscard]] bool layerPlain(std::size_t layer) const {
    return layers_.at(layer).blend == BlendMode::Normal && layers_.at(layer).opacity >= 1.0;
  }
  [[nodiscard]] const QImage& layerTile(std::size_t layer, std::size_t index) const {
    return layers_.at(layer).tiles.at(index);
  }
  // The image covers the layer's painted tiles, and is null when there are
  // none. Null when there is no memory for the image.
  [[nodiscard]] std::optional<LayerContent> layerContent(std::size_t layer) const;
  // Whether anything has painted the mask, even if it was erased again.
  [[nodiscard]] bool hasMask() const;
  // Each change to the stack first commits the open step, then becomes an undo
  // step of its own. Undo of one brings back the current layer from before it,
  // and redo the one from after it.
  //
  // Puts an empty layer above the current one and makes it current.
  void addLayer();
  // Puts a layer holding content above the current one and makes it current.
  // What falls outside the image is cut. Returns false, and changes nothing,
  // when there is no memory.
  bool addLayer(const LayerContent& content);
  // Removes the current layer, unless it is the only one, and makes the one
  // below current. Returns false when nothing was removed.
  bool removeLayer();
  // Moves the layer at from to position to and makes it current.
  void moveLayer(std::size_t from, std::size_t to);
  void setLayerVisible(std::size_t layer, bool visible);
  // Clamps opacity to between 0 and 1. With join, the change joins the newest
  // step when that step only changed this layer's opacity, so dragging a slider
  // makes one step.
  void setLayerOpacity(std::size_t layer, double opacity, bool join = false);
  void setLayerBlendMode(std::size_t layer, BlendMode mode);

  // Calls draw once for each tile under area, with the tile's index and a
  // painter in image pixels that is clipped to area and to the tile. A null
  // tile becomes a clear one first. The change joins the open undo step.
  void paint(QRect area, const std::function<void(QPainter&, std::size_t tile)>& draw,
             Plane plane = Plane::Image);
  // Empties the mask as an undo step of its own.
  void clearMask();
  // Closes the open undo step, if anything changed since the last one, and
  // drops the redo steps. Then drops the oldest steps until the tiles that
  // undo keeps fit the budget, but always keeps the newest step.
  void commit();
  void setUndoBudget(std::size_t bytes);
  // The pixels of the tiles of every layer and the mask, and of the tiles that
  // undo keeps.
  [[nodiscard]] std::size_t bytes() const;
  [[nodiscard]] std::size_t undoBytes() const;
  // Each first commits the open step. They return false when there is no step.
  bool undo();
  bool redo();
  [[nodiscard]] bool canUndo() const;
  [[nodiscard]] bool canRedo() const;
  // Names the edits behind the current layers and mask: undo and redo return to
  // an earlier number, and each paint call, even one that joins the open step,
  // gives a number never seen before. A save stores it to tell later whether
  // anything changed.
  [[nodiscard]] std::uint64_t revision() const;

  // The plane's dirty tiles in ascending order. They are clean afterwards.
  [[nodiscard]] std::vector<std::size_t> takeDirty(Plane plane = Plane::Image);
  [[nodiscard]] std::vector<std::size_t> takeLayerDirty(std::size_t layer);

 private:
  static constexpr std::uint64_t kMaskId = 0;

  struct Layer {
    std::uint64_t id = kMaskId;
    QString name;
    bool visible = true;
    double opacity = 1.0;
    BlendMode blend = BlendMode::Normal;
    std::vector<QImage> tiles;
    std::vector<bool> dirty;
    // Which tiles the open step already saved.
    std::vector<bool> saved;
  };
  struct SavedTile {
    std::uint64_t layer;
    std::size_t index;
    QImage image;
  };
  struct Stack {
    std::vector<Layer> layers;
    std::size_t current = 0;
  };
  struct Step {
    // The revision of the step's latest paint.
    std::uint64_t revision = 0;
    std::vector<SavedTile> tiles;
    // The stack before a change to it, and the bytes of the layers it holds
    // that the stack after the change has lost.
    std::optional<Stack> stack;
    std::size_t stack_bytes = 0;
    // The layer whose opacity is all the step changed, or kMaskId.
    std::uint64_t opacity_of = kMaskId;
  };

  [[nodiscard]] Layer emptyLayer() const;
  // Copies image, placed at offset, into the tiles of layer. Returns false
  // when there is no memory.
  bool fillLayer(Layer& layer, const QImage& image, QPoint offset, QImage::Format format) const;
  [[nodiscard]] static std::size_t layerBytes(const Layer& layer);
  [[nodiscard]] std::size_t lostBytes(const std::vector<Layer>& layers) const;
  static std::vector<std::size_t> takeFlags(std::vector<bool>& flags);
  Layer& target(Plane plane);
  [[nodiscard]] const Layer& target(Plane plane) const;
  Layer* find(std::uint64_t id);
  void save(Layer& layer, std::size_t index);
  void restack(const std::function<void()>& change);
  void swapIn(Step& step);
  // Returns the bytes of the layers the stack it gives back has lost.
  std::size_t swapStack(Stack& stack);
  void pushStep(Step step);
  void trimUndo();

  QSize size_;
  std::size_t columns_ = 0;
  std::size_t tile_count_ = 0;
  std::vector<Layer> layers_;
  std::size_t current_ = 0;
  Layer mask_;
  std::uint64_t last_layer_id_ = kMaskId;
  Step open_;
  // The revision before the oldest step in undo_.
  std::uint64_t base_revision_ = 0;
  std::uint64_t last_revision_ = 0;
  std::vector<Step> undo_;
  std::vector<Step> redo_;
  std::size_t undo_budget_ = kDefaultUndoBudget;
};

struct ReadImage {
  TileStore tiles;
  QString error;
};

// Reads the image file at path, turned upright by its orientation tag, into a
// store. An OpenRaster file (.ora) keeps its layers and, when pinsel wrote it,
// its mask. On failure the store is empty and error says why. Slow for a
// large image, so callers run it off the UI thread.
ReadImage readImage(const QString& path);

// A type of file that writeImage writes.
struct ImageFormat {
  QLatin1StringView suffix;
  // A name filter for a file dialog.
  QLatin1StringView filter;
  // Keeps the layers and the mask, rather than merging the visible layers.
  // OpenRaster is the one such format.
  bool layered = false;
};
inline constexpr std::array kImageFormats{
    ImageFormat{
        .suffix = QLatin1StringView("png"),
        .filter = QLatin1StringView("PNG image (*.png)"),
        .layered = false,
    },
    ImageFormat{
        .suffix = QLatin1StringView("ora"),
        .filter = QLatin1StringView("OpenRaster image, with layers and mask (*.ora)"),
        .layered = true,
    },
};

// The format that the suffix of path names, if writeImage writes it.
std::optional<ImageFormat> imageFormatOf(const QString& path);

// Writes tiles at path, through a temporary file, so a failed write leaves the
// old file whole: an OpenRaster file (.ora) keeps the layers and the mask, and
// a PNG holds the visible layers merged. Returns an empty string, or why it
// failed. Slow, so callers run it off the UI thread on a copy of the store,
// which shares the tiles.
QString writeImage(const TileStore& tiles, const QString& path);

}  // namespace pinsel
