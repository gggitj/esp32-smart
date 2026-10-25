#pragma once
#include <QString>
class QQuickWindow;
int runGuiAudit(QQuickWindow *window, const QString &outputDirectory);
bool touchLauncherEntry(QQuickWindow *window, const QString &entryId);
