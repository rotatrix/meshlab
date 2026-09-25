#include "openaxis_overlay.h"
#include <QApplication>
#include <QImage>
#include <iostream>
using namespace meshlab_openaxis::overlay;
int main(int argc,char **argv) {
    QApplication app(argc,argv);
    Clip a{-2,0,0,1},b{2,0,0,1};
    if (!clip_segment(a,b) || a[0]!=-1 || b[0]!=1) return 1;
    a={0,0,0,-1}; b={0,0,0,1};
    if (visible(a) || visible({0,0,2,1})) return 2;
    a={-2,0,0,1}; b={-3,0,0,1};
    if (clip_segment(a,b)) return 3;
    const double identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    const std::map<std::string,std::array<int,3>> palette{
        {"text",{245,245,245}}, {"cursor",{255,235,40}}, {"model",{40,210,255}},
        {"missing",{255,130,130}}, {"skipped",{165,165,165}}};
    openaxis::DiagnosticPresentation frame;
    frame.context="view-A";
    frame.lines={{"OpenAxis navigation diagnostics", "text"},
                 {"pick.cursor: returned candidate", "cursor"},
                 {"pick.viewport_center: unavailable", "missing"},
                 {"selection.bounds: skipped", "skipped"}};
    frame.markers={{"pick.cursor.selection\npick.cursor",{160,150},"cursor"}};
    frame.segments={{{-.6,-.6,0},{.6,-.6,0},"model",1,.35},
                    {{.6,-.6,0},{.6,.1,0},"model",1,.35}};
    for (int dpi:{1,2}) {
        QImage image(640*dpi,360*dpi,QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(dpi); image.fill(Qt::transparent);
        QPainter p(&image);
        draw(p,frame,"view-B",identity,identity,{640,360},palette);
        if (image.pixelColor(160*dpi,150*dpi).alpha()!=0) return 4;
        draw(p,frame,"view-A",identity,identity,{640,360},palette);
        p.end();
        if (image.pixelColor(160*dpi,150*dpi).alpha()==0) return 5;
        // Stacked label glyphs must exist, independently of the GL text engine.
        int bright=0;
        for (int y=142*dpi;y<172*dpi;++y)
            for (int x=172*dpi;x<310*dpi;++x)
                if (image.pixelColor(x,y).red()>100) ++bright;
        if (bright<40*dpi) return 6;
        if (argc>1 && dpi==2) image.save(QString::fromLocal8Bit(argv[1]));
    }
    std::cout<<"Overlay context isolation, clipping, labels and 1x/2x display scaling passed\n";
}
