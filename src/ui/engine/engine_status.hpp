#pragma once

#include <QString>
#include <QWidget>
#include <functional>
#include <map>
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

  void showLoad(int job, const QString& path);
  void showProgress(int job, const EngineProgress& progress);
  void showEnd(int job, const std::optional<EngineFailure>& failure);

 private:
  void showText(const QString& text);

  QLabel* label_;
  QProgressBar* bar_;
  QToolButton* cancel_;
  // The model each open load reads, by its job.
  std::map<int, QString> loads_;
};

}  // namespace pinsel
