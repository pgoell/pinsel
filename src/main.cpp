#include <QApplication>
#include <QCommandLineParser>
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
  parser.addOption(screenshot);
  parser.process(app);

  pinsel::MainWindow window;
  window.show();
  if (parser.isSet(screenshot)) {
    QTimer::singleShot(0, &window, [&] {
      QApplication::exit(window.grab().save(parser.value(screenshot)) ? 0 : 1);
    });
  }
  return QApplication::exec();
}
