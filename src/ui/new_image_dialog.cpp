#include "ui/new_image_dialog.hpp"

#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QIcon>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>

namespace pinsel {

namespace {

constexpr int kDefaultSide = 1024;
// A square image this wide takes 1 GiB as RGBA8.
constexpr int kMaxSide = 16384;

QSpinBox* sideField(QWidget* parent) {
  auto* field = new QSpinBox(parent);
  field->setRange(1, kMaxSide);
  field->setValue(kDefaultSide);
  field->setSuffix(QStringLiteral(" px"));
  return field;
}

}  // namespace

std::optional<NewImage> askNewImage(QWidget* parent) {
  QDialog dialog(parent);
  dialog.setWindowTitle(QStringLiteral("New Image"));
  auto* form = new QFormLayout(&dialog);
  QSpinBox* width = sideField(&dialog);
  QSpinBox* height = sideField(&dialog);
  form->addRow(QStringLiteral("Width"), width);
  form->addRow(QStringLiteral("Height"), height);

  QColor background = Qt::white;
  auto* color = new QPushButton(&dialog);
  const auto showColor = [color](const QColor& value) {
    QPixmap swatch(16, 16);
    swatch.fill(value);
    color->setIcon(QIcon(swatch));
    color->setText(value.alpha() == 0 ? QStringLiteral("Transparent") : value.name());
  };
  showColor(background);
  QObject::connect(color, &QPushButton::clicked, &dialog, [&] {
    const QColor chosen = QColorDialog::getColor(background, &dialog, QStringLiteral("Background"),
                                                 QColorDialog::ShowAlphaChannel);
    if (chosen.isValid()) {
      background = chosen;
      showColor(chosen);
    }
  });
  form->addRow(QStringLiteral("Background"), color);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  form->addRow(buttons);

  if (dialog.exec() != QDialog::Accepted) {
    return std::nullopt;
  }
  return NewImage{.size = QSize(width->value(), height->value()), .background = background};
}

}  // namespace pinsel
