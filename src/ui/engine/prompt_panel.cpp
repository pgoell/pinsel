#include "ui/engine/prompt_panel.hpp"

#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QImage>
#include <QKeySequence>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSpinBox>
#include <QStringList>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <QtConcurrentRun>
#include <cstdint>
#include <limits>
#include <utility>

#include "canvas/canvas.hpp"
#include "inpaint/inpaint.hpp"

namespace pinsel {

namespace {

constexpr int kNameLength = 40;

QPlainTextEdit* textField(const QString& placeholder, int lines, QWidget* parent) {
  auto* field = new QPlainTextEdit(parent);
  field->setPlaceholderText(placeholder);
  field->setTabChangesFocus(true);
  field->setFixedHeight((field->fontMetrics().lineSpacing() * lines) + 12);
  return field;
}

struct Range {
  int min;
  int max;
  int value;
};

QSpinBox* numberField(Range range, QWidget* parent) {
  auto* field = new QSpinBox(parent);
  field->setRange(range.min, range.max);
  field->setValue(range.value);
  field->setAlignment(Qt::AlignRight);
  return field;
}

}  // namespace

struct PromptPanel::Pending {
  int document;
  QString name;
  QStringList files;
  std::function<void(bool added)> done;
  // Set once the crop is ready.
  InpaintInput input = {};
};

PromptPanel::PromptPanel(Canvas* canvas, Run run, Document document, QWidget* parent)
    : QWidget(parent),
      canvas_(canvas),
      run_(std::move(run)),
      document_(std::move(document)),
      prompt_(textField(QStringLiteral("What to draw in the mask"), 4, this)),
      negative_(textField(QStringLiteral("What to keep out"), 2, this)),
      seed_(numberField({.min = 0, .max = std::numeric_limits<int>::max(), .value = 0}, this)),
      random_seed_(new QCheckBox(QStringLiteral("Random"), this)),
      steps_(numberField({.min = 1, .max = 150, .value = 30}, this)),
      strength_(numberField({.min = 1, .max = 100, .value = 100}, this)),
      generate_(new QPushButton(QStringLiteral("Generate"), this)),
      note_(new QLabel(this)) {
  auto* layout = new QVBoxLayout(this);
  layout->addWidget(prompt_);
  layout->addWidget(negative_);
  auto* form = new QFormLayout;
  auto* seed_row = new QHBoxLayout;
  seed_row->addWidget(seed_, 1);
  seed_row->addWidget(random_seed_);
  form->addRow(QStringLiteral("Seed"), seed_row);
  form->addRow(QStringLiteral("Steps"), steps_);
  strength_->setSuffix(QStringLiteral(" %"));
  strength_->setToolTip(QStringLiteral("How far the masked part may move from the image"));
  form->addRow(QStringLiteral("Strength"), strength_);
  layout->addLayout(form);
  layout->addWidget(generate_);
  note_->setWordWrap(true);
  note_->hide();
  layout->addWidget(note_);
  layout->addStretch(1);
  random_seed_->setChecked(true);
  generate_->setToolTip(QStringLiteral("Redraw the masked part as a new layer (Ctrl+Return)"));
  generate_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return));
  connect(generate_, &QPushButton::clicked, this, [this] { generate(); });
}

PromptPanel::~PromptPanel() = default;

void PromptPanel::setPrompt(const QString& prompt) {
  prompt_->setPlainText(prompt);
}

void PromptPanel::say(const QString& text) {
  note_->setText(text);
  note_->setVisible(!text.isEmpty());
}

void PromptPanel::generate(const std::function<void(bool added)>& done) {
  if (pending_) {
    return;
  }
  if (!canvas_->tiles().hasMask()) {
    say(QStringLiteral("Paint a mask over the part to redraw."));
    return;
  }
  if (!files_) {
    files_ = std::make_unique<QTemporaryDir>();
  }
  if (random_seed_->isChecked()) {
    seed_->setValue(QRandomGenerator::global()->bounded(seed_->maximum()));
  }
  const QString stem = QDir(files_->path()).filePath(QString::number(++file_count_));
  const InpaintRequest request{
      .prompt = prompt_->toPlainText(),
      .negative_prompt = negative_->toPlainText(),
      .steps = steps_->value(),
      .seed = static_cast<std::uint32_t>(seed_->value()),
      .strength = strength_->value() / 100.0,
      .image = stem + QStringLiteral("-image.png"),
      .mask = stem + QStringLiteral("-mask.png"),
      .output = stem + QStringLiteral("-out.png"),
  };
  const QString prompt = request.prompt.simplified();
  pending_ = std::make_unique<Pending>(Pending{
      .document = document_(),
      .name = prompt.isEmpty() ? QStringLiteral("Inpaint")
                               : QStringLiteral("Inpaint: %1").arg(prompt.left(kNameLength)),
      .files = {request.image, request.mask, request.output},
      .done = done,
  });
  say({});
  generate_->setEnabled(false);
  generate_->setText(QStringLiteral("Generating..."));
  auto* watcher = new QFutureWatcher<std::optional<InpaintInput>>(this);
  connect(watcher, &QFutureWatcher<std::optional<InpaintInput>>::finished, this,
          [this, watcher, request] {
            std::optional<InpaintInput> input = watcher->result();
            watcher->deleteLater();
            if (!input) {
              say(QStringLiteral("The mask is empty, or the crop could not be written to %1.")
                      .arg(files_->path()));
              end(false);
              return;
            }
            pending_->input = std::move(*input);
            send(request);
          });
  watcher->setFuture(QtConcurrent::run([tiles = canvas_->tiles(), image = request.image,
                                        mask = request.mask]() -> std::optional<InpaintInput> {
    std::optional<InpaintInput> input = inpaintInput(tiles);
    if (!input || !input->image.save(image) || !input->mask.save(mask)) {
      return std::nullopt;
    }
    return input;
  }));
}

void PromptPanel::send(const InpaintRequest& request) {
  if (pending_->document != document_() ||
      !run_(request, [this](const std::optional<EngineFailure>& failure, const QString& output) {
        place(failure, output);
      })) {
    end(false);
  }
}

void PromptPanel::place(const std::optional<EngineFailure>& failure, const QString& output) {
  if (failure || pending_->document != document_()) {
    end(false);
    return;
  }
  auto* watcher = new QFutureWatcher<std::optional<TileStore::LayerContent>>(this);
  connect(watcher, &QFutureWatcher<std::optional<TileStore::LayerContent>>::finished, this,
          [this, watcher] {
            const std::optional<TileStore::LayerContent> layer = watcher->result();
            watcher->deleteLater();
            bool added = false;
            if (layer && pending_->document == document_()) {
              canvas_->edit([&](TileStore& tiles) { added = tiles.addLayer(*layer); });
            }
            if (!added) {
              say(QStringLiteral("Could not read the engine's result, or there is no memory."));
            }
            end(added);
          });
  watcher->setFuture(
      QtConcurrent::run([input = std::move(pending_->input), output, name = pending_->name] {
        return inpaintLayer(input, QImage(output), name);
      }));
}

void PromptPanel::end(bool added) {
  const std::unique_ptr<Pending> pending = std::move(pending_);
  generate_->setEnabled(true);
  generate_->setText(QStringLiteral("Generate"));
  for (const QString& file : std::as_const(pending->files)) {
    QFile::remove(file);
  }
  if (pending->done) {
    pending->done(added);
  }
}

}  // namespace pinsel
