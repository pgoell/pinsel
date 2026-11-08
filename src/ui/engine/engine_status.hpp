#pragma once

#include <QString>
#include <QWidget>
#include <functional>
#include <optional>

#include "engine/engine.hpp"

class QLabel;
class QProgressBar;
class QToolButton;

namespace pinsel {

// Shows, for the status bar, what the engine is doing: the model it holds, a
// load's progress with a button that calls cancel, or why a load failed.
class EngineStatus : public QWidget {
 public:
  explicit EngineStatus(std::function<void()> cancel, QWidget* parent = nullptr);

  void showLoad(const QString& path);
  void showProgress(const EngineProgress& progress);
  // busy says whether another job is still open.
  void showEnd(const QString& path, const std::optional<EngineFailure>& failure, bool busy);

 private:
  void showText(const QString& text);

  QLabel* label_;
  QProgressBar* bar_;
  QToolButton* cancel_;
};

}  // namespace pinsel
