#include <QApplication>
#include <QCommandLineParser>
#include <QStringList>
#include <QTimer>
#include <string>

#include "project.hpp"
#include "ui/main_window.hpp"

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QApplication::setApplicationName(QString::fromStdString(std::string(pinsel::name())));

  QCommandLineParser parser;
  parser.addHelpOption();
  const QCommandLineOption screenshot(QStringLiteral("screenshot"),
                                      QStringLiteral("Save the main window as a PNG and quit."),
                                      QStringLiteral("path"));
  const QCommandLineOption size(QStringLiteral("size"),
                                QStringLiteral("Window size, as WIDTHxHEIGHT."),
                                QStringLiteral("size"));
  parser.addOption(screenshot);
  parser.addOption(size);
  parser.process(app);

  pinsel::MainWindow window;
  if (parser.isSet(size)) {
    const QStringList parts = parser.value(size).split(QLatin1Char('x'));
    const int width = parts.value(0).toInt();
    const int height = parts.value(1).toInt();
    if (parts.size() != 2 || width <= 0 || height <= 0) {
      parser.showHelp(1);
    }
    window.resize(width, height);
  }
  if (parser.isSet(screenshot)) {
    // Lays the window out at any size, unclipped by the screen, and never shows it.
    window.setAttribute(Qt::WA_DontShowOnScreen);
  }
  window.show();
  if (parser.isSet(screenshot)) {
    QTimer::singleShot(0, &window, [&] {
      QApplication::exit(window.grab().save(parser.value(screenshot)) ? 0 : 1);
    });
  }
  return QApplication::exec();
}
