#include <benchmark/benchmark.h>

#include <QApplication>
#include <QColor>
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
#include <numeric>
#include <tracy/Tracy.hpp>
#include <vector>

#ifdef _WIN32
#include <windows.h>
// psapi.h needs windows.h first.
#include <psapi.h>
#else
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#endif

#include "canvas/canvas.hpp"
#include "tiles/tile_store.hpp"

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

struct ProcessMemory {
  double now_mb = 0.0;
  double peak_mb = 0.0;
};

// On Windows the private bytes, which count the driver's upload copies in this
// process too; elsewhere the resident set.
ProcessMemory processMemory() {
#ifdef _WIN32
  constexpr double kMiB = 1024.0 * 1024.0;
  PROCESS_MEMORY_COUNTERS_EX counters{};
  if (GetProcessMemoryInfo(GetCurrentProcess(),
                           reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                           sizeof(counters)) == 0) {
    return {};
  }
  return {.now_mb = static_cast<double>(counters.PrivateUsage) / kMiB,
          .peak_mb = static_cast<double>(counters.PeakPagefileUsage) / kMiB};
#else
  ProcessMemory memory;
  std::ifstream status("/proc/self/status");
  std::string line;
  while (std::getline(status, line)) {
    for (auto [key, mb] : {std::pair{"VmRSS:", &memory.now_mb}, {"VmHWM:", &memory.peak_mb}}) {
      if (line.starts_with(key)) {
        *mb = std::stod(line.substr(std::string_view(key).size())) / 1024.0;
      }
    }
  }
  return memory;
#endif
}

double percentile(std::vector<double> values, double fraction) {
  std::ranges::sort(values);
  const auto index = static_cast<std::size_t>(static_cast<double>(values.size() - 1) * fraction);
  return values.at(index);
}

// Times each frame on its own: input, then a full repaint into an image. Google
// Benchmark reports the mean; the slowest frames decide whether pan and zoom feel
// smooth, so the 99th percentile goes in a counter too.
//
// Rendering into an image also copies the frame back from the GPU, which a frame
// on screen never does. So the wall time overstates a real frame, and the gpu_ms
// counters give the GPU's own time for the draw.
void measureFrames(benchmark::State& state, pinsel::Canvas& canvas,
                   const std::function<void(std::size_t frame)>& input) {
  QImage target(kViewport, QImage::Format_ARGB32_Premultiplied);
  std::vector<double> seconds;
  std::vector<double> gpu_seconds;
  for ([[maybe_unused]] auto _ : state) {
    const auto start = std::chrono::steady_clock::now();
    input(seconds.size());
    canvas.render(&target);
    const std::chrono::duration<double> took = std::chrono::steady_clock::now() - start;
    state.SetIterationTime(took.count());
    // Each frame reports the one before it, so the first report belongs to setUp.
    if (!seconds.empty()) {
      gpu_seconds.push_back(canvas.previousGpuFrameSeconds());
    }
    seconds.push_back(took.count());
    FrameMark;
  }
  state.counters["p99_ms"] = percentile(seconds, 0.99) * 1000.0;
  // The peak covers the whole run so far, not just this benchmark.
  const ProcessMemory memory = processMemory();
  state.counters["mem_mb"] = memory.now_mb;
  state.counters["peak_mem_mb"] = memory.peak_mb;
  // All zero means the backend records no timestamps; a zero counter would read as free.
  if (std::ranges::any_of(gpu_seconds, [](double s) { return s > 0.0; })) {
    state.counters["gpu_ms"] = std::accumulate(gpu_seconds.begin(), gpu_seconds.end(), 0.0) *
                               1000.0 / static_cast<double>(gpu_seconds.size());
    state.counters["gpu_p99_ms"] = percentile(gpu_seconds, 0.99) * 1000.0;
  }
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
  canvas.setImage(pinsel::TileStore(image));
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

// One long brush stroke in a circle, at the default brush size, so each frame
// lays a few dabs and uploads only the tiles they touch.
void BM_Paint(benchmark::State& state) {
  pinsel::Canvas canvas;
  setUp(canvas);
  const QPointF centre(kViewport.width() / 2.0, kViewport.height() / 2.0);
  const auto onCircle = [&](std::size_t frame) {
    const double angle = static_cast<double>(frame) * 0.05;
    return centre + (QPointF(std::cos(angle), std::sin(angle)) * 300.0);
  };
  sendMouse(canvas, QEvent::MouseButtonPress, onCircle(0), Qt::LeftButton, Qt::LeftButton);
  measureFrames(state, canvas, [&](std::size_t frame) {
    sendMouse(canvas, QEvent::MouseMove, onCircle(frame), Qt::NoButton, Qt::LeftButton);
  });
  sendMouse(canvas, QEvent::MouseButtonRelease, centre, Qt::LeftButton, Qt::NoButton);
}
BENCHMARK(BM_Paint)->UseManualTime()->Unit(benchmark::kMillisecond);

}  // namespace

int main(int argc, char** argv) {
  // Qt reads this when it creates the QRhi, and then records GPU timestamps.
  qputenv("QSG_RHI_PROFILE", "1");
  const QApplication app(argc, argv);
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
