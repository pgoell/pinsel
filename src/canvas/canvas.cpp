#include "canvas/canvas.hpp"

#include <QPainter>

namespace pinsel {

Canvas::Canvas(QWidget* parent) : QWidget(parent) {}

void Canvas::paintEvent(QPaintEvent* /*event*/) {
  QPainter painter(this);
  painter.fillRect(rect(), QColor(80, 80, 80));
  painter.setPen(Qt::white);
  painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("Canvas (milestone 1)"));
}

}  // namespace pinsel
