#include "writing_assistant_view.h"

#include <ui/design_system/design_system.h>

#include <QLabel>
#include <QVBoxLayout>


namespace Ui {

class WritingAssistantView::Implementation
{
public:
    explicit Implementation(QWidget* _parent);

    QLabel* titleLabel = nullptr;
    QLabel* statusLabel = nullptr;
};

WritingAssistantView::Implementation::Implementation(QWidget* _parent)
    : titleLabel(new QLabel(_parent))
    , statusLabel(new QLabel(_parent))
{
    titleLabel->setText(QStringLiteral("Asistente de escritura"));
    titleLabel->setAlignment(Qt::AlignCenter);

    statusLabel->setText(QStringLiteral("Plugin cargado correctamente.\nIteración 1 — esqueleto sin conexión a Claude todavía."));
    statusLabel->setAlignment(Qt::AlignCenter);
    statusLabel->setWordWrap(true);
}


// ****


WritingAssistantView::WritingAssistantView(QWidget* _parent)
    : Widget(_parent)
    , d(new Implementation(this))
{
    auto layout = new QVBoxLayout;
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(16);
    layout->addStretch();
    layout->addWidget(d->titleLabel);
    layout->addWidget(d->statusLabel);
    layout->addStretch();
    setLayout(layout);
}

WritingAssistantView::~WritingAssistantView() = default;

QWidget* WritingAssistantView::asQWidget()
{
    return this;
}

void WritingAssistantView::setEditingMode(ManagementLayer::DocumentEditingMode _mode)
{
    Q_UNUSED(_mode)
}

void WritingAssistantView::updateTranslations()
{
    d->titleLabel->setText(tr("Asistente de escritura"));
    d->statusLabel->setText(
        tr("Plugin cargado correctamente.\nIteración 1 — esqueleto sin conexión a Claude todavía."));
}

void WritingAssistantView::designSystemChangeEvent(DesignSystemChangeEvent* _event)
{
    Widget::designSystemChangeEvent(_event);

    setBackgroundColor(DesignSystem::color().surface());

    auto titleFont = DesignSystem::font().h6();
    d->titleLabel->setFont(titleFont);
    d->titleLabel->setStyleSheet(
        QString("color: %1;").arg(DesignSystem::color().onSurface().name()));

    auto bodyFont = DesignSystem::font().body1();
    d->statusLabel->setFont(bodyFont);
    d->statusLabel->setStyleSheet(
        QString("color: %1;").arg(DesignSystem::color().onSurface().name()));
}

} // namespace Ui
