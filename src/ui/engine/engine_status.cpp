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

EngineStatus::EngineStatus(std::function<void()> cancel, QWidget* parent)
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
  connect(cancel_, &QToolButton::clicked, this, std::move(cancel));
  showText(QStringLiteral("No model loaded"));
}

void EngineStatus::showLoad(int job, const QString& path) {
  const QString model = QFileInfo(path).fileName();
  loads_[job] = model;
  showText(QStringLiteral("Loading %1").arg(model));
  bar_->setRange(0, 0);
  bar_->show();
  cancel_->show();
}

void EngineStatus::showProgress(int job, const EngineProgress& progress) {
  if (loads_.contains(job)) {
    bar_->setRange(0, progress.total);
    bar_->setValue(progress.done);
  }
}

void EngineStatus::showEnd(int job, const std::optional<EngineFailure>& failure) {
  auto node = loads_.extract(job);
  if (node.empty()) {
    return;
  }
  const QString& model = node.mapped();
  showText(failure ? failureText(model, *failure) : QStringLiteral("Model: %1").arg(model));
  bar_->setVisible(!loads_.empty());
  cancel_->setVisible(!loads_.empty());
}

void EngineStatus::showText(const QString& text) {
  label_->setText(label_->fontMetrics().elidedText(text, Qt::ElideMiddle, kTextWidth));
  label_->setToolTip(text);
}

}  // namespace pinsel
