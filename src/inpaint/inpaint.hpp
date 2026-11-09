#pragma once

#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>
#include <optional>

#include "tiles/tile_store.hpp"

namespace pinsel {

// One inpaint job's region: a padded box around the mask, and the size the
// model draws it at. SDXL draws best near 1024 x 1024 pixels in all, with sides
// a multiple of 64, so the box scales up or down to that.
struct InpaintCrop {
  // In image pixels.
  QRect area;
  QSize size;
};

// What the model reads, at the crop's size, and the mask at full size, which
// says where its answer lands.
struct InpaintInput {
  InpaintCrop crop;
  // The visible layers, merged, as opaque RGB.
  QImage image;
  // White where the model draws, black where it keeps the image.
  QImage mask;
  // The mask under crop.area, unscaled, in TileStore::kMaskFormat.
  QImage area_mask;
};

// The crop for a mask whose painted pixels fit in bounds, in an image of size.
// Null when bounds is empty.
std::optional<InpaintCrop> planInpaint(QRect bounds, QSize image);

// The input for the mask as it is now. Null when nothing is masked, or when
// there is no memory. Merges the layers, so callers run it off the UI thread
// on a copy of the store, which shares the tiles.
std::optional<InpaintInput> inpaintInput(const TileStore& tiles);

// Scales result back over the crop and keeps it only where the mask was, as a
// layer for TileStore::addLayer, which one undo removes. Null when result is
// null or there is no memory.
std::optional<TileStore::LayerContent> inpaintLayer(const InpaintInput& input, const QImage& result,
                                                    const QString& name);

}  // namespace pinsel
