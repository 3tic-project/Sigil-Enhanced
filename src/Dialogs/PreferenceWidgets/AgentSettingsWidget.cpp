/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "AgentSettingsWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>

#include "Agent/Persistence/AgentSettings.h"

AgentSettingsWidget::AgentSettingsWidget()
{
    setWindowTitle(tr("Native Agent"));
    auto *layout = new QFormLayout(this);
    m_baseUrl = new QLineEdit(this);
    m_baseUrl->setObjectName(QStringLiteral("agentBaseUrl"));
    m_baseUrl->setPlaceholderText(QStringLiteral("https://api.deepseek.com/chat/completions"));
    m_apiKey = new QLineEdit(this);
    m_apiKey->setObjectName(QStringLiteral("agentApiKey"));
    m_apiKey->setEchoMode(QLineEdit::Password);
    m_apiKey->setPlaceholderText(tr("Stored only in local Sigil settings, never in the EPUB or transcript"));
    m_model = new QLineEdit(this);
    m_model->setObjectName(QStringLiteral("agentModel"));
    m_thinking = new QCheckBox(tr("Send thinking (reasoning_content)"), this);
    m_thinking->setObjectName(QStringLiteral("agentThinking"));
    m_effort = new QComboBox(this);
    m_effort->setObjectName(QStringLiteral("agentReasoningEffort"));
    m_effort->addItems({ QStringLiteral("low"), QStringLiteral("medium"), QStringLiteral("high") });
    layout->addRow(tr("Chat Completions URL"), m_baseUrl);
    layout->addRow(tr("API key"), m_apiKey);
    layout->addRow(tr("Model"), m_model);
    layout->addRow(m_thinking);
    layout->addRow(tr("Reasoning effort"), m_effort);
    auto *note = new QLabel(tr("Native Agent talks to an OpenAI-compatible endpoint directly. It does not call MCP or the Python plugin host."), this);
    note->setWordWrap(true);
    layout->addRow(note);
    readSettings();
}

void AgentSettingsWidget::readSettings()
{
    SigilAgent::AgentSettings settings;
    m_baseUrl->setText(settings.baseUrl());
    m_apiKey->setText(settings.apiKey());
    m_model->setText(settings.model());
    m_thinking->setChecked(settings.thinkingEnabled());
    const int effort = m_effort->findText(settings.reasoningEffort());
    m_effort->setCurrentIndex(effort >= 0 ? effort : 1);
}

PreferencesWidget::ResultActions AgentSettingsWidget::saveSettings()
{
    SigilAgent::AgentSettings settings;
    settings.setBaseUrl(m_baseUrl->text().trimmed());
    settings.setApiKey(m_apiKey->text());
    settings.setModel(m_model->text().trimmed());
    settings.setThinkingEnabled(m_thinking->isChecked());
    settings.setReasoningEffort(m_effort->currentText());
    return PreferencesWidget::ResultAction_None;
}
