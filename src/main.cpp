#include <QApplication>
#include <QCommandLineParser>
#include <QSize>
#include <QStringList>
#include <QTimer>
#include <string>

#include "project.hpp"
#include "ui/main_window.hpp"
#include "ui/theme.hpp"

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
  parser.addOption(screenshot);
  parser.addOption(layers);
  parser.addOption(open);
  parser.addOption(blank);
  parser.addOption(size);
  parser.process(app);
  const auto sizeOf = [&](const QCommandLineOption& option) {
    const QStringList parts = parser.value(option).split(QLatin1Char('x'));
    const int width = parts.value(0).toInt();
    const int height = parts.value(1).toInt();
    if (parts.size() != 2 || width <= 0 || height <= 0) {
      parser.showHelp(1);
    }
    return QSize(width, height);
  };

  pinsel::MainWindow window;
  if (parser.isSet(size)) {
    window.resize(sizeOf(size));
  }
  if (parser.isSet(blank) && !window.newImage(sizeOf(blank), Qt::white)) {
    qCritical("Not enough memory for the new image");
    return 1;
  }
  const int layer_count = parser.value(layers).toInt();
  window.addLayers(layer_count);
  if (parser.isSet(screenshot)) {
    // Lays the window out at any size, unclipped by the screen, and never shows it.
    window.setAttribute(Qt::WA_DontShowOnScreen);
  }
  window.show();
  const auto saveScreenshot = [&] {
    QApplication::exit(window.grab().save(parser.value(screenshot)) ? 0 : 1);
  };
  if (parser.isSet(open)) {
    window.openImage(parser.value(open), [&](const QString& error) {
      window.addLayers(layer_count);
      if (!error.isEmpty()) {
        qCritical("Could not open %s: %s", qPrintable(parser.value(open)), qPrintable(error));
        QApplication::exit(1);
      } else if (parser.isSet(screenshot)) {
        saveScreenshot();
      }
    });
  } else if (parser.isSet(screenshot)) {
    QTimer::singleShot(0, &window, saveScreenshot);
  }
  return QApplication::exec();
}
