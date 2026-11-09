#pragma once

#include <QString>

#include "tiles/tile_store.hpp"

namespace pinsel {

// OpenRaster 0.0.6: a zip of PNG layers and a stack.xml that orders them.
// https://www.openraster.org/baseline/layer-stack-spec.html
//
// The mask goes in as a hidden layer on top, marked with pinsel:mask, so other
// editors show it as a layer and pinsel reads it back as the mask.
ReadImage readOpenRaster(const QString& path);
QString writeOpenRaster(const TileStore& tiles, const QString& path);

}  // namespace pinsel
