#include <gtest/gtest.h>

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QLineF>
#include <QMouseEvent>
#include <QPoint>
#include <QPointF>
#include <QString>
#include <array>
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

TEST_F(CanvasTest, EraseMaskEmptiesTheMaskAndUndoBringsItBack) {
  canvas().setTool(Canvas::Tool::Mask);
  drag(canvas(), kAcross);
  canvas().eraseMask();
  EXPECT_EQ(maskAt(QPoint(200, 150)), 0);
  canvas().undo();
  EXPECT_EQ(maskAt(QPoint(200, 150)), 255);
}

}  // namespace
