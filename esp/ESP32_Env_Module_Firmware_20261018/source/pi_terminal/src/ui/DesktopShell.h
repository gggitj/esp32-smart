#pragma once

#include <QObject>

class QWindow;
class QSystemTrayIcon;

class DesktopShell final : public QObject {
    Q_OBJECT
public:
    explicit DesktopShell(QObject *parent = nullptr);

    void setWindow(QWindow *window);
    Q_INVOKABLE void showDesktop();
    Q_INVOKABLE void showMainWindow();
    Q_INVOKABLE void quitApplication();

private:
    void activateWindow();

    QWindow *window_ = nullptr;
    QSystemTrayIcon *tray_ = nullptr;
    bool restoreFullscreen_ = true;
};
