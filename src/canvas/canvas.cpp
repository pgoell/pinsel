#include "canvas/canvas.hpp"

#include <QPainter>

namespace pinsel {

Canvas::Canvas(QWidget* parent) : QWidget(parent) {}

void Canvas::paintEvent(QPaintEvent* /*event*/) {
  QPainter painter(this);
  painter.fillRect(rect(), QColor(27, 27, 27));
  painter.setPen(QColor(128, 128, 128));
  painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("Open an image  (Ctrl+O)"));
}

}  // namespace pinsel
