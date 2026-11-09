#include "engine/engine.hpp"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QProcess>
#include <QTimer>
#include <QtGlobal>
#include <utility>
#include <vector>

namespace pinsel {

namespace {

constexpr qsizetype kErrorTail = 4096;

QByteArray lineOf(const QJsonObject& request) {
  return QJsonDocument(request).toJson(QJsonDocument::Compact);
}

EngineFailure::Kind kindOf(const QString& kind) {
  if (kind == QLatin1String("cancelled")) {
    return EngineFailure::Kind::Cancelled;
  }
  if (kind == QLatin1String("oom")) {
    return EngineFailure::Kind::OutOfMemory;
  }
  return EngineFailure::Kind::Failed;
}

// The last line the process wrote to stderr, which for Python names the
// exception that ended it.
QString lastLine(const QByteArray& errors) {
  const QList<QByteArray> lines = errors.trimmed().split('\n');
  return QString::fromUtf8(lines.constLast()).trimmed();
}

}  // namespace

Engine::Engine(QStringList command, EngineEvents events)
    : command_(std::move(command)), events_(std::move(events)) {}

Engine::~Engine() {
  if (!process_) {
    return;
  }
  process_->disconnect();
  // The engine holds nothing worth a wait, and torch can take seconds to exit.
  // A child that uv started outlives the kill on Linux: idle, it ends when its
  // stdin closes; busy, at its next progress write.
  process_->closeWriteChannel();
  process_->kill();
  process_->waitForFinished(1000);
}

QStringList Engine::defaultCommand() {
  const QString custom = qEnvironmentVariable("PINSEL_ENGINE");
  if (!custom.isEmpty()) {
    return QProcess::splitCommand(custom);
  }
  return {
      QStringLiteral("uv"),
      QStringLiteral("run"),
      QStringLiteral("--project"),
      QStringLiteral(PINSEL_SIDECAR_DIR),
      QStringLiteral("--locked"),
      QStringLiteral("--no-dev"),
      QStringLiteral("--extra"),
      QStringLiteral("diffusers"),
      QStringLiteral("pinsel-sidecar"),
  };
}

int Engine::load(const QString& model) {
  const int job = ++next_job_;
  open_.insert(job);
  start();
  owner_[job] = job;
  loads_[job] = model;
  model_ = model;
  process_has_model_ = true;
  send(lineOf({{"op", "load"}, {"job", job}, {"model", model}}));
  return job;
}

int Engine::generate(const GenerateRequest& request) {
  const int job = ++next_job_;
  open_.insert(job);
  start();
  if (!process_has_model_ && !model_.isEmpty()) {
    const int reload = ++next_job_;
    owner_[reload] = job;
    loads_[reload] = model_;
    process_has_model_ = true;
    send(lineOf({{"op", "load"}, {"job", reload}, {"model", model_}}));
  }
  owner_[job] = job;
  send(lineOf({
      {"op", "generate"},
      {"job", job},
      {"prompt", request.prompt},
      {"negative_prompt", request.negative_prompt},
      {"steps", request.steps},
      {"seed", static_cast<qint64>(request.seed)},
      {"width", request.size.width()},
      {"height", request.size.height()},
      {"output", request.output},
  }));
  return job;
}

void Engine::cancel(int job) {
  if (!open_.contains(job)) {
    return;
  }
  std::vector<int> wire_jobs;
  for (const auto& [wire, owner] : owner_) {
    if (owner == job) {
      wire_jobs.push_back(wire);
    }
  }
  for (const int wire : wire_jobs) {
    send(lineOf({{"op", "cancel"}, {"job", wire}}));
  }
}

void Engine::start() {
  if (process_) {
    return;
  }
  process_ = std::make_unique<QProcess>();
  process_has_model_ = false;
  events_buffer_.clear();
  errors_.clear();
  QProcess* process = process_.get();
  QObject::connect(process, &QProcess::readyReadStandardOutput, process, [this] { readEvents(); });
  QObject::connect(process, &QProcess::readyReadStandardError, process, [this, process] {
    errors_ = (errors_ + process->readAllStandardError()).right(kErrorTail);
  });
  QObject::connect(process, &QProcess::finished, process,
                   [this, process](int code, QProcess::ExitStatus status) {
                     errors_ = (errors_ + process->readAllStandardError()).right(kErrorTail);
                     QString message =
                         status == QProcess::CrashExit
                             ? QStringLiteral("The engine crashed")
                             : QStringLiteral("The engine quit with code %1").arg(code);
                     const QString cause = lastLine(errors_);
                     if (!cause.isEmpty()) {
                       message += QStringLiteral(": ") + cause;
                     }
                     stopped(message);
                   });
  QObject::connect(process, &QProcess::errorOccurred, process,
                   [this, process](QProcess::ProcessError error) {
                     if (error != QProcess::FailedToStart) {
                       return;
                     }
                     // Qt may report this inside start(), before load or
                     // generate has returned the job's id to its caller.
                     QTimer::singleShot(0, process, [this, process] {
                       stopped(QStringLiteral("Could not start %1: %2")
                                   .arg(command_.value(0), process->errorString()));
                     });
                   });
  process->start(command_.value(0), command_.mid(1));
}

void Engine::send(const QByteArray& line) {
  if (process_) {
    process_->write(line + '\n');
  }
}

void Engine::readEvents() {
  events_buffer_ += process_->readAllStandardOutput();
  const qsizetype end = events_buffer_.lastIndexOf('\n');
  if (end >= 0) {
    const QByteArray lines = events_buffer_.left(end);
    events_buffer_.remove(0, end + 1);
    handleLines(lines);
  }
}

void Engine::handleLines(const QByteArray& lines) {
  for (const QByteArray& line : lines.split('\n')) {
    handle(line);
  }
}

void Engine::handle(const QByteArray& line) {
  const QJsonObject event = QJsonDocument::fromJson(line).object();
  const QString kind = event.value(QLatin1String("event")).toString();
  const auto owner = owner_.find(event.value(QLatin1String("job")).toInt(-1));
  if (owner == owner_.end()) {
    return;
  }
  const int wire = owner->first;
  const int job = owner->second;
  const bool open = open_.contains(job);
  if (kind == QLatin1String("progress")) {
    if (open && events_.progress) {
      events_.progress(job, {
                                .stage = event.value(QLatin1String("stage")).toString(),
                                .done = event.value(QLatin1String("done")).toInt(),
                                .total = event.value(QLatin1String("total")).toInt(),
                            });
    }
    return;
  }
  const auto load = loads_.find(wire);
  const bool is_load = load != loads_.end();
  if (kind == QLatin1String("error") && is_load && load->second == model_) {
    // The sidecar lets go of its old model before it loads the next.
    model_.clear();
    process_has_model_ = false;
  }
  end(wire);
  if (!open) {
    return;
  }
  if (kind == QLatin1String("done") && wire == job) {
    open_.erase(job);
    if (events_.finished) {
      events_.finished(job, event.value(QLatin1String("output")).toString());
    }
  } else if (kind == QLatin1String("error")) {
    fail(job, {
                  .kind = kindOf(event.value(QLatin1String("kind")).toString()),
                  .message = event.value(QLatin1String("message")).toString(),
              });
  }
}

void Engine::end(int wire_job) {
  owner_.erase(wire_job);
  loads_.erase(wire_job);
}

void Engine::fail(int job, const EngineFailure& failure) {
  open_.erase(job);
  if (events_.failed) {
    events_.failed(job, failure);
  }
}

void Engine::stopped(const QString& message) {
  if (!process_) {
    return;
  }
  // The callbacks below may start the next process, so this one leaves first.
  QProcess* old = process_.release();
  old->disconnect();
  old->deleteLater();
  const QByteArray rest = std::exchange(events_buffer_, {}) + old->readAllStandardOutput();
  // A model that crashed the engine as it loaded would crash the next one too.
  if (!loads_.empty()) {
    model_.clear();
  }
  process_has_model_ = false;
  const int last = next_job_;
  handleLines(rest);
  std::erase_if(owner_, [last](const auto& entry) { return entry.first <= last; });
  std::erase_if(loads_, [last](const auto& entry) { return entry.first <= last; });
  const std::set<int> ended(open_.begin(), open_.upper_bound(last));
  for (const int job : ended) {
    if (open_.contains(job)) {
      fail(job, {.kind = EngineFailure::Kind::Stopped, .message = message});
    }
  }
}

}  // namespace pinsel
