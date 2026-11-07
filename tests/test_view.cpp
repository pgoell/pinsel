#include <gtest/gtest.h>

#include "canvas/view.hpp"

namespace {

using pinsel::View;

View fitted(QSize image, QSizeF viewport) {
  View view;
  view.setViewportSize(viewport);
  view.setImageSize(image);
  return view;
}

TEST(View, FitCentresTheImageAtTheLargestScaleThatShowsAllOfIt) {
  const View view = fitted(QSize(3840, 2160), QSizeF(1000, 1000));
  EXPECT_DOUBLE_EQ(view.scale(), 1000.0 / 3840.0);
  EXPECT_NEAR(view.offset().x(), 0.0, 1e-9);
  EXPECT_DOUBLE_EQ(view.offset().y(), (1000.0 - (2160.0 * view.scale())) / 2.0);
}

TEST(View, StaysFittedWhenTheViewportResizesUntilTheUserMovesIt) {
  View view = fitted(QSize(200, 100), QSizeF(200, 100));
  view.setViewportSize(QSizeF(400, 200));
  EXPECT_DOUBLE_EQ(view.scale(), 2.0);

  view.panBy(QPointF(10, 0));
  view.setViewportSize(QSizeF(800, 400));
  EXPECT_DOUBLE_EQ(view.scale(), 2.0);
  EXPECT_DOUBLE_EQ(view.offset().x(), 10.0);
}

TEST(View, ZoomKeepsTheImagePointUnderTheAnchor) {
  View view = fitted(QSize(1000, 1000), QSizeF(500, 500));
  const QPointF anchor(120, 340);
  const QPointF before = view.toImage(anchor);
  view.zoomAt(anchor, 3.0);
  EXPECT_DOUBLE_EQ(view.scale(), 1.5);
  EXPECT_NEAR(view.toImage(anchor).x(), before.x(), 1e-9);
  EXPECT_NEAR(view.toImage(anchor).y(), before.y(), 1e-9);
}

TEST(View, ZoomStopsAtTheScaleRange) {
  View view = fitted(QSize(100, 100), QSizeF(100, 100));
  view.zoomAt(QPointF(), 1e6);
  EXPECT_DOUBLE_EQ(view.scale(), View::kMaxScale);
  view.zoomAt(QPointF(), 1e-12);
  EXPECT_DOUBLE_EQ(view.scale(), View::kMinScale);
}

TEST(View, FitStaysInTheScaleRangeSoZoomInNeverZoomsOut) {
  View view = fitted(QSize(4, 4), QSizeF(1000, 1000));
  EXPECT_DOUBLE_EQ(view.scale(), View::kMaxScale);
  view.zoomAt(QPointF(), 2.0);
  EXPECT_DOUBLE_EQ(view.scale(), View::kMaxScale);
}

TEST(View, VisibleRectIsTheViewportInImagePixels) {
  View view = fitted(QSize(100, 100), QSizeF(100, 100));
  view.zoomAt(QPointF(), 2.0);
  view.panBy(QPointF(-20, -40));
  EXPECT_EQ(view.visibleImageRect(), QRectF(10, 20, 50, 50));
}

}  // namespace
