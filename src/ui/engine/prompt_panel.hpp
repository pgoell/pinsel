#pragma once

#include <QString>
#include <QWidget>
#include <functional>
#include <memory>
#include <optional>

#include "engine/engine.hpp"

class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTemporaryDir;

namespace pinsel {

class Canvas;

// The prompt and its settings, and Generate, which redraws the masked part of
// the image and puts the result above the current layer as a layer of its own.
class PromptPanel : public QWidget {
 public:
  // output is the image the engine wrote.
  using Ended =
      std::function<void(const std::optional<EngineFailure>& failure, const QString& output)>;
  // Sends request to the engine and calls ended once it ends. Returns false,
  // and never calls ended, when no model is loaded.
  using Run = std::function<bool(const InpaintRequest& request, Ended ended)>;
  // Names the image on the canvas; it changes when another image replaces it.
  using Document = std::function<int()>;

  PromptPanel(Canvas* canvas, Run run, Document document, QWidget* parent = nullptr);
  ~PromptPanel() override;
  PromptPanel(const PromptPanel&) = delete;
  PromptPanel& operator=(const PromptPanel&) = delete;
  PromptPanel(PromptPanel&&) = delete;
  PromptPanel& operator=(PromptPanel&&) = delete;

  void setPrompt(const QString& prompt);
  // Calls done with whether a layer was added, false at once when nothing can
  // be sent.
  void generate(const std::function<void(bool added)>& done = {});

 private:
  void say(const QString& text);
  void send(const InpaintRequest& request);
  void place(const std::optional<EngineFailure>& failure, const QString& output);
  void end(bool added);

  Canvas* canvas_;
  Run run_;
  Document document_;
  QPlainTextEdit* prompt_;
  QPlainTextEdit* negative_;
  QSpinBox* seed_;
  QCheckBox* random_seed_;
  QSpinBox* steps_;
  QSpinBox* strength_;
  QPushButton* generate_;
  QLabel* note_;
  std::unique_ptr<QTemporaryDir> files_;
  int file_count_ = 0;
  struct Pending;
  std::unique_ptr<Pending> pending_;
};

}  // namespace pinsel
