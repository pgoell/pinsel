#include "canvas/canvas.hpp"

#include <QPainter>

namespace pinsel {

Canvas::Canvas(QWidget* parent) : QWidget(parent) {}

void Canvas::paintEvent(QPaintEvent* /*event*/) {
  QPainter painter(this);
  painter.fillRect(rect(), palette().dark());
  painter.setPen(palette().brightText().color());
  painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("Canvas (milestone 1)"));
}

}  // namespace pinsel
