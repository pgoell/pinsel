#include "canvas/canvas.hpp"

#include <QPainter>

namespace pinsel {

Canvas::Canvas(QWidget* parent) : QWidget(parent) {}

void Canvas::paintEvent(QPaintEvent* /*event*/) {
  QPainter painter(this);
  painter.fillRect(rect(), palette().base());
  painter.setPen(palette().placeholderText().color());
  painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("No image open"));
}

}  // namespace pinsel
