#include "ui/layers/layers_panel.hpp"

#include <QAbstractItemModel>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QListWidget>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>
#include <utility>

#include "canvas/canvas.hpp"
#include "tiles/tile_store.hpp"

namespace pinsel {

namespace {

QToolButton* addButton(QHBoxLayout* row, QLatin1StringView icon, const QString& tip) {
  auto* button = new QToolButton(row->parentWidget());
  button->setIcon(QIcon(QStringLiteral(":/icons/%1.svg").arg(icon)));
  button->setToolTip(tip);
  button->setAutoRaise(true);
  row->addWidget(button);
  return button;
}

}  // namespace

LayersPanel::LayersPanel(Canvas* canvas, QWidget* parent)
    : QWidget(parent),
      canvas_(canvas),
      mode_(new QComboBox(this)),
      opacity_(new QSlider(Qt::Horizontal, this)),
      opacity_field_(new QSpinBox(this)),
      list_(new QListWidget(this)) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  addBlendControls(layout);
  list_->setDragDropMode(QAbstractItemView::InternalMove);
  list_->setSelectionMode(QAbstractItemView::SingleSelection);
  layout->addWidget(list_, 1);
  auto* buttons = new QHBoxLayout();
  layout->addLayout(buttons);
  // The layout has its parent only once added, and the buttons need it.
  add_ = addButton(buttons, QLatin1StringView("plus"), QStringLiteral("New Layer"));
  remove_ = addButton(buttons, QLatin1StringView("trash-2"), QStringLiteral("Delete Layer"));
  buttons->addStretch(1);

  connect(add_, &QToolButton::clicked, this,
          [this] { canvas_->edit([](TileStore& tiles) { tiles.addLayer(); }); });
  connect(remove_, &QToolButton::clicked, this,
          [this] { canvas_->edit([](TileStore& tiles) { tiles.removeLayer(); }); });
  connect(list_, &QListWidget::currentRowChanged, this, [this](int row) {
    if (refreshing_ || row < 0) {
      return;
    }
    const std::size_t layer = layerAt(row);
    canvas_->edit([layer](TileStore& tiles) { tiles.setCurrentLayer(layer); });
  });
  connect(list_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
    if (refreshing_) {
      return;
    }
    const std::size_t layer = layerAt(list_->row(item));
    const bool visible = item->checkState() == Qt::Checked;
    canvas_->edit([layer, visible](TileStore& tiles) { tiles.setLayerVisible(layer, visible); });
  });
  // A drop in the list moves the row itself; the store follows it.
  connect(list_->model(), &QAbstractItemModel::rowsMoved, this,
          [this](const QModelIndex&, int start, int, const QModelIndex&, int row) {
            if (refreshing_) {
              return;
            }
            const std::size_t from = layerAt(start);
            const std::size_t to = layerAt(row > start ? row - 1 : row);
            canvas_->edit([from, to](TileStore& tiles) { tiles.moveLayer(from, to); });
          });
  refresh();
}

void LayersPanel::addBlendControls(QVBoxLayout* layout) {
  // A widget of its own gives the form the style's margins, as in the Brush panel.
  auto* controls = new QWidget(this);
  auto* form = new QFormLayout(controls);
  layout->addWidget(controls);
  for (const auto& [name, mode] : {
           std::pair{"Normal", TileStore::BlendMode::Normal},
           {"Multiply", TileStore::BlendMode::Multiply},
           {"Screen", TileStore::BlendMode::Screen},
           {"Overlay", TileStore::BlendMode::Overlay},
       }) {
    mode_->addItem(QString::fromLatin1(name), static_cast<int>(mode));
  }
  form->addRow(QStringLiteral("Mode"), mode_);
  auto* row = new QHBoxLayout();
  opacity_->setRange(0, 100);
  opacity_field_->setRange(0, 100);
  opacity_field_->setSuffix(QStringLiteral(" %"));
  opacity_field_->setButtonSymbols(QAbstractSpinBox::NoButtons);
  opacity_field_->setAlignment(Qt::AlignRight);
  opacity_field_->setFixedWidth(64);
  opacity_field_->setKeyboardTracking(false);
  row->addWidget(opacity_, 1);
  row->addWidget(opacity_field_);
  form->addRow(QStringLiteral("Opacity"), row);

  connect(mode_, &QComboBox::currentIndexChanged, this, [this] {
    if (refreshing_) {
      return;
    }
    const auto mode = static_cast<TileStore::BlendMode>(mode_->currentData().toInt());
    canvas_->edit(
        [mode](TileStore& tiles) { tiles.setLayerBlendMode(tiles.currentLayer(), mode); });
  });
  connect(opacity_, &QSlider::valueChanged, opacity_field_, &QSpinBox::setValue);
  connect(opacity_field_, &QSpinBox::valueChanged, opacity_, &QSlider::setValue);
  connect(opacity_, &QSlider::sliderPressed, this, [this] { joining_ = false; });
  connect(opacity_, &QSlider::valueChanged, this, [this](int value) {
    if (refreshing_) {
      return;
    }
    const bool join = joining_ && opacity_->isSliderDown();
    canvas_->edit([value, join](TileStore& tiles) {
      tiles.setLayerOpacity(tiles.currentLayer(), value / 100.0, join);
    });
    joining_ = opacity_->isSliderDown();
  });
}

std::size_t LayersPanel::layerAt(int row) const {
  return static_cast<std::size_t>(list_->count() - 1 - row);
}

int LayersPanel::rowOf(std::size_t layer) const {
  return list_->count() - 1 - static_cast<int>(layer);
}

void LayersPanel::refresh() {
  const bool was_refreshing = std::exchange(refreshing_, true);
  const TileStore& tiles = canvas_->tiles();
  const auto count = static_cast<int>(tiles.layerCount());
  while (list_->count() > count) {
    delete list_->takeItem(list_->count() - 1);
  }
  while (list_->count() < count) {
    auto* item = new QListWidgetItem(list_);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable |
                   Qt::ItemIsDragEnabled);
  }
  for (std::size_t layer = 0; layer < tiles.layerCount(); ++layer) {
    QListWidgetItem* item = list_->item(rowOf(layer));
    item->setText(tiles.layerName(layer));
    item->setCheckState(tiles.layerVisible(layer) ? Qt::Checked : Qt::Unchecked);
  }
  if (count > 0) {
    const std::size_t current = tiles.currentLayer();
    list_->setCurrentRow(rowOf(current));
    mode_->setCurrentIndex(mode_->findData(static_cast<int>(tiles.layerBlendMode(current))));
    opacity_->setValue(static_cast<int>(std::lround(tiles.layerOpacity(current) * 100.0)));
  }
  mode_->setEnabled(count > 0);
  opacity_->setEnabled(count > 0);
  opacity_field_->setEnabled(count > 0);
  add_->setEnabled(count > 0);
  remove_->setEnabled(count > 1);
  refreshing_ = was_refreshing;
}

}  // namespace pinsel
