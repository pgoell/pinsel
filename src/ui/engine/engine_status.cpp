#include "ui/engine/engine_status.hpp"

#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QToolButton>
#include <utility>

namespace pinsel {

namespace {

constexpr int kTextWidth = 900;

QString failureText(const QString& model, const EngineFailure& failure) {
  switch (failure.kind) {
    case EngineFailure::Kind::Cancelled:
      return QStringLiteral("Load cancelled. No model loaded.");
    case EngineFailure::Kind::OutOfMemory:
      return QStringLiteral("Out of GPU memory loading %1: %2").arg(model, failure.message);
    case EngineFailure::Kind::Failed:
      return QStringLiteral("Could not load %1: %2").arg(model, failure.message);
    case EngineFailure::Kind::Stopped:
      return QStringLiteral("%1. It starts again with the next job.").arg(failure.message);
  }
  return failure.message;
}

}  // namespace

EngineStatus::EngineStatus(QWidget* parent)
    : QWidget(parent),
      label_(new QLabel(this)),
      bar_(new QProgressBar(this)),
      cancel_(new QToolButton(this)) {
  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(label_);
  layout->addWidget(bar_);
  layout->addWidget(cancel_);
  bar_->setFixedWidth(160);
  bar_->setTextVisible(false);
  bar_->hide();
  cancel_->setText(QStringLiteral("Cancel"));
  cancel_->setAutoRaise(true);
  cancel_->hide();
  connect(cancel_, &QToolButton::clicked, this, &EngineStatus::cancel);
  engine_ = std::make_unique<Engine>(
      Engine::defaultCommand(),
      EngineEvents{
          .progress =
              [this](int job, const EngineProgress& progress) {
                if (pending_.contains(job)) {
                  bar_->setRange(0, progress.total);
                  bar_->setValue(progress.done);
                }
              },
          .finished = [this](int job, const QString& /*output*/) { end(job, std::nullopt); },
          .failed = [this](int job, const EngineFailure& failure) { end(job, failure); },
      });
  showText(QStringLiteral("No model loaded"));
}

void EngineStatus::loadModel(const QString& path,
                             std::function<void(const std::optional<EngineFailure>&)> ended) {
  const QString model = QFileInfo(path).fileName();
  pending_[engine_->load(path)] = {.model = model, .ended = std::move(ended)};
  showText(QStringLiteral("Loading %1").arg(model));
  bar_->setRange(0, 0);
  bar_->show();
  cancel_->show();
}

void EngineStatus::cancel() {
  for (const auto& [job, pending] : pending_) {
    engine_->cancel(job);
  }
}

void EngineStatus::showText(const QString& text) {
  label_->setText(label_->fontMetrics().elidedText(text, Qt::ElideMiddle, kTextWidth));
  label_->setToolTip(text);
}

void EngineStatus::end(int job, const std::optional<EngineFailure>& failure) {
  auto node = pending_.extract(job);
  if (node.empty()) {
    return;
  }
  Pending ended = std::move(node.mapped());
  showText(failure ? failureText(ended.model, *failure)
                   : QStringLiteral("Model: %1").arg(ended.model));
  bar_->setVisible(!pending_.empty());
  cancel_->setVisible(!pending_.empty());
  if (ended.ended) {
    ended.ended(failure);
  }
}

}  // namespace pinsel
