#pragma once
#include <QApplication>

namespace meshlab_openaxis {
// MDI documents share a top-level window. A document-local "current pane"
// flag alone cannot decide ownership; use the main window's active viewport.
inline bool ownsNavigationFocus(const QWidget *viewport, const QWidget *mainWindow,
                                const QWidget *activeViewport, bool available,
                                Qt::ApplicationState state = QGuiApplication::applicationState()) {
    return available && viewport && mainWindow && viewport==activeViewport &&
        state==Qt::ApplicationActive && QApplication::activeWindow()==mainWindow &&
        !QApplication::activeModalWidget() && !QApplication::activePopupWidget();
}
}
