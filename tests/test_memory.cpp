#include <gtest/gtest.h>

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QRect>
#include <atomic>
#include <cstddef>

#include "tiles/tile_store.hpp"

#ifdef PINSEL_ASAN
// From <sanitizer/allocator_interface.h>, which not every clang-tidy ships.
extern "C" {
std::size_t __sanitizer_get_current_allocated_bytes();
int __sanitizer_install_malloc_and_free_hooks(void (*malloc_hook)(const volatile void*,
                                                                  std::size_t),
                                              void (*free_hook)(const volatile void*));
}
#endif

namespace {

using pinsel::TileStore;

constexpr int kTile = TileStore::kTileSize;
constexpr auto kTileBytes = std::size_t{kTile} * kTile * 4;
// 4K, so the edge tiles are partial.
constexpr QSize k4K{3840, 2160};
constexpr std::size_t k4KBytes = std::size_t{3840} * 2160 * 4;

#ifdef PINSEL_ASAN
// The most heap in use since the last reset, from AddressSanitizer's allocator
// hooks. The hooks see every malloc, Qt's included.
std::atomic<std::size_t>& peak() {
  static std::atomic<std::size_t> bytes{0};
  return bytes;
}

void onMalloc(const volatile void* /*ptr*/, std::size_t /*size*/) {
  const std::size_t now = __sanitizer_get_current_allocated_bytes();
  std::size_t seen = peak().load();
  while (now > seen && !peak().compare_exchange_weak(seen, now)) {
  }
}

void onFree(const volatile void* /*ptr*/) {}

// Peak heap growth while it lives, over the heap in use when it began.
class PeakMeter {
 public:
  PeakMeter() : base_(__sanitizer_get_current_allocated_bytes()) {
    // Each install adds one more pair of hooks, so install them once.
    static const bool installed = __sanitizer_install_malloc_and_free_hooks(onMalloc, onFree) != 0;
    installed_ = installed;
    peak().store(base_);
  }
  [[nodiscard]] bool installed() const {
    return installed_;
  }
  [[nodiscard]] std::size_t peakGrowth() const {
    return peak().load() - base_;
  }

 private:
  std::size_t base_;
  bool installed_ = false;
};
#endif

QImage gradient(QSize size, QImage::Format format) {
  QImage image(size, format);
  QPainter painter(&image);
  QLinearGradient fill(0, 0, size.width(), size.height());
  fill.setColorAt(0, QColor(40, 90, 160, 200));
  fill.setColorAt(1, QColor(220, 140, 60, 255));
  painter.fillRect(image.rect(), fill);
  return image;
}

TEST(TileStoreMemory, HoldsOneCopyOfTheImage) {
  const TileStore store(gradient(k4K, QImage::Format_RGB32));
  EXPECT_EQ(store.bytes(), k4KBytes);
  EXPECT_EQ(store.undoBytes(), 0U);
}

TEST(TileStoreMemory, UndoKeepsOnlyTheTilesAPaintTouched) {
  TileStore store(gradient(k4K, TileStore::kFormat));
  const QRect across(kTile - 5, 10, 10, 10);
  store.paint(across,
              [&](QPainter& painter, std::size_t /*tile*/) { painter.fillRect(across, Qt::red); });
  store.commit();
  EXPECT_EQ(store.undoBytes(), 2 * kTileBytes);
  EXPECT_EQ(store.bytes(), k4KBytes);
}

#ifdef PINSEL_ASAN
// Decoders hand back these formats; each must convert without a full copy.
class LoadPeak : public testing::TestWithParam<QImage::Format> {};

TEST_P(LoadPeak, NeedsTheTilesAndOneMoreTile) {
  const QImage image = gradient(k4K, GetParam());
  const PeakMeter meter;
  if (!meter.installed()) {
    GTEST_SKIP() << "AddressSanitizer refused the allocator hooks";
  }
  const TileStore store(image);
  ASSERT_EQ(store.bytes(), k4KBytes);
  // A little room for Qt's own bookkeeping.
  constexpr std::size_t kSlack = std::size_t{64} * 1024;
  EXPECT_LE(meter.peakGrowth(), k4KBytes + kTileBytes + kSlack);
}

INSTANTIATE_TEST_SUITE_P(Formats, LoadPeak,
                         testing::Values(QImage::Format_RGB32, QImage::Format_ARGB32,
                                         QImage::Format_RGBA8888_Premultiplied,
                                         QImage::Format_RGB888, QImage::Format_Grayscale8));
#endif

}  // namespace
