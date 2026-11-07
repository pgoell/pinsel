#include <gtest/gtest.h>

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QMouseEvent>
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

void sendMouse(QWidget& widget, QEvent::Type type, QPointF pos, Qt::MouseButton button,
               Qt::MouseButtons buttons) {
  QMouseEvent event(type, pos, widget.mapToGlobal(pos), button, buttons, Qt::NoModifier);
  QCoreApplication::sendEvent(&widget, &event);
}

void drag(QWidget& widget, QPointF from, QPointF to) {
  sendMouse(widget, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
  sendMouse(widget, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
  sendMouse(widget, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
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

  [[nodiscard]] QColor at(QPoint point) const {
    return canvas_->tiles().toImage().pixelColor(point);
  }

  std::unique_ptr<Canvas> canvas_;
};

TEST_F(CanvasTest, BrushDragPaintsAndUndoRedoFollow) {
  int changes = 0;
  canvas_->setHistoryChanged([&] { ++changes; });
  drag(*canvas_, QPointF(50, 150), QPointF(350, 150));
  EXPECT_EQ(at(QPoint(200, 150)), QColor(Qt::black));
  EXPECT_EQ(at(QPoint(200, 100)), QColor(Qt::white));
  EXPECT_TRUE(canvas_->tiles().canUndo());
  EXPECT_EQ(changes, 1);

  QImage frame(kSize, QImage::Format_ARGB32_Premultiplied);
  canvas_->render(&frame);
  frame.save(QStringLiteral(PINSEL_SCREENSHOTS "/brush-stroke.png"));

  canvas_->undo();
  EXPECT_EQ(at(QPoint(200, 150)), QColor(Qt::white));
  canvas_->redo();
  EXPECT_EQ(at(QPoint(200, 150)), QColor(Qt::black));
  EXPECT_EQ(changes, 3);
}

TEST_F(CanvasTest, EraserDragClears) {
  canvas_->setTool(Canvas::Tool::Eraser);
  drag(*canvas_, QPointF(50, 150), QPointF(350, 150));
  EXPECT_EQ(at(QPoint(200, 150)).alpha(), 0);
}

TEST_F(CanvasTest, PanToolDragLeavesTheImageAlone) {
  canvas_->setTool(Canvas::Tool::Pan);
  drag(*canvas_, QPointF(50, 150), QPointF(350, 150));
  EXPECT_EQ(at(QPoint(200, 150)), QColor(Qt::white));
  EXPECT_FALSE(canvas_->tiles().canUndo());
}

}  // namespace
