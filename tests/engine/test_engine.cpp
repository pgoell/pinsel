#include <gtest/gtest.h>

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QLatin1StringView>
#include <QString>
#include <QStringList>
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "engine/engine.hpp"

namespace {

using pinsel::Engine;
using pinsel::EngineFailure;
using pinsel::EngineProgress;
using pinsel::GenerateRequest;
using Kind = EngineFailure::Kind;

void ensureApp() {
  static int argc = 1;
  static std::array<char, 6> name{"tests"};
  static std::array<char*, 2> argv{name.data(), nullptr};
  // Never deleted: Qt's own statics may outlive any owner the test could give it.
  static const QCoreApplication* const app =
      QCoreApplication::instance() != nullptr ? nullptr : new QApplication(argc, argv.data());
  std::ignore = app;
}

struct Ended {
  int job;
  std::optional<EngineFailure> failure;
  QString output;

  [[nodiscard]] std::optional<Kind> kind() const {
    return failure ? std::optional(failure->kind) : std::nullopt;
  }
  [[nodiscard]] std::string message() const {
    return failure ? failure->message.toStdString() : std::string();
  }
};

GenerateRequest request(const QString& prompt, int steps = 3) {
  return {
      .prompt = prompt,
      .negative_prompt = {},
      .steps = steps,
      .seed = 1,
      .size = {64, 64},
      .output = QStringLiteral("out.png"),
  };
}

// An engine on the fake sidecar that records what it reports.
struct Harness {
  explicit Harness(const QStringList& command = {QStringLiteral(PINSEL_FAKE_ENGINE)})
      : engine((ensureApp(), command), events()) {}

  pinsel::EngineEvents events() {
    return {
        .progress =
            [this](int job, const EngineProgress& each) {
              progress.emplace_back(job, each);
              if (on_progress) {
                on_progress(job);
              }
            },
        .finished =
            [this](int job, const QString& output) {
              ended.push_back({.job = job, .failure = {}, .output = output});
            },
        .failed =
            [this](int job, const EngineFailure& failure) {
              ended.push_back({.job = job, .failure = failure, .output = {}});
            },
    };
  }

  // Runs the event loop until job ends, and returns how.
  Ended waitFor(int job) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 20000) {
      for (const Ended& each : ended) {
        if (each.job == job) {
          return each;
        }
      }
      QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents, 50);
    }
    ADD_FAILURE() << "job " << job << " never ended";
    return {.job = job, .failure = {}, .output = {}};
  }

  // The progress job reported for stage, as each report's count.
  [[nodiscard]] std::vector<int> counts(int job, QLatin1StringView stage) const {
    std::vector<int> found;
    for (const auto& [owner, each] : progress) {
      if (owner == job && each.stage == stage) {
        found.push_back(each.done);
      }
    }
    return found;
  }

  std::vector<std::pair<int, EngineProgress>> progress;
  std::vector<Ended> ended;
  std::function<void(int job)> on_progress;
  Engine engine;
};

constexpr QLatin1StringView kLoad("load");
constexpr QLatin1StringView kGenerate("generate");
constexpr QLatin1StringView kModel("model.safetensors");

TEST(Engine, ReportsLoadProgressThenFinishes) {
  Harness h;
  const int job = h.engine.load(kModel);
  EXPECT_EQ(h.waitFor(job).kind(), std::nullopt);
  EXPECT_EQ(h.counts(job, kLoad), (std::vector<int>{1, 2}));
}

TEST(Engine, ReportsEachStepAndTheOutput) {
  Harness h;
  h.engine.load(kModel);
  const int job = h.engine.generate(request(QStringLiteral("a fox"), 4));
  const Ended ended = h.waitFor(job);
  EXPECT_EQ(ended.message(), "");
  EXPECT_EQ(ended.output, QStringLiteral("out.png"));
  EXPECT_EQ(h.counts(job, kGenerate), (std::vector<int>{1, 2, 3, 4}));
  EXPECT_FALSE(h.engine.busy());
}

TEST(Engine, CancelEndsTheRunningJob) {
  Harness h;
  h.engine.load(kModel);
  const int job = h.engine.generate(request(QStringLiteral("a fox"), 100000));
  h.on_progress = [&h, job](int progressed) {
    if (progressed == job) {
      h.engine.cancel(job);
    }
  };
  EXPECT_EQ(h.waitFor(job).kind(), Kind::Cancelled);
}

TEST(Engine, OutOfMemoryEndsOnlyThatJob) {
  Harness h;
  const int load = h.engine.load(kModel);
  EXPECT_EQ(h.waitFor(h.engine.generate(request(QStringLiteral("oom")))).kind(), Kind::OutOfMemory);
  const int job = h.engine.generate(request(QStringLiteral("a fox")));
  EXPECT_EQ(h.waitFor(job).message(), "");
  // The model stayed loaded, so nothing loaded it again.
  EXPECT_EQ(h.counts(load, kLoad).size(), 2U);
  EXPECT_TRUE(h.counts(job, kLoad).empty());
}

TEST(Engine, ACrashFailsTheJobAndTheNextJobLoadsTheModelAgain) {
  Harness h;
  h.engine.load(kModel);
  const Ended crashed = h.waitFor(h.engine.generate(request(QStringLiteral("crash"))));
  EXPECT_EQ(crashed.kind(), Kind::Stopped);
  EXPECT_NE(crashed.message().find("the fake engine crashed"), std::string::npos)
      << crashed.message();

  const int job = h.engine.generate(request(QStringLiteral("a fox")));
  EXPECT_EQ(h.waitFor(job).message(), "");
  EXPECT_EQ(h.counts(job, kLoad), (std::vector<int>{1, 2}));
}

TEST(Engine, AModelThatCrashesTheEngineIsNotLoadedAgain) {
  Harness h;
  EXPECT_EQ(h.waitFor(h.engine.load(QStringLiteral("crash"))).kind(), Kind::Stopped);
  EXPECT_EQ(h.waitFor(h.engine.generate(request(QStringLiteral("a fox")))).message(),
            "RuntimeError: No model loaded");
}

TEST(Engine, AFailedLoadLeavesNoModel) {
  Harness h;
  h.engine.load(kModel);
  EXPECT_EQ(h.waitFor(h.engine.load(QStringLiteral("missing"))).kind(), Kind::Failed);
  EXPECT_EQ(h.waitFor(h.engine.generate(request(QStringLiteral("a fox")))).message(),
            "RuntimeError: No model loaded");
}

TEST(Engine, AProgramThatCannotStartFailsTheJob) {
  Harness h({QStringLiteral("pinsel-no-such-engine")});
  const Ended ended = h.waitFor(h.engine.load(kModel));
  EXPECT_EQ(ended.kind(), Kind::Stopped);
  EXPECT_EQ(ended.message().rfind("Could not start", 0), 0U) << ended.message();
  EXPECT_FALSE(h.engine.busy());
}

}  // namespace
