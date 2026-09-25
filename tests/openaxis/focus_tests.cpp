#include "openaxis_focus.h"
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMainWindow>
#include <QDialog>
#include <iostream>
int main(int argc,char **argv) {
    QApplication app(argc,argv);
    QMainWindow first,second;
    auto *mdi=new QMdiArea(&first); first.setCentralWidget(mdi);
    auto *docA=new QWidget, *docB=new QWidget;
    auto *a=new QWidget(docA), *split=new QWidget(docA), *b=new QWidget(docB);
    auto *subA=mdi->addSubWindow(docA), *subB=mdi->addSubWindow(docB);
    first.show(); subA->show(); subB->show(); app.processEvents();
    QApplication::setActiveWindow(&first);
    QWidget *paneA=a;
    auto active=[&]() -> QWidget * { return mdi->currentSubWindow()==subA ? paneA : b; };
    auto focused=[&](QWidget *pane,Qt::ApplicationState state=Qt::ApplicationActive) {
        return meshlab_openaxis::ownsNavigationFocus(pane,&first,active(),true,state);
    };
    mdi->setActiveSubWindow(subA);
    if (!focused(a) || focused(b) || focused(split)) return 1;
    paneA=split;
    if (focused(a) || !focused(split) || focused(b)) return 2;
    mdi->setActiveSubWindow(subB);
    if (focused(split) || !focused(b)) return 3;
    mdi->setActiveSubWindow(subA);
    if (!focused(split) || focused(b)) return 4;
    second.show(); QApplication::setActiveWindow(&second);
    if (focused(split) || focused(b)) return 5;
    QApplication::setActiveWindow(&first);
    if (!focused(split) || focused(split,Qt::ApplicationInactive)) return 6;
    QDialog modal(&first); modal.setModal(true); modal.show(); app.processEvents();
    if (focused(split)) return 7;
    modal.close(); QApplication::setActiveWindow(&first);
    if (!focused(split)) return 8;
    delete subA;
    mdi->setActiveSubWindow(subB);
    if (!focused(b)) return 9;
    std::cout<<"MDI documents, split panes, top-level windows, focus loss, modal focus and document closure passed\n";
}
