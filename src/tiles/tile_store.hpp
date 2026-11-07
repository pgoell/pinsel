#pragma once

#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

class QPainter;

namespace pinsel {

// The image as a grid of tiles, each a QImage of its own, with undo and redo.
// Tiles count from the top left, row by row. An edit marks the tiles it touches
// as dirty until takeDirty collects them, so the canvas uploads only those.
//
// Each tile has two planes: the image, and a mask that marks the region
// inpainting may change. Both planes share one undo history. A mask tile stays
// null, and costs nothing, until something paints on it.
//
// Undo keeps whole tiles from before each change. QImage shares its pixels
// between copies until one of them is written, so keeping a tile costs nothing
// until the edit detaches it.
class TileStore {
 public:
  static constexpr int kTileSize = 512;
  // RGBA8 textures take bytes in this order on every GPU backend.
  static constexpr QImage::Format kFormat = QImage::Format_RGBA8888_Premultiplied;
  // One byte per pixel: 0 leaves the pixel alone, 255 masks it fully.
  static constexpr QImage::Format kMaskFormat = QImage::Format_Alpha8;
  static constexpr std::size_t kDefaultUndoBudget = std::size_t{1} << 30U;

  enum class Plane : std::uint8_t { Image, Mask };

  TileStore() = default;
  // Converts image to kFormat. Every tile starts dirty. The store stays empty
  // when there is no memory for the tiles. At its peak the store needs memory
  // for the tiles and one more tile on top of image.
  explicit TileStore(const QImage& image);

  [[nodiscard]] QSize size() const {
    return size_;
  }
  [[nodiscard]] std::size_t tileCount() const {
    return planes_.at(planeIndex(Plane::Image)).size();
  }
  [[nodiscard]] QRect tileRect(std::size_t index) const;
  // Null for a mask tile that nothing has painted.
  [[nodiscard]] const QImage& tile(std::size_t index, Plane plane = Plane::Image) const {
    return planes_.at(planeIndex(plane)).at(index);
  }
  [[nodiscard]] QImage toImage(Plane plane = Plane::Image) const;

  // Calls draw once for each tile under area, with the tile's index and a
  // painter in image pixels that is clipped to area and to the tile. A null
  // mask tile becomes a clear one first. The change joins the open undo step.
  void paint(QRect area, const std::function<void(QPainter&, std::size_t tile)>& draw,
             Plane plane = Plane::Image);
  // Empties the mask as an undo step of its own.
  void clearMask();
  // Closes the open undo step, if anything changed since the last one, and
  // drops the redo steps. Then drops the oldest steps until the tiles that
  // undo keeps fit the budget, but always keeps the newest step.
  void commit();
  void setUndoBudget(std::size_t bytes);
  // The pixels of the tiles of both planes, and of the tiles that undo keeps.
  [[nodiscard]] std::size_t bytes() const;
  [[nodiscard]] std::size_t undoBytes() const;
  // Each first commits the open step. They return false when there is no step.
  bool undo();
  bool redo();
  [[nodiscard]] bool canUndo() const;
  [[nodiscard]] bool canRedo() const;
  // Names the edits behind the current pixels of both planes: undo and redo
  // return to an earlier number, and any new edit, the open step included,
  // gives a number never seen before. A save stores it to tell later whether
  // anything changed.
  [[nodiscard]] std::uint64_t revision() const;

  // The plane's dirty tiles in ascending order. They are clean afterwards.
  [[nodiscard]] std::vector<std::size_t> takeDirty(Plane plane = Plane::Image);

 private:
  static constexpr std::size_t kPlanes = 2;
  template <typename T>
  using PerPlane = std::array<std::vector<T>, kPlanes>;

  struct SavedTile {
    Plane plane;
    std::size_t index;
    QImage image;
  };
  struct Step {
    // The revision this step's edit leads to.
    std::uint64_t revision = 0;
    std::vector<SavedTile> tiles;
  };

  static std::size_t planeIndex(Plane plane) {
    return static_cast<std::size_t>(plane);
  }
  void save(Plane plane, std::size_t index);
  void swapIn(Step& step);
  void trimUndo();

  QSize size_;
  std::size_t columns_ = 0;
  PerPlane<QImage> planes_;
  PerPlane<bool> dirty_;
  // Which tiles the open step already saved.
  PerPlane<bool> saved_;
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
// store. On failure the store is empty and error says why. Slow for a large
// image, so callers run it off the UI thread.
ReadImage readImage(const QString& path);

// Writes image as a PNG at path, through a temporary file, so a failed write
// leaves the old file whole. Returns an empty string, or why it failed.
QString writePng(const QImage& image, const QString& path);

}  // namespace pinsel
