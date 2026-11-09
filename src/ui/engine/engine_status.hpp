#pragma once

#include <QString>
#include <QWidget>
#include <cstdint>
#include <functional>
#include <optional>

#include "engine/engine.hpp"

class QLabel;
class QProgressBar;
class QToolButton;

namespace pinsel {

// Shows, for the status bar, what the engine is doing: the model it holds, a
// job's progress with a button that calls cancel, or why a job failed.
class EngineStatus : public QWidget {
 public:
  enum class Job : std::uint8_t { Load, Inpaint };

  explicit EngineStatus(std::function<void()> cancel, QWidget* parent = nullptr);

  // model is the checkpoint's path: the one a load reads, or the one an
  // inpaint runs on.
  void showStart(Job job, const QString& model);
  void showProgress(const EngineProgress& progress);
  // busy says whether another job is still open.
  void showEnd(Job job, const QString& model, const std::optional<EngineFailure>& failure,
               bool busy);

 private:
  void showText(const QString& text);

  QLabel* label_;
  QProgressBar* bar_;
  QToolButton* cancel_;
};

}  // namespace pinsel
