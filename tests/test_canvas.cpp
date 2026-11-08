#include <gtest/gtest.h>

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QLineF>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPoint>
#include <QPointF>
#include <QString>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <tuple>

#include "canvas/canvas.hpp"
#include "tiles/tile_store.hpp"

namespace {

using pinsel::Canvas;
using pinsel::TileStore;

// The image is the size of the canvas, so the fitted view maps one widget pixel
// to one image pixel.
constexpr QSize kSize{400, 300};

void ensureApp() {
  static int argc = 1;
  static std::array<char, 6> name{"tests"};
  static std::array<char*, 2> argv{name.data(), nullptr};
  // Never deleted: Qt's own statics may outlive any owner the test could give it.
  static const QApplication* const app = new QApplication(argc, argv.data());
  std::ignore = app;
}

void drag(QWidget& widget, QLineF path, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  const auto send = [&](QEvent::Type type, QPointF pos, Qt::MouseButton button,
                        Qt::MouseButtons buttons) {
    QMouseEvent event(type, pos, widget.mapToGlobal(pos), button, buttons, modifiers);
    QCoreApplication::sendEvent(&widget, &event);
  };
  send(QEvent::MouseButtonPress, path.p1(), Qt::LeftButton, Qt::LeftButton);
  send(QEvent::MouseMove, path.p2(), Qt::NoButton, Qt::LeftButton);
  send(QEvent::MouseButtonRelease, path.p2(), Qt::LeftButton, Qt::NoButton);
}

constexpr QLineF kAcross(50, 150, 350, 150);

// Presses at the first point, moves through the rest and releases at the last,
// unless asked to stay pressed.
void lasso(QWidget& widget, std::initializer_list<QPointF> points,
           Qt::KeyboardModifiers modifiers = Qt::NoModifier, bool release = true) {
  const auto send = [&](QEvent::Type type, QPointF pos, Qt::MouseButton button,
                        Qt::MouseButtons buttons) {
    QMouseEvent event(type, pos, widget.mapToGlobal(pos), button, buttons, modifiers);
    QCoreApplication::sendEvent(&widget, &event);
  };
  send(QEvent::MouseButtonPress, *points.begin(), Qt::LeftButton, Qt::LeftButton);
  for (const QPointF& point : points) {
    send(QEvent::MouseMove, point, Qt::NoButton, Qt::LeftButton);
  }
  if (release) {
    send(QEvent::MouseButtonRelease, *std::prev(points.end()), Qt::LeftButton, Qt::NoButton);
  }
}

constexpr std::initializer_list<QPointF> kBox{
    QPointF(100, 100),
    QPointF(300, 100),
    QPointF(300, 250),
    QPointF(100, 250),
};

// Of any color channel, over every pixel of two images of one size.
int largestDifference(const QImage& one, const QImage& other) {
  int largest = 0;
  for (int y = 0; y < one.height(); ++y) {
    for (int x = 0; x < one.width(); ++x) {
      const QRgb a = one.pixel(x, y);
      const QRgb b = other.pixel(x, y);
      largest = std::max({
          largest,
          std::abs(qRed(a) - qRed(b)),
          std::abs(qGreen(a) - qGreen(b)),
          std::abs(qBlue(a) - qBlue(b)),
      });
    }
  }
  return largest;
}

class CanvasTest : public testing::Test {
 protected:
  void SetUp() override {
    ensureApp();
    canvas_ = std::make_unique<Canvas>();
    canvas_->resize(kSize);
    canvas_->setAttribute(Qt::WA_DontShowOnScreen);
    canvas_->show();
    QImage image(kSize, TileStore::kFormat);
    image.fill(Qt::white);
    canvas_->setImage(TileStore(image));
    canvas_->setBrush({.size = 20, .opacity = 1.0, .color = Qt::black, .erase = false});
  }

  [[nodiscard]] Canvas& canvas() const {
    return *canvas_;
  }
  [[nodiscard]] QColor at(QPoint point) const {
    return canvas_->tiles().toImage().pixelColor(point);
  }
  [[nodiscard]] int maskAt(QPoint point) const {
    return canvas_->tiles().toImage(TileStore::Plane::Mask).pixelColor(point).alpha();
  }

 private:
  std::unique_ptr<Canvas> canvas_;
};

TEST_F(CanvasTest, BrushDragPaintsAStroke) {
  drag(canvas(), kAcross);
  EXPECT_EQ(at(QPoint(200, 150)), QColor(Qt::black));
  EXPECT_EQ(at(QPoint(200, 100)), QColor(Qt::white));

  QImage frame(kSize, QImage::Format_ARGB32_Premultiplied);
  canvas().render(&frame);
  EXPECT_TRUE(frame.save(QStringLiteral(PINSEL_SCREENSHOTS "/brush-stroke.png")));
}

TEST_F(CanvasTest, UndoAndRedoTheStrokeAndReportEachChange) {
  int changes = 0;
  canvas().setHistoryChanged([&] { ++changes; });
  drag(canvas(), kAcross);
  canvas().undo();
  EXPECT_EQ(at(QPoint(200, 150)), QColor(Qt::white));
  canvas().redo();
  EXPECT_EQ(at(QPoint(200, 150)), QColor(Qt::black));
  EXPECT_EQ(changes, 3);
}

TEST_F(CanvasTest, EraserDragClears) {
  canvas().setTool(Canvas::Tool::Eraser);
  drag(canvas(), kAcross);
  EXPECT_EQ(at(QPoint(200, 150)).alpha(), 0);
}

TEST_F(CanvasTest, PanToolDragLeavesTheImageAlone) {
  canvas().setTool(Canvas::Tool::Pan);
  drag(canvas(), kAcross);
  EXPECT_EQ(at(QPoint(200, 150)), QColor(Qt::white));
  EXPECT_FALSE(canvas().tiles().canUndo());
}

TEST_F(CanvasTest, MaskDragMasksAndLeavesTheImageAlone) {
  canvas().setTool(Canvas::Tool::Mask);
  drag(canvas(), kAcross);
  EXPECT_EQ(maskAt(QPoint(200, 150)), 255);
  EXPECT_EQ(maskAt(QPoint(200, 100)), 0);
  EXPECT_EQ(at(QPoint(200, 150)), QColor(Qt::white));

  QImage frame(kSize, QImage::Format_ARGB32_Premultiplied);
  canvas().render(&frame);
  EXPECT_TRUE(frame.save(QStringLiteral(PINSEL_SCREENSHOTS "/mask-stroke.png")));
}

TEST_F(CanvasTest, MaskDragWithAltUnmasks) {
  canvas().setTool(Canvas::Tool::Mask);
  drag(canvas(), kAcross);
  drag(canvas(), QLineF(200, 150, 200, 150), Qt::AltModifier);
  EXPECT_EQ(maskAt(QPoint(200, 150)), 0);
  EXPECT_EQ(maskAt(QPoint(300, 150)), 255);
}

TEST_F(CanvasTest, LassoFillsTheOutlineIntoTheMaskAsOneStep) {
  canvas().setTool(Canvas::Tool::Lasso);
  lasso(canvas(), kBox);
  EXPECT_EQ(maskAt(QPoint(200, 175)), 255);
  EXPECT_EQ(maskAt(QPoint(50, 175)), 0);
  EXPECT_EQ(maskAt(QPoint(200, 280)), 0);
  EXPECT_EQ(at(QPoint(200, 175)), QColor(Qt::white));
  canvas().undo();
  EXPECT_EQ(maskAt(QPoint(200, 175)), 0);
  EXPECT_FALSE(canvas().tiles().canUndo());
}

TEST_F(CanvasTest, LassoWithAltUnmasks) {
  canvas().setTool(Canvas::Tool::Lasso);
  lasso(canvas(), kBox);
  lasso(canvas(), {QPointF(150, 150), QPointF(250, 150), QPointF(250, 200), QPointF(150, 200)},
        Qt::AltModifier);
  EXPECT_EQ(maskAt(QPoint(200, 175)), 0);
  EXPECT_EQ(maskAt(QPoint(120, 120)), 255);
}

TEST_F(CanvasTest, LassoShowsItsOutlineUntilAnotherToolCancelsIt) {
  QImage before(kSize, QImage::Format_ARGB32_Premultiplied);
  canvas().render(&before);
  canvas().setTool(Canvas::Tool::Lasso);
  lasso(canvas(), kBox, Qt::NoModifier, false);
  QImage drawing(kSize, QImage::Format_ARGB32_Premultiplied);
  canvas().render(&drawing);
  EXPECT_TRUE(drawing.save(QStringLiteral(PINSEL_SCREENSHOTS "/lasso-outline.png")));
  EXPECT_GT(largestDifference(before, drawing), 0);

  canvas().setTool(Canvas::Tool::Mask);
  QImage after(kSize, QImage::Format_ARGB32_Premultiplied);
  canvas().render(&after);
  EXPECT_EQ(largestDifference(before, after), 0);
  EXPECT_FALSE(canvas().tiles().hasMask());
}

TEST_F(CanvasTest, EraseMaskEmptiesTheMaskAndUndoBringsItBack) {
  canvas().setTool(Canvas::Tool::Mask);
  drag(canvas(), kAcross);
  canvas().eraseMask();
  EXPECT_EQ(maskAt(QPoint(200, 150)), 0);
  canvas().undo();
  EXPECT_EQ(maskAt(QPoint(200, 150)), 255);
}

TEST_F(CanvasTest, BrushPaintsOnTheCurrentLayer) {
  canvas().edit([](TileStore& tiles) { tiles.addLayer(); });
  drag(canvas(), kAcross);
  const TileStore& tiles = canvas().tiles();
  EXPECT_EQ(tiles.layerTile(1, 0).pixelColor(200, 150), QColor(Qt::black));
  EXPECT_EQ(tiles.layerTile(0, 0).pixelColor(200, 150), QColor(Qt::white));
  EXPECT_EQ(at(QPoint(200, 150)), QColor(Qt::black));
}

// The export flattens with QPainter and the canvas with the GPU; both must
// give the same picture. Bands of each mode, at several opacities, over and
// under plain layers, with soft alpha. A machine with no GPU skips it.
TEST_F(CanvasTest, TheGpuBlendsLayersAsTheExportDoes) {
  using Mode = TileStore::BlendMode;
  const auto band = [&](int index, Mode mode, double opacity) {
    canvas().edit([&](TileStore& tiles) {
      tiles.addLayer();
      const QRect area(20 + (index * 60), 10, 120, 280);
      tiles.paint(area, [&](QPainter& painter, std::size_t /*tile*/) {
        QLinearGradient gradient(area.topLeft(), area.bottomLeft());
        gradient.setColorAt(0, QColor(250, 40, 90, 255));
        gradient.setColorAt(1, QColor(30, 200, 240, 60));
        painter.fillRect(area, gradient);
      });
      tiles.commit();
      tiles.setLayerBlendMode(tiles.currentLayer(), mode);
      tiles.setLayerOpacity(tiles.currentLayer(), opacity);
    });
  };
  canvas().edit([](TileStore& tiles) {
    tiles.paint(QRect(0, 0, 200, 300), [](QPainter& painter, std::size_t /*tile*/) {
      painter.fillRect(QRect(0, 0, 200, 300), QColor(60, 90, 120));
    });
  });
  band(0, Mode::Multiply, 0.8);
  band(1, Mode::Normal, 1.0);
  band(2, Mode::Overlay, 1.0);
  band(3, Mode::Screen, 0.6);
  band(4, Mode::Normal, 0.5);
  band(5, Mode::Normal, 1.0);

  const QImage frame = canvas().grabFramebuffer().convertToFormat(QImage::Format_ARGB32);
  if (frame.isNull()) {
    GTEST_SKIP() << "No GPU";
  }
  QImage expected(kSize, QImage::Format_ARGB32_Premultiplied);
  expected.fill(canvas().palette().base().color());
  QPainter(&expected).drawImage(0, 0, canvas().tiles().toImage());
  expected.convertTo(QImage::Format_ARGB32);
  ASSERT_EQ(frame.size(), expected.size());
  EXPECT_LE(largestDifference(frame, expected), 3);
  EXPECT_TRUE(frame.save(QStringLiteral(PINSEL_SCREENSHOTS "/blend-modes.png")));
}

}  // namespace
