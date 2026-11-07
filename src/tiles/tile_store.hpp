#pragma once

#include <QImage>
#include <QRect>
#include <QSize>
#include <cstddef>
#include <functional>
#include <vector>

class QPainter;

namespace pinsel {

// The image as a grid of tiles, each a QImage of its own, with undo and redo.
// Tiles count from the top left, row by row. An edit marks the tiles it touches
// as dirty until takeDirty collects them, so the canvas uploads only those.
//
// Undo keeps whole tiles from before each change. QImage shares its pixels
// between copies until one of them is written, so keeping a tile costs nothing
// until the edit detaches it.
class TileStore {
 public:
  static constexpr int kTileSize = 512;
  // RGBA8 textures take bytes in this order on every GPU backend.
  static constexpr QImage::Format kFormat = QImage::Format_RGBA8888_Premultiplied;
  static constexpr std::size_t kDefaultUndoBudget = std::size_t{1} << 30U;

  TileStore() = default;
  // Converts image to kFormat. Every tile starts dirty. The store stays empty
  // when there is no memory for the tiles. At its peak the store needs memory
  // for the tiles and one more tile on top of image.
  explicit TileStore(const QImage& image);

  [[nodiscard]] QSize size() const {
    return size_;
  }
  [[nodiscard]] std::size_t tileCount() const {
    return tiles_.size();
  }
  [[nodiscard]] QRect tileRect(std::size_t index) const;
  [[nodiscard]] const QImage& tile(std::size_t index) const {
    return tiles_.at(index);
  }
  [[nodiscard]] QImage toImage() const;

  // Calls draw once for each tile under area, with the tile's index and a
  // painter in image pixels that is clipped to area and to the tile. The change
  // joins the open undo step.
  void paint(QRect area, const std::function<void(QPainter&, std::size_t tile)>& draw);
  // Closes the open undo step, if anything changed since the last one, and
  // drops the redo steps. Then drops the oldest steps until the tiles that
  // undo keeps fit the budget, but always keeps the newest step.
  void commit();
  void setUndoBudget(std::size_t bytes);
  // The pixels of the tiles, and of the tiles that undo keeps.
  [[nodiscard]] std::size_t bytes() const;
  [[nodiscard]] std::size_t undoBytes() const;
  // Each first commits the open step. They return false when there is no step.
  bool undo();
  bool redo();
  [[nodiscard]] bool canUndo() const;
  [[nodiscard]] bool canRedo() const;

  // The dirty tiles in ascending order. They are clean afterwards.
  [[nodiscard]] std::vector<std::size_t> takeDirty();

 private:
  struct SavedTile {
    std::size_t index;
    QImage image;
  };
  using Step = std::vector<SavedTile>;

  void swapIn(Step& step);
  void trimUndo();

  QSize size_;
  std::size_t columns_ = 0;
  std::vector<QImage> tiles_;
  std::vector<bool> dirty_;
  // Which tiles the open step already saved, by tile index.
  std::vector<bool> saved_;
  Step open_;
  std::vector<Step> undo_;
  std::vector<Step> redo_;
  std::size_t undo_budget_ = kDefaultUndoBudget;
};

}  // namespace pinsel
