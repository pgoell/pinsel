// Speaks the engine protocol as the sidecar does, with no model behind it, so
// the tests run without Python. A model named "missing" fails to load and one
// named "crash" ends the process as it loads; a prompt of "oom" runs out of
// memory and one of "crash" ends the process after the first step.
#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <functional>
#include <iostream>
#include <mutex>
#include <set>
#include <string>
#include <thread>

namespace {

struct Inbox {
  std::mutex mutex;
  std::condition_variable arrived;
  std::deque<QJsonObject> queue;
  std::set<int> cancelled;
  bool closed = false;

  bool isCancelled(int job) {
    const std::scoped_lock lock(mutex);
    return cancelled.contains(job);
  }
};

void send(const QJsonObject& event) {
  const QByteArray line = QJsonDocument(event).toJson(QJsonDocument::Compact) + '\n';
  std::fwrite(line.constData(), 1, static_cast<std::size_t>(line.size()), stdout);
  std::fflush(stdout);
}

void readRequests(Inbox& inbox) {
  std::string line;
  while (std::getline(std::cin, line)) {
    const QJsonObject request = QJsonDocument::fromJson(QByteArray::fromStdString(line)).object();
    const std::scoped_lock lock(inbox.mutex);
    if (request.value("op").toString() == QLatin1String("cancel")) {
      inbox.cancelled.insert(request.value("job").toInt());
    } else {
      inbox.queue.push_back(request);
    }
    inbox.arrived.notify_one();
  }
  const std::scoped_lock lock(inbox.mutex);
  inbox.closed = true;
  inbox.arrived.notify_one();
}

void fail(int job, const char* kind, const QString& message) {
  send({{"event", "error"}, {"job", job}, {"kind", kind}, {"message", message}});
}

// Returns false when the job was cancelled first.
bool runSteps(Inbox& inbox, int job, const QString& stage, int total, bool crash) {
  for (int done = 1; done <= total; ++done) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if (inbox.isCancelled(job)) {
      fail(job, "cancelled", QStringLiteral("Cancelled"));
      return false;
    }
    send({{"event", "progress"}, {"job", job}, {"stage", stage}, {"done", done}, {"total", total}});
    if (crash) {
      std::fputs("RuntimeError: the fake engine crashed\n", stderr);
      std::fflush(stderr);
      std::_Exit(3);
    }
  }
  return true;
}

}  // namespace

int main() {
  Inbox inbox;
  // Ends with stdin, before main returns.
  std::thread(readRequests, std::ref(inbox)).detach();
  send({{"event", "ready"}, {"engine", "fake"}});
  QString model;
  while (true) {
    QJsonObject request;
    {
      std::unique_lock lock(inbox.mutex);
      inbox.arrived.wait(lock, [&] { return inbox.closed || !inbox.queue.empty(); });
      if (inbox.queue.empty()) {
        return 0;
      }
      request = inbox.queue.front();
      inbox.queue.pop_front();
    }
    const int job = request.value("job").toInt();
    const QString op = request.value("op").toString();
    if (op == QLatin1String("load")) {
      model.clear();
      const QString path = request.value("model").toString();
      if (path == QLatin1String("missing")) {
        fail(job, "failed", QStringLiteral("FileNotFoundError: missing"));
      } else if (runSteps(inbox, job, QStringLiteral("load"), 2, path == QLatin1String("crash"))) {
        model = path;
        send({{"event", "done"}, {"job", job}});
      }
    } else if (model.isEmpty()) {
      fail(job, "failed", QStringLiteral("RuntimeError: No model loaded"));
    } else if (request.value("prompt").toString() == QLatin1String("oom")) {
      fail(job, "oom", QStringLiteral("CUDA out of memory"));
    } else if (runSteps(inbox, job, QStringLiteral("generate"), request.value("steps").toInt(),
                        request.value("prompt").toString() == QLatin1String("crash"))) {
      send({{"event", "done"}, {"job", job}, {"output", request.value("output")}});
    }
  }
}
