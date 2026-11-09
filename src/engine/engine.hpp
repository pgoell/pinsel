#pragma once

#include <QByteArray>
#include <QSize>
#include <QString>
#include <QStringList>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>

class QJsonObject;
class QProcess;

namespace pinsel {

struct GenerateRequest {
  QString prompt;
  QString negative_prompt;
  int steps = 30;
  std::uint32_t seed = 0;
  QSize size{1024, 1024};
  // Where the engine writes the image, as a PNG.
  QString output;
};

// Redraws the white part of mask in image. Both are PNGs of the same size,
// which the output keeps.
struct InpaintRequest {
  QString prompt;
  QString negative_prompt;
  int steps = 30;
  std::uint32_t seed = 0;
  // How far from the image the masked part may go: 1 starts it from noise.
  double strength = 1.0;
  QString image;
  QString mask;
  QString output;
};

struct EngineFailure {
  enum class Kind : std::uint8_t {
    Cancelled,
    // The engine freed its memory and keeps running; the model stays loaded.
    OutOfMemory,
    Failed,
    // The process ended or never started. The next job starts it again.
    Stopped,
  };
  Kind kind;
  QString message;
};

// The engine's GPU, in MiB, as it reported at the end of a job.
struct EngineMemory {
  double free_mb;
  double total_mb;
  // The most the engine held at once during the job.
  double peak_mb;
};

struct EngineProgress {
  // "load" or "generate".
  QString stage;
  int done;
  int total;
};

// Each runs on the thread that made the Engine, from its event loop.
struct EngineEvents {
  std::function<void(int job, const EngineProgress& progress)> progress;
  // output is the image a generate job wrote, and empty for a load.
  std::function<void(int job, const QString& output)> finished;
  std::function<void(int job, const EngineFailure& failure)> failed;
};

// Runs models in a separate process, which pinsel talks to over the engine
// protocol (sidecar/README.md). Jobs run one at a time, in order. A crash or
// a lack of memory there ends only the job. A job after a crash starts the
// process again and loads the last model before it runs.
class Engine {
 public:
  // command is the program and its arguments.
  Engine(QStringList command, EngineEvents events);
  ~Engine();
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;
  Engine(Engine&&) = delete;
  Engine& operator=(Engine&&) = delete;

  // The command in PINSEL_ENGINE, split as a shell would, or else the Python
  // sidecar in this source tree, through uv.
  static QStringList defaultCommand();

  // Each returns the new job's id.
  int load(const QString& model);
  int generate(const GenerateRequest& request);
  int inpaint(const InpaintRequest& request);
  // Does nothing for a job that has ended.
  void cancel(int job);
  // The model the queued jobs will run on, or empty when a load failed or none
  // was asked for.
  [[nodiscard]] const QString& model() const {
    return model_;
  }
  [[nodiscard]] bool busy() const {
    return !open_.empty();
  }
  // From the last job that finished, if its engine reports memory.
  [[nodiscard]] const std::optional<EngineMemory>& memory() const {
    return memory_;
  }

 private:
  // Sends a job that needs the model, loading it first after a restart.
  int submit(QJsonObject request);
  void start();
  void send(const QByteArray& line);
  void readEvents();
  void handleLines(const QByteArray& lines);
  void handle(const QByteArray& line);
  void readMemory(const QJsonObject& gpu);
  void end(int wire_job);
  void fail(int job, const EngineFailure& failure);
  void stopped(const QString& message);

  QStringList command_;
  EngineEvents events_;
  std::unique_ptr<QProcess> process_;
  QByteArray events_buffer_;
  // The end of the process's stderr, for the message when it stops.
  QByteArray errors_;
  int next_job_ = 0;
  // Jobs that have neither finished nor failed.
  std::set<int> open_;
  // Each request sent, by its own id, to the job it serves. A reload after a
  // crash serves the job that caused it.
  std::map<int, int> owner_;
  std::map<int, QString> loads_;
  // The model the queued jobs leave loaded, and whether this process will hold
  // it. A job after a restart loads it again.
  QString model_;
  bool process_has_model_ = false;
  std::optional<EngineMemory> memory_;
};

}  // namespace pinsel
