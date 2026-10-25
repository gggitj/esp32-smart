#include "DesktopShell.h"

#include <QAction>
#include <QApplication>
#include <QIcon>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QWindow>

DesktopShell::DesktopShell(QObject *parent) : QObject(parent) {
    tray_ = new QSystemTrayIcon(QIcon(QStringLiteral(":/icons/orange_pi_terminal.svg")), this);

    auto *menu = new QMenu;
    auto *showAction = menu->addAction(QStringLiteral("返回主界面"));
    connect(showAction, &QAction::triggered, this, &DesktopShell::showMainWindow);
    menu->addSeparator();
    auto *quitAction = menu->addAction(QStringLiteral("退出终端"));
    connect(quitAction, &QAction::triggered, this, &DesktopShell::quitApplication);
    tray_->setContextMenu(menu);
    tray_->setToolTip(QStringLiteral("Orange Pi 模块化终端"));
    connect(tray_, &QSystemTrayIcon::activated, this, [this](auto reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
            showMainWindow();
    });
    tray_->show();
}

void DesktopShell::setWindow(QWindow *window) {
    window_ = window;
    if (window_)
        restoreFullscreen_ = window_->visibility() == QWindow::FullScreen;
}

void DesktopShell::showDesktop() {
    if (!window_) return;
    restoreFullscreen_ = window_->visibility() == QWindow::FullScreen;
    window_->hide();
}

void DesktopShell::activateWindow() {
    if (!window_) return;
    if (restoreFullscreen_) window_->showFullScreen();
    else window_->showNormal();
    window_->raise();
    window_->requestActivate();
}

void DesktopShell::showMainWindow() {
    activateWindow();
}

void DesktopShell::quitApplication() {
    qApp->quit();
}
