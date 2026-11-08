#include <QApplication>
#include <QCommandLineParser>
#include <QPointF>
#include <QPolygonF>
#include <QSize>
#include <QStringList>
#include <QTimer>
#include <functional>
#include <optional>
#include <string>

#include "app/project.hpp"
#include "ui/main_window.hpp"
#include "ui/theme.hpp"

namespace {

// The option's value as X,Y pairs separated by spaces. Anything else shows the
// help and quits.
QPolygonF pointsOf(QCommandLineParser& parser, const QCommandLineOption& option) {
  QPolygonF points;
  for (const QString& point : parser.value(option).split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
    const QStringList xy = point.split(QLatin1Char(','));
    bool x_ok = false;
    bool y_ok = false;
    const double x = xy.value(0).toDouble(&x_ok);
    const double y = xy.value(1).toDouble(&y_ok);
    if (xy.size() != 2 || !x_ok || !y_ok) {
      parser.showHelp(1);
    }
    points.append(QPointF(x, y));
  }
  return points;
}

// The option's value as WIDTHxHEIGHT. Anything else shows the help and quits.
QSize sizeOf(QCommandLineParser& parser, const QCommandLineOption& option) {
  const QStringList parts = parser.value(option).split(QLatin1Char('x'));
  const int width = parts.value(0).toInt();
  const int height = parts.value(1).toInt();
  if (parts.size() != 2 || width <= 0 || height <= 0) {
    parser.showHelp(1);
  }
  return {width, height};
}

// Loads the model, then inpaints the mask with prompt, skipping each step whose
// value is empty, then calls ready. Quits when the inpaint adds no layer.
void loadAndInpaint(pinsel::MainWindow& window, const QString& model, const QString& prompt,
                    const std::function<void()>& ready) {
  const std::function<void()> inpaint = [&window, prompt, ready] {
    if (prompt.isEmpty()) {
      ready();
      return;
    }
    window.inpaint(prompt, [ready](bool added) {
      if (added) {
        ready();
      } else {
        qCritical("The inpaint added no layer");
        QApplication::exit(1);
      }
    });
  };
  if (model.isEmpty()) {
    inpaint();
  } else {
    window.loadModel(model, [inpaint](const std::optional<pinsel::EngineFailure>&) { inpaint(); });
  }
}

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  pinsel::applyTheme(app);
  QApplication::setApplicationName(QString::fromStdString(std::string(pinsel::name())));

  QCommandLineParser parser;
  parser.addHelpOption();
  const QCommandLineOption screenshot(QStringLiteral("screenshot"),
                                      QStringLiteral("Save the main window as a PNG and quit."),
                                      QStringLiteral("path"));
  const QCommandLineOption size(QStringLiteral("size"),
                                QStringLiteral("Window size, as WIDTHxHEIGHT."),
                                QStringLiteral("size"));
  const QCommandLineOption open(QStringLiteral("open"), QStringLiteral("Open an image."),
                                QStringLiteral("path"));
  const QCommandLineOption blank(QStringLiteral("new"),
                                 QStringLiteral("Start a blank white image, as WIDTHxHEIGHT."),
                                 QStringLiteral("size"));
  const QCommandLineOption layers(QStringLiteral("layers"),
                                  QStringLiteral("Add this many empty layers to the image."),
                                  QStringLiteral("count"));
  const QCommandLineOption lasso(
      QStringLiteral("lasso"),
      QStringLiteral("Fill an outline into the mask, as image points X,Y separated by spaces."),
      QStringLiteral("points"));
  const QCommandLineOption model(
      QStringLiteral("load-model"),
      QStringLiteral("Load an SDXL checkpoint; a screenshot waits until the load ends."),
      QStringLiteral("path"));
  const QCommandLineOption inpaint(
      QStringLiteral("inpaint"),
      QStringLiteral("After the model loads, inpaint the mask; a screenshot waits for the layer."),
      QStringLiteral("prompt"));
  parser.addOption(screenshot);
  parser.addOption(model);
  parser.addOption(inpaint);
  parser.addOption(lasso);
  parser.addOption(layers);
  parser.addOption(open);
  parser.addOption(blank);
  parser.addOption(size);
  parser.process(app);
  const QPolygonF outline = pointsOf(parser, lasso);

  pinsel::MainWindow window;
  if (parser.isSet(size)) {
    window.resize(sizeOf(parser, size));
  }
  if (parser.isSet(blank) && !window.newImage(sizeOf(parser, blank), Qt::white)) {
    qCritical("Not enough memory for the new image");
    return 1;
  }
  const int layer_count = parser.value(layers).toInt();
  window.addLayers(layer_count);
  window.fillMask(outline);
  if (parser.isSet(screenshot)) {
    // Lays the window out at any size, unclipped by the screen, and never shows it.
    window.setAttribute(Qt::WA_DontShowOnScreen);
  }
  window.show();
  const std::function<void()> saveScreenshot = [&] {
    QApplication::exit(window.grab().save(parser.value(screenshot)) ? 0 : 1);
  };
  const std::function<void()> ready = parser.isSet(screenshot) ? saveScreenshot : [] {};
  const std::function<void()> afterOpen = [&] {
    loadAndInpaint(window, parser.value(model), parser.value(inpaint), ready);
  };
  if (parser.isSet(open)) {
    window.openImage(parser.value(open), [&](const QString& error) {
      window.addLayers(layer_count);
      window.fillMask(outline);
      if (!error.isEmpty()) {
        qCritical("Could not open %s: %s", qPrintable(parser.value(open)), qPrintable(error));
        QApplication::exit(1);
      } else {
        afterOpen();
      }
    });
  } else {
    QTimer::singleShot(0, &window, afterOpen);
  }
  return QApplication::exec();
}
