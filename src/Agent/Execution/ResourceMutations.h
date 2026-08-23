/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef SIGIL_AGENT_RESOURCE_MUTATIONS_H
#define SIGIL_AGENT_RESOURCE_MUTATIONS_H

#include <QString>
#include <QStringList>

namespace SigilAgent
{

QString defaultXhtmlTemplate();
QString mediaTypeForKind(const QString &kind);
QString kindFromPathOrType(const QString &book_path, const QString &kind);
QString suggestCopyBookPath(const QString &source_path, const QStringList &existing_paths);
bool bookPathTaken(const QString &book_path, const QStringList &existing_paths);

} // namespace SigilAgent

#endif
