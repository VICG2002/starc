#include "writing_assistant_view.h"

#include <ui/design_system/design_system.h>

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>


namespace Ui {

class WritingAssistantView::Implementation
{
public:
    explicit Implementation(QWidget* _parent);

    QLabel* titleLabel = nullptr;
    QTextEdit* responseArea = nullptr;
    QLineEdit* inputField = nullptr;
    QPushButton* sendButton = nullptr;
    QLabel* statusLabel = nullptr;
};

WritingAssistantView::Implementation::Implementation(QWidget* _parent)
    : titleLabel(new QLabel(_parent))
    , responseArea(new QTextEdit(_parent))
    , inputField(new QLineEdit(_parent))
    , sendButton(new QPushButton(_parent))
    , statusLabel(new QLabel(_parent))
{
    titleLabel->setText(QStringLiteral("Asistente de escritura"));
    titleLabel->setAlignment(Qt::AlignCenter);

    responseArea->setReadOnly(true);
    responseArea->setPlaceholderText(
        QStringLiteral("Las respuestas de Claude aparecerán aquí."));

    inputField->setPlaceholderText(
        QStringLiteral("Escribe tu mensaje y presiona Enter o el botón..."));

    sendButton->setText(QStringLiteral("Enviar"));

    statusLabel->setText(QString());
    statusLabel->setAlignment(Qt::AlignCenter);
}


// ****


WritingAssistantView::WritingAssistantView(QWidget* _parent)
    : Widget(_parent)
    , d(new Implementation(this))
{
    auto inputRow = new QHBoxLayout;
    inputRow->setContentsMargins({});
    inputRow->setSpacing(8);
    inputRow->addWidget(d->inputField, 1);
    inputRow->addWidget(d->sendButton);

    auto layout = new QVBoxLayout;
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);
    layout->addWidget(d->titleLabel);
    layout->addWidget(d->responseArea, 1);
    layout->addLayout(inputRow);
    layout->addWidget(d->statusLabel);
    setLayout(layout);

    //
    // Wiring de submit (click o Enter)
    //
    auto submitHandler = [this] {
        const QString text = d->inputField->text().trimmed();
        if (text.isEmpty()) {
            return;
        }
        d->inputField->clear();
        emit messageSubmitted(text);
    };
    connect(d->sendButton, &QPushButton::clicked, this, submitHandler);
    connect(d->inputField, &QLineEdit::returnPressed, this, submitHandler);
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

void WritingAssistantView::appendUserMessage(const QString& _text)
{
    d->responseArea->append(QStringLiteral("<b>Tú:</b> %1").arg(_text.toHtmlEscaped()));
    d->responseArea->append(QString());
}

void WritingAssistantView::appendAssistantMessage(const QString& _text)
{
    d->responseArea->append(
        QStringLiteral("<b>Claude:</b> %1").arg(_text.toHtmlEscaped()));
    d->responseArea->append(QString());
}

void WritingAssistantView::appendError(const QString& _error)
{
    d->responseArea->append(QStringLiteral(
        "<span style='color:#c00;'><b>Error:</b> %1</span>").arg(_error.toHtmlEscaped()));
    d->responseArea->append(QString());
}

void WritingAssistantView::setStatus(const QString& _status)
{
    d->statusLabel->setText(_status);
}

void WritingAssistantView::setInputEnabled(bool _enabled)
{
    d->inputField->setEnabled(_enabled);
    d->sendButton->setEnabled(_enabled);
    if (_enabled) {
        d->inputField->setFocus();
    }
}

void WritingAssistantView::updateTranslations()
{
    d->titleLabel->setText(tr("Asistente de escritura"));
    d->inputField->setPlaceholderText(tr("Escribe tu mensaje y presiona Enter o el botón..."));
    d->sendButton->setText(tr("Enviar"));
    d->responseArea->setPlaceholderText(tr("Las respuestas de Claude aparecerán aquí."));
}

void WritingAssistantView::designSystemChangeEvent(DesignSystemChangeEvent* _event)
{
    Widget::designSystemChangeEvent(_event);

    setBackgroundColor(DesignSystem::color().surface());

    const auto bodyColor = DesignSystem::color().onSurface().name();

    d->titleLabel->setFont(DesignSystem::font().h6());
    d->titleLabel->setStyleSheet(QString("color: %1;").arg(bodyColor));

    d->responseArea->setFont(DesignSystem::font().body1());
    d->responseArea->setStyleSheet(
        QString("QTextEdit { color: %1; background: %2; border: 1px solid %3; }")
            .arg(bodyColor,
                 DesignSystem::color().background().name(),
                 DesignSystem::color().onBackground().name()));

    d->inputField->setFont(DesignSystem::font().body1());
    d->sendButton->setFont(DesignSystem::font().button());
    d->statusLabel->setFont(DesignSystem::font().caption());
    d->statusLabel->setStyleSheet(
        QString("color: %1;").arg(DesignSystem::color().onSurface().name()));
}

} // namespace Ui
