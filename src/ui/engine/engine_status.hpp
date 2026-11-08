#pragma once

#include <QString>
#include <QWidget>
#include <functional>
#include <map>
#include <memory>
#include <optional>

#include "engine/engine.hpp"

class QLabel;
class QProgressBar;
class QToolButton;

namespace pinsel {

// Owns the engine and shows, for the status bar, what it is doing: the model it
// holds, a job's progress with a button that cancels it, or why a job failed.
class EngineStatus : public QWidget {
 public:
  explicit EngineStatus(QWidget* parent = nullptr);

  // Calls ended with the failure, or with none once the model is loaded.
  void loadModel(const QString& path,
                 std::function<void(const std::optional<EngineFailure>&)> ended = {});
  // Cancels every job that has not ended.
  void cancel();

 private:
  void showText(const QString& text);
  void end(int job, const std::optional<EngineFailure>& failure);

  QLabel* label_;
  QProgressBar* bar_;
  QToolButton* cancel_;
  std::unique_ptr<Engine> engine_;
  // Loads that have not ended.
  struct Pending {
    QString model;
    std::function<void(const std::optional<EngineFailure>&)> ended;
  };
  std::map<int, Pending> pending_;
};

}  // namespace pinsel
