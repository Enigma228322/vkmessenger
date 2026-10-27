#pragma once
#include "Messenger.h"
#include <QFileOpenEvent>
#include <QPointer>
#include <QTimer>
#include <QWindow>

// Installed before QML loads, so a cold-start URL is handled by the same path
// as a return to an already running application.
class DeepLinkHandler : public QObject {
public:
    explicit DeepLinkHandler(Messenger &messenger) : m_messenger(messenger) {}
    void setWindow(QWindow *window) {
        m_window = window;
        if (m_activationPending) activateWindow();
    }
protected:
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() != QEvent::FileOpen) return false;
        if (!m_messenger.openDeepLink(static_cast<QFileOpenEvent *>(event)->url())) return false;
        m_activationPending = true;
        QTimer::singleShot(0, this, [this] { activateWindow(); });
        event->accept();
        return true;
    }
private:
    void activateWindow() {
        if (!m_window) return;
        m_activationPending = false;
        if (m_window->visibility() == QWindow::Minimized) m_window->showNormal();
        else m_window->show();
        m_window->raise();
        m_window->requestActivate();
    }
    Messenger &m_messenger;
    QPointer<QWindow> m_window;
    bool m_activationPending = false;
};
