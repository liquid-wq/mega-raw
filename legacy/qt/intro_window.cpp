// Natives Intro (QPainter)
// Kein QtWebEngine mehr: kein Chromium, keine Artefakte, ~250 MB kleiner.
#include "intro_window.h"
#include <QApplication>
#include <QScreen>
#include <QPainter>
#include <QPixmap>
#include <QImage>
#include <QTimer>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QPropertyAnimation>
#include <QFont>
#include <QFontMetricsF>
#include <QRadialGradient>
#include <QPainterPath>
#include <QRandomGenerator>
#include <cmath>

namespace {

struct FMeta { int w, h; double cx, cy; int bottom; };
// exakt FMETA aus der HTML
static const FMeta FM[12] = {
    {158,149, 74.0, 73.5,144},{256,159,150.4, 84.0,154},{197,169, 97.9, 87.5,164},
    {233,143,126.4, 69.6,138},{194,158, 99.7, 81.6,153},{208,184,116.1, 91.7,179},
    {201,183,112.9, 91.6,178},{146,186, 72.5, 95.5,181},{181,193, 71.0, 98.8,188},
    {144,199, 71.8,103.9,194},{143,200, 68.4,107.0,196},{144,200, 69.5,107.2,195}
};

struct KFrame { int t, f; double x, arc; };
static const KFrame KF[5] = { {0,11,0,0},{3700,8,0,0},{4500,9,0,0},{4800,10,0,0},{5100,11,0,0} };

static double easeInOut(double x){ return x<0.5 ? 4*x*x*x : 1-std::pow(-2*x+2,3)/2; }
static double lerp(double a,double b,double t){ return a+(b-a)*t; }

struct Star { double x,y,z,s; };
struct TextInfo { QString txt; bool cursor=false, rawr=false; double rawrP=0; double raFade=-1; };

static const QString FULL = QStringLiteral("MEGA-RETROACHIEVEMENTS");

static TextInfo textForTime(double t){
    TextInfo r;
    if(t<1600){ int n=int((t/1600.0)*FULL.length()); r.txt=FULL.left(n+1); r.cursor=true; return r; }
    if(t<2300){ r.txt=FULL; return r; }
    if(t<3050){ const int tg=11; double p=(t-2300)/750.0;
        int rem=int(std::floor(easeInOut(std::min(1.0,p))*(FULL.length()-tg)));
        r.txt=FULL.left(std::max(tg, int(FULL.length())-rem)); return r; }
    if(t<3400){ double p=easeInOut(std::min(1.0,(t-3050)/350.0));
        int del=int(std::round(p*4)); QString mid=QStringLiteral("ETRO").left(4-del);
        r.txt=QStringLiteral("MEGA-R")+mid+QStringLiteral("A"); return r; }
    if(t<3700){ r.txt=QStringLiteral("MEGA-RA"); return r; }
    if(t<4500){ double p=(t-3700)/800.0; int n=int(std::floor(p*8));
        r.txt=QStringLiteral("MEGA-RAW")+QString(n,QChar('R')); r.rawr=true; r.rawrP=p; return r; }
    if(t<5100){ double p=easeInOut((t-4500)/600.0); int n=int(std::round(8*(1-p)));
        r.txt=QStringLiteral("MEGA-RAW")+QString(n,QChar('R')); return r; }
    r.txt=QStringLiteral("MEGA-RAW");
    r.raFade=(t>5900)?std::max(0.0,1-(t-5900)/600.0):1.0;
    return r;
}

struct JasonState { int f; double x, arc; };
static JasonState jasonState(double t){
    if(t<KF[0].t) return {-1,-0.50,0};
    int ki=4;
    for(int i=0;i<4;i++){ if(t>=KF[i].t && t<KF[i+1].t){ ki=i; break; } }
    const KFrame &c=KF[ki], &n=(ki<4)?KF[ki+1]:KF[ki];
    double p=(ki<4)?(t-c.t)/double(n.t-c.t):1.0;
    double ep=easeInOut(std::min(1.0,p));
    return { c.f, lerp(c.x,n.x,ep), lerp(c.arc,n.arc,ep) };
}

// einfacher Box-Blur (3 Paesse ~ Gauss) fuer den RAWR-Glow
static void boxBlur(QImage& img, int radius){
    if(radius<1) return;
    const int w=img.width(), h=img.height();
    QImage tmp(img.size(), img.format());
    for(int pass=0;pass<3;pass++){
        for(int y=0;y<h;y++){ // horizontal
            const QRgb* src=reinterpret_cast<const QRgb*>(img.constScanLine(y));
            QRgb* dst=reinterpret_cast<QRgb*>(tmp.scanLine(y));
            int rs=0,gs=0,bs=0,as=0,cnt=0;
            for(int x=-radius;x<=radius;x++){int xx=std::clamp(x,0,w-1);
                QRgb px=src[xx];rs+=qRed(px);gs+=qGreen(px);bs+=qBlue(px);as+=qAlpha(px);cnt++;}
            for(int x=0;x<w;x++){
                dst[x]=qRgba(rs/cnt,gs/cnt,bs/cnt,as/cnt);
                int xo=std::clamp(x-radius,0,w-1), xn=std::clamp(x+radius+1,0,w-1);
                QRgb po=src[xo],pn=src[xn];
                rs+=qRed(pn)-qRed(po);gs+=qGreen(pn)-qGreen(po);bs+=qBlue(pn)-qBlue(po);as+=qAlpha(pn)-qAlpha(po);
            }
        }
        for(int x=0;x<w;x++){ // vertikal
            int rs=0,gs=0,bs=0,as=0,cnt=0;
            for(int y=-radius;y<=radius;y++){int yy=std::clamp(y,0,h-1);
                QRgb px=reinterpret_cast<const QRgb*>(tmp.constScanLine(yy))[x];
                rs+=qRed(px);gs+=qGreen(px);bs+=qBlue(px);as+=qAlpha(px);cnt++;}
            for(int y=0;y<h;y++){
                reinterpret_cast<QRgb*>(img.scanLine(y))[x]=qRgba(rs/cnt,gs/cnt,bs/cnt,as/cnt);
                int yo=std::clamp(y-radius,0,h-1), yn=std::clamp(y+radius+1,0,h-1);
                QRgb po=reinterpret_cast<const QRgb*>(tmp.constScanLine(yo))[x];
                QRgb pn=reinterpret_cast<const QRgb*>(tmp.constScanLine(yn))[x];
                rs+=qRed(pn)-qRed(po);gs+=qGreen(pn)-qGreen(po);bs+=qBlue(pn)-qBlue(po);as+=qAlpha(pn)-qAlpha(po);
            }
        }
    }
}

class IntroWidget : public QWidget {
public:
    QEventLoop* loop=nullptr;
    explicit IntroWidget(QWidget* parent=nullptr) : QWidget(parent) {
        for(int i=0;i<12;i++)
            frames_[i]=QPixmap(QStringLiteral(":/cat/cat%1.png").arg(i,2,10,QChar('0')));
        timer_.setInterval(16);
        connect(&timer_,&QTimer::timeout,this,[this]{ tick(); });
    }
    // play(): intro_start-Aequivalent -> Fenster zeigen, nach 1000 ms Animation
    void startSequence(){
        initStars();
        show();
        QTimer::singleShot(1000,this,[this]{ clock_.start(); running_=true; timer_.start(); });
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing,true);
        p.setRenderHint(QPainter::SmoothPixmapTransform,true);
        const double W=width(), H=height();
        if(!running_){ p.fillRect(rect(), QColor(5,5,10)); return; }
        double t = double(clock_.elapsed());

        // Hintergrund + radialer Schein (wie createRadialGradient)
        p.fillRect(rect(), QColor(5,5,10));
        QRadialGradient bg(W/2,H/2,std::max(W,H)*0.7);
        bg.setColorAt(0.0, QColor(30,26,18,153));
        bg.setColorAt(1.0, QColor(5,5,10,153));
        p.fillRect(rect(), bg);

        // Sterne (Drift wie im Original, pro gezeichnetem Frame)
        for(Star& st : stars_){
            st.x -= st.z*0.3; if(st.x<0) st.x=W;
            QColor c(255,242,192); c.setAlphaF(st.z*0.7+0.3);
            p.fillRect(QRectF(st.x,st.y,st.s,st.s), c);
        }

        const double cx=W/2, cy=H/2;
        TextInfo ti=textForTime(t);
        JasonState js=jasonState(t);
        double shk = ti.rawr ? (1-ti.rawrP)*14 : 0;
        auto rnd=[&]{ return QRandomGenerator::global()->generateDouble()-0.5; };
        double sx=rnd()*shk, sy=rnd()*shk*0.4;

        // Shockwave beim RAWR
        if(js.f==8 && shockT_<0){
            shockT_=t; shockX_=cx+js.x*W;
            const FMeta& mm=FM[8]; double dh=std::min(H*0.20,100.0), sc=dh/mm.h;
            double gY=cy+H*0.05;
            shockY_ = gY - (mm.bottom-mm.cy)*sc;
        }
        if(shockT_>=0){
            double rt=(t-shockT_)/600.0;
            if(rt>=0 && rt<1){
                double rad=rt*W*0.6;
                QRadialGradient g(shockX_,shockY_,rad);
                g.setColorAt(0.0, QColor(255,246,216,0));
                g.setColorAt(0.7, QColor(255,246,216,0));
                g.setColorAt(0.8, QColor(255,246,216,int((1-rt)*0.5*255)));
                g.setColorAt(1.0, QColor(255,246,216,0));
                p.fillRect(rect(), g);
                QPen pen(QColor(255,255,255,int((1-rt)*255))); pen.setWidthF(2);
                p.setPen(pen); p.setBrush(Qt::NoBrush);
                p.drawEllipse(QPointF(shockX_,shockY_), rad, rad);
            }
        }

        // Katze bodenverankert
        double dispW=0, dispH=0, txx=0, tyy=0; const FMeta* mcur=nullptr;
        if(js.f>=0 && js.f<12 && !frames_[js.f].isNull()){
            const FMeta& m=FM[js.f]; mcur=&m;
            dispH=std::min(H*0.20,100.0);
            double groundY=cy+H*0.05;
            double scale=dispH/m.h; dispW=m.w*scale;
            txx=cx+js.x*W+sx; tyy=groundY+js.arc*H+sy;
            double dx=txx-m.cx*scale, dy=tyy-m.bottom*scale;
            p.drawPixmap(QRectF(std::round(dx),std::round(dy),std::round(dispW),std::round(dispH)),
                         frames_[js.f], frames_[js.f].rect());
            // Sprechblase
            double ba=0;
            if(js.f==11 && t>=3400 && t<3700) ba=std::min(1.0,(t-3400)/250.0);
            else if(js.f==8) ba=std::max(0.0,1-(t-3700)/250.0);
            if(ba>0){
                double bx=txx+dispW*0.18, by=tyy-m.cy*scale-dispH*0.22;
                drawBubble(p,bx,by,ba);
            }
        }

        // Titeltext, zeichenweise, rotes R/A, RAWR-Glow
        int ts2=int(std::round(std::min(W*0.06,56.0)));
        QFont f(QStringLiteral("Courier New")); f.setBold(true); f.setPixelSize(ts2);
        f.setStyleHint(QFont::Monospace);
        QFontMetricsF fm(f);
        const QString& full=ti.txt;
        bool cursor = ti.cursor && ((int(t/250))%2==0);
        QColor baseCol = ti.rawr ? QColor(0xff,0xf6,0xd8) : QColor(0xe8,0xb8,0x4b);
        double raFade = (ti.raFade>=0)?ti.raFade:(ti.rawr?0.0:1.0);
        auto mix=[&](int a,int b){ return int(std::round(a+(b-a)*raFade)); };
        QColor HL(mix(baseCol.red(),255), mix(baseCol.green(),77), mix(baseCol.blue(),77));
        int rIdx=(full.length()>5 && full[5]==QChar('R'))?5:-1, aIdx=-1;
        if(full.startsWith(QStringLiteral("MEGA-R")) && full.endsWith(QStringLiteral("A")) && full.length()>=7) aIdx=full.length()-1;
        else if(full.startsWith(QStringLiteral("MEGA-RETROA"))) aIdx=10;
        else if(full.startsWith(QStringLiteral("MEGA-RA"))) aIdx=6;

        double tw=fm.horizontalAdvance(full+(cursor?QStringLiteral("_"):QString()));
        double ox=std::round(cx+sx), oy=std::round(cy+H*0.19+sy);

        auto drawText=[&](QPainter& tp,double offX,double offY){
            tp.setFont(f);
            double x=-tw/2;
            for(int i=0;i<full.length();i++){
                tp.setPen((i==rIdx||i==aIdx)?HL:baseCol);
                tp.drawText(QPointF(offX+std::round(x), offY+fm.ascent()-fm.height()/2), QString(full[i]));
                x+=fm.horizontalAdvance(QString(full[i]));
            }
            if(cursor){ tp.setPen(baseCol);
                tp.drawText(QPointF(offX+std::round(x), offY+fm.ascent()-fm.height()/2), QStringLiteral("_")); }
        };

        if(ti.rawr){
            // shadowBlur=40-Aequivalent: Text in Bild, Box-Blur, golden getoent dahinter
            int pad=60;
            QImage gl(int(tw)+2*pad, int(fm.height())+2*pad, QImage::Format_ARGB32_Premultiplied);
            gl.fill(Qt::transparent);
            {
                QPainter gp(&gl);
                gp.setRenderHint(QPainter::Antialiasing,true);
                QColor gc(232,184,75); // Glowfarbe unabhaengig von Zeichenfarbe
                gp.setFont(f);
                double x=-tw/2;
                for(int i=0;i<full.length();i++){
                    gp.setPen(gc);
                    gp.drawText(QPointF(pad+tw/2+std::round(x), pad+fm.ascent()), QString(full[i]));
                    x+=fm.horizontalAdvance(QString(full[i]));
                }
            }
            boxBlur(gl,13);
            double ga=0.6+ti.rawrP*0.4;
            p.setOpacity(ga);
            p.drawImage(QPointF(ox-tw/2-pad, oy-fm.height()/2-pad), gl);
            p.setOpacity(1.0);
        }
        drawText(p,ox,oy);

        // © Liqui
        p.save();
        p.setOpacity(0.5);
        QFont f2(QStringLiteral("Courier New")); f2.setItalic(true);
        f2.setPixelSize(int(std::min(W*0.02,18.0)));
        p.setFont(f2); p.setPen(QColor(0x8a,0x7a,0x4a));
        QFontMetricsF fm2(f2);
        QString lq=QStringLiteral("© Liqui");
        p.drawText(QPointF(cx-fm2.horizontalAdvance(lq)/2, cy+H*0.22+fm2.ascent()-fm2.height()/2), lq);
        p.restore();
    }
private:
    void initStars(){
        stars_.clear();
        auto* rg=QRandomGenerator::global();
        for(int i=0;i<70;i++)
            stars_.push_back({ rg->generateDouble()*width(), rg->generateDouble()*height(),
                               rg->generateDouble()*0.6+0.4, rg->generateDouble()*2.5+1.2 });
    }
    void tick(){
        double t=double(clock_.elapsed());
        if(t>=7800.0){ timer_.stop(); if(loop && loop->isRunning()) loop->quit(); return; }
        update();
    }
    QPixmap frames_[12];
    QVector<Star> stars_;
    QTimer timer_;
    QElapsedTimer clock_;
    bool running_=false;
    double shockT_=-1, shockX_=0, shockY_=0;

    void drawBubble(QPainter& p,double cx,double cy,double a){
        if(a<0.01) return;
        p.save(); p.setOpacity(a);
        double bw=50,bh=26,r=7,bx=cx+12,by=cy;
        QPainterPath path;
        path.addRoundedRect(QRectF(bx,by,bw,bh),r,r);
        p.setPen(Qt::NoPen); p.setBrush(QColor(0xff,0xfd,0xf0));
        p.drawPath(path);
        QPainterPath tail;
        tail.moveTo(bx+4,by+bh); tail.lineTo(bx-7,by+bh+11); tail.lineTo(bx+16,by+bh); tail.closeSubpath();
        p.drawPath(tail);
        QFont bf(QStringLiteral("Courier New")); bf.setBold(true); bf.setPixelSize(15);
        p.setFont(bf); p.setPen(QColor(0x66,0x66,0x66));
        QFontMetricsF bfm(bf);
        QString dots=QStringLiteral("...");
        p.drawText(QPointF(bx+bw/2-bfm.horizontalAdvance(dots)/2, by+bh/2+bfm.ascent()-bfm.height()/2), dots);
        p.restore();
    }
};

} // namespace

bool IntroWindow::showBlocking(const QString& /*htmlPath: entfaellt, Intro ist nativ*/) {
    IntroWidget win;
    win.setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    win.setFixedSize(768, 840);
    if (auto* scr = QApplication::primaryScreen()) {
        QRect g = scr->geometry();
        win.move(g.center().x() - 384, g.center().y() - 420);
    }

    QEventLoop loop;
    win.loop = &loop;
    // Original-Timing: 2000 ms warten (waitAndPlay), dann play() -> show + 1000 ms -> Animation
    QTimer::singleShot(2000, &win, [&win]{ win.startSequence(); });
    QTimer::singleShot(40000, &loop, &QEventLoop::quit); // Watchdog wie gehabt

    loop.exec();

    // Sanftes Ausblenden (unveraendert)
    auto* fade = new QPropertyAnimation(&win, "windowOpacity");
    fade->setDuration(450);
    fade->setStartValue(1.0);
    fade->setEndValue(0.0);
    QEventLoop fadeLoop;
    QObject::connect(fade, &QPropertyAnimation::finished, &fadeLoop, &QEventLoop::quit);
    fade->start(QAbstractAnimation::DeleteWhenStopped);
    fadeLoop.exec();

    win.close();
    return true;
}
