#include <benchmark/benchmark.h>

#include <QApplication>
#include <QImage>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <functional>
#include <tracy/Tracy.hpp>
#include <vector>

#include "canvas/canvas.hpp"

namespace {

constexpr QSize kViewport{1920, 1080};
constexpr int kWheelNotch = 120;

void sendMouse(QWidget& widget, QEvent::Type type, QPointF pos, Qt::MouseButton button,
               Qt::MouseButtons buttons) {
  QMouseEvent event(type, pos, widget.mapToGlobal(pos), button, buttons, Qt::NoModifier);
  QCoreApplication::sendEvent(&widget, &event);
}

void sendWheel(QWidget& widget, QPointF pos, int delta) {
  QWheelEvent event(pos, widget.mapToGlobal(pos), QPoint(), QPoint(0, delta), Qt::NoButton,
                    Qt::NoModifier, Qt::NoScrollPhase, false);
  QCoreApplication::sendEvent(&widget, &event);
}

// Times each frame on its own: input, then a full repaint into an image. Google
// Benchmark reports the mean; the slowest frames decide whether pan and zoom feel
// smooth, so the 99th percentile goes in a counter too.
void measureFrames(benchmark::State& state, pinsel::Canvas& canvas,
                   const std::function<void(std::size_t frame)>& input) {
  QImage target(kViewport, QImage::Format_ARGB32_Premultiplied);
  std::vector<double> seconds;
  for ([[maybe_unused]] auto _ : state) {
    const auto start = std::chrono::steady_clock::now();
    input(seconds.size());
    canvas.render(&target);
    const std::chrono::duration<double> took = std::chrono::steady_clock::now() - start;
    state.SetIterationTime(took.count());
    seconds.push_back(took.count());
    FrameMark;
  }
  std::ranges::sort(seconds);
  const auto p99 = static_cast<std::size_t>(static_cast<double>(seconds.size() - 1) * 0.99);
  state.counters["p99_ms"] = seconds.at(p99) * 1000.0;
}

// Shows the canvas off screen with a 4K image, as milestone 1 promises: a
// gradient under a grid. With Qt 6.10 on OpenGL, rendering a widget that was
// never shown can skip the GPU frame and leaks memory on every call.
void setUp(pinsel::Canvas& canvas) {
  canvas.resize(kViewport);
  canvas.setAttribute(Qt::WA_DontShowOnScreen);
  canvas.show();

  QImage image(3840, 2160, QImage::Format_RGBA8888_Premultiplied);
  QPainter painter(&image);
  QLinearGradient gradient(0, 0, image.width(), image.height());
  gradient.setColorAt(0, QColor(40, 90, 160));
  gradient.setColorAt(1, QColor(220, 140, 60));
  painter.fillRect(image.rect(), gradient);
  painter.setPen(QColor(255, 255, 255, 120));
  for (int x = 0; x < image.width(); x += 128) {
    painter.drawLine(x, 0, x, image.height());
  }
  for (int y = 0; y < image.height(); y += 128) {
    painter.drawLine(0, y, image.width(), y);
  }
  painter.end();
  canvas.setImage(image);
}

void BM_Pan(benchmark::State& state) {
  pinsel::Canvas canvas;
  setUp(canvas);
  const QPointF centre(kViewport.width() / 2.0, kViewport.height() / 2.0);
  sendMouse(canvas, QEvent::MouseButtonPress, centre, Qt::MiddleButton, Qt::MiddleButton);
  measureFrames(state, canvas, [&](std::size_t frame) {
    // Drags in a circle, so the pan never runs off the image.
    const double angle = static_cast<double>(frame) * 0.05;
    const QPointF pos = centre + QPointF(std::cos(angle), std::sin(angle)) * 300.0;
    sendMouse(canvas, QEvent::MouseMove, pos, Qt::NoButton, Qt::MiddleButton);
  });
  sendMouse(canvas, QEvent::MouseButtonRelease, centre, Qt::MiddleButton, Qt::NoButton);
}
BENCHMARK(BM_Pan)->UseManualTime()->Unit(benchmark::kMillisecond);

void BM_Zoom(benchmark::State& state) {
  pinsel::Canvas canvas;
  setUp(canvas);
  const QPointF centre(kViewport.width() / 2.0, kViewport.height() / 2.0);
  measureFrames(state, canvas, [&](std::size_t frame) {
    // Ten notches in, ten out, so the zoom stays in range.
    const bool zoomIn = (frame / 10) % 2 == 0;
    sendWheel(canvas, centre, zoomIn ? kWheelNotch : -kWheelNotch);
  });
}
BENCHMARK(BM_Zoom)->UseManualTime()->Unit(benchmark::kMillisecond);

}  // namespace

int main(int argc, char** argv) {
  const QApplication app(argc, argv);
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
