// ============================================================
// RAKIT DI LAUT - Raft Ocean Survival (V6)
// C++ + Raylib | Bresenham & DDA Line Drawing
//
// Fitur baru:
//   [TAB]  Toggle mode wireframe (outline 2D / normal fill)
//   Audio  Suara angin & ombak laut (PCM synthesis via AudioStream)
// ============================================================

#include "raylib.h"
#include "raymath.h"
#include <vector>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <string>
#include <algorithm>

const int SCREEN_W = 1200;
const int SCREEN_H = 700;

// ================================================================
// AUDIO — PCM synthesis: angin & ombak
// ================================================================
static const int AUDIO_RATE    = 44100;
static const int AUDIO_BUFSIZE = 1024;

struct AudioState {
    AudioStream windStream;
    AudioStream waveStream;
    float windPhase  = 0;
    float wavePhase  = 0;
    bool  ready      = false;
};

// Angin: noise pink sederhana + modulasi lambat
static void FillWindBuffer(
    short* buf, int count, float& phase, float vol
) {
    static float prev = 0;
    for (int i = 0; i < count; i++) {
        float noise = ((float)(rand()%32767)/32767.0f)*2.0f - 1.0f;
        float f = prev*0.92f + noise*0.08f;
        prev = f;
        phase += 0.00008f;
        float mod = 0.6f + 0.4f*sinf(phase);
        buf[i] = (short)(f * mod * vol * 32000.0f);
    }
}

// Ombak: 3 sinus frekuensi rendah + noise tekstur
static void FillWaveBuffer(
    short* buf, int count, float& phase, float vol
) {
    for (int i = 0; i < count; i++) {
        float s = sinf(phase*0.80f)*0.50f
                + sinf(phase*1.30f)*0.30f
                + sinf(phase*2.10f)*0.20f;
        float noise = ((float)(rand()%1000)/1000.0f - 0.5f)*0.12f;
        float env   = 0.5f + 0.5f*sinf(phase*0.015f);
        buf[i] = (short)((s + noise)*env * vol * 28000.0f);
        phase += (2.0f*PI*120.0f) / AUDIO_RATE;
    }
}

// ================================================================
// WIREFRAME TOGGLE (global, dipakai oleh semua fungsi draw)
// ================================================================
static bool g_wireframe = false;

// ================================================================
// BRESENHAM LINE
// ================================================================
void DrawBresenhamLine(int x0, int y0, int x1, int y1, Color c) {
    int dx = abs(x1-x0), dy = abs(y1-y0);
    int sx = x0<x1?1:-1, sy = y0<y1?1:-1;
    int err = dx-dy;
    while (true) {
        DrawPixel(x0, y0, c);
        if (x0==x1 && y0==y1) break;
        int e2 = 2*err;
        if (e2>-dy) { err-=dy; x0+=sx; }
        if (e2< dx) { err+=dx; y0+=sy; }
    }
}

void DrawBresenhamThick(
    int x0, int y0, int x1, int y1, Color c, int th
) {
    bool steep = abs(y1-y0) > abs(x1-x0);
    for (int t = -th/2; t <= th/2; t++) {
        if (steep) DrawBresenhamLine(x0+t, y0, x1+t, y1, c);
        else       DrawBresenhamLine(x0, y0+t, x1, y1+t, c);
    }
}

// ================================================================
// DDA LINE
// ================================================================
void DrawDDALine(float x0, float y0, float x1, float y1, Color c) {
    float dx=x1-x0, dy=y1-y0;
    int steps = (int)(fabs(dx)>fabs(dy) ? fabs(dx) : fabs(dy));
    if (!steps) return;
    float xi=dx/steps, yi=dy/steps;
    for (int i=0; i<=steps; i++) {
        DrawPixel((int)roundf(x0),(int)roundf(y0),c);
        x0+=xi; y0+=yi;
    }
}

void DrawDDAThick(
    float x0, float y0, float x1, float y1, Color c, int th
) {
    bool steep = fabs(y1-y0) > fabs(x1-x0);
    for (int t = -th/2; t <= th/2; t++) {
        if (steep) DrawDDALine(x0+t, y0, x1+t, y1, c);
        else       DrawDDALine(x0, y0+t, x1, y1+t, c);
    }
}

// ================================================================
// CIRCLE — outline Bresenham, fill dengan scanline DrawRectangle
// DrawCircle() otomatis memilih mode sesuai g_wireframe
// ================================================================
void DrawBresenhamCircleOutline(int cx, int cy, int r, Color c) {
    int x=0, y=r, d=3-2*r;
    auto p = [&](int px, int py) {
        DrawPixel(cx+px,cy+py,c); DrawPixel(cx-px,cy+py,c);
        DrawPixel(cx+px,cy-py,c); DrawPixel(cx-px,cy-py,c);
        DrawPixel(cx+py,cy+px,c); DrawPixel(cx-py,cy+px,c);
        DrawPixel(cx+py,cy-px,c); DrawPixel(cx-py,cy-px,c);
    };
    while (y>=x) {
        p(x,y);
        if (d>0) { d+=4*(x-y)+10; y--; } else d+=4*x+6;
        x++;
    }
}

void DrawCircle(int cx, int cy, int r, Color fill, Color outline) {
    if (g_wireframe) {
        DrawBresenhamCircleOutline(cx, cy, r, outline);
    } else {
        for (int dy=-r; dy<=r; dy++) {
            int dx=(int)sqrtf(fmaxf(0.f,(float)(r*r-dy*dy)));
            DrawRectangle(cx-dx, cy+dy, dx*2+1, 1, fill);
        }
    }
}

// Span horizontal — dilewati di wireframe mode
void DrawSpan(int x, int y, int w, Color fill) {
    if (!g_wireframe && w>0) DrawRectangle(x, y, w, 1, fill);
}

// ================================================================
// BACKGROUND GRADIENT (Baked ke RenderTexture)
// ================================================================
struct SkyColors {
    Color skyTop, skyBottom, waterDeep, waterShallow;
};

bool ColorsChanged(const SkyColors& a, const SkyColors& b) {
    auto d = [](Color x, Color y) {
        return abs(x.r-y.r)+abs(x.g-y.g)+abs(x.b-y.b) > 8;
    };
    return d(a.skyTop,b.skyTop) || d(a.skyBottom,b.skyBottom)
        || d(a.waterDeep,b.waterDeep)
        || d(a.waterShallow,b.waterShallow);
}

void BakeBackground(
    RenderTexture2D& rt, const SkyColors& sc, int hY
) {
    BeginTextureMode(rt);
    for (int y=0; y<hY; y++) {
        float t = (float)y/hY;
        DrawRectangle(0, y, SCREEN_W, 1, {
            (unsigned char)(sc.skyTop.r+(sc.skyBottom.r-sc.skyTop.r)*t),
            (unsigned char)(sc.skyTop.g+(sc.skyBottom.g-sc.skyTop.g)*t),
            (unsigned char)(sc.skyTop.b+(sc.skyBottom.b-sc.skyTop.b)*t),
            255 });
    }
    for (int y=hY; y<SCREEN_H; y++) {
        float t = (float)(y-hY)/(SCREEN_H-hY);
        DrawRectangle(0, y, SCREEN_W, 1, {
            (unsigned char)(sc.waterDeep.r+(sc.waterShallow.r-sc.waterDeep.r)*t),
            (unsigned char)(sc.waterDeep.g+(sc.waterShallow.g-sc.waterDeep.g)*t),
            (unsigned char)(sc.waterDeep.b+(sc.waterShallow.b-sc.waterDeep.b)*t),
            255 });
    }
    EndTextureMode();
}

float WaveY(
    float x, float t, float bY, float amp, float freq, float spd
) {
    return bY
        + sinf(x*freq + t*spd)*amp
        + sinf(x*freq*0.7f + t*spd*1.3f)*amp*0.4f;
}

// ================================================================
// DRAW — MATAHARI
// ================================================================
void DrawSun(int cx, int cy, float alpha) {
    Color glow = {255,240,100,(unsigned char)(55*alpha)};
    Color sunC = {255,210,50, (unsigned char)(255*alpha)};
    Color rayC = {255,220,80, (unsigned char)(175*alpha)};
    Color sunOut= {255,180,30,(unsigned char)(255*alpha)};

    DrawCircle(cx, cy, 44, glow, glow);
    DrawCircle(cx, cy, 30, sunC, sunOut);
    for (int i=0; i<12; i++) {
        float a = i/12.0f*2.0f*PI;
        DrawDDAThick(
            cx+38*cosf(a), cy+38*sinf(a),
            cx+54*cosf(a), cy+54*sinf(a), rayC, 2);
    }
}

// ================================================================
// DRAW — BULAN
// ================================================================
void DrawMoon(int cx, int cy, float alpha) {
    Color moonC  = {220,230,255,(unsigned char)(255*alpha)};
    Color shadow = {14, 23, 54, (unsigned char)(255*alpha)};
    Color starC  = {255,255,210,(unsigned char)(195*alpha)};

    DrawCircle(cx,   cy,   22, moonC,  moonC);
    DrawCircle(cx+7, cy-4, 18, shadow, shadow);
    int sp[][2] = {{45,25},{-55,18},{75,-38},{-28,-55},{58,55}};
    for (auto& s : sp) {
        DrawDDALine((float)(cx+s[0]-4),(float)(cy+s[1]),
                    (float)(cx+s[0]+4),(float)(cy+s[1]), starC);
        DrawDDALine((float)(cx+s[0]),(float)(cy+s[1]-4),
                    (float)(cx+s[0]),(float)(cy+s[1]+4), starC);
    }
}

// ================================================================
// DRAW — BINTANG
// ================================================================
void DrawStars(std::vector<Vector2>& stars, float alpha) {
    if (alpha < 0.02f) return;
    Color sc  = {255,255,220,(unsigned char)(255*alpha)};
    Color sc2 = {200,200,180,(unsigned char)(140*alpha)};
    for (int i=0; i<(int)stars.size(); i++) {
        int sx=(int)stars[i].x, sy=(int)stars[i].y;
        DrawPixel(sx,sy,sc);
        if (i%7==0) {
            DrawPixel(sx-1,sy,sc2); DrawPixel(sx+1,sy,sc2);
            DrawPixel(sx,sy-1,sc2); DrawPixel(sx,sy+1,sc2);
        }
    }
}

// ================================================================
// DRAW — AWAN
// ================================================================
void DrawCloud(int cx, int cy, float alpha) {
    Color c1 = {255,255,255,(unsigned char)(188*alpha)};
    Color c2 = {208,218,234,(unsigned char)(138*alpha)};
    Color wf = {180,200,230,(unsigned char)(200*alpha)};

    if (g_wireframe) {
        DrawBresenhamCircleOutline(cx,    cy+6,  23, wf);
        DrawBresenhamCircleOutline(cx-26, cy+11, 19, wf);
        DrawBresenhamCircleOutline(cx,    cy+6,  27, wf);
        DrawBresenhamCircleOutline(cx+28, cy+11, 19, wf);
        DrawBresenhamCircleOutline(cx+10, cy-10, 17, wf);
        DrawBresenhamCircleOutline(cx-12, cy-6,  16, wf);
    } else {
        DrawCircle(cx,    cy+6,  23, c2, c2);
        DrawCircle(cx-26, cy+11, 19, c1, c1);
        DrawCircle(cx,    cy+6,  27, c1, c1);
        DrawCircle(cx+28, cy+11, 19, c1, c1);
        DrawCircle(cx+10, cy-10, 17, c1, c1);
        DrawCircle(cx-12, cy-6,  16, c1, c1);
    }
}

// ================================================================
// DRAW — PULAU
// ================================================================
void DrawIsland(int cx, int cy) {
    Color sandD = {173,146,76,255}, sandL = {213,188,116,255};
    Color grass = {56,126,46,255},  trunk = {116,76,36,255};
    Color leaf  = {36,156,56,255},  leafD = {16,106,36,255};
    Color coco  = {96,66,26,255};
    Color wf    = {100,200,100,200};

    int lx=cx+25, ly=cy-108;
    int fr[][2]={{-52,-18},{-32,-44},{0,-54},{32,-44},{56,-14},{50,22}};

    if (g_wireframe) {
        // Kontur bukit dengan elips Bresenham
        for (int i=0; i<82; i+=6) {
            int w=(int)(87.0f*sinf((float)i/82.0f*PI));
            DrawPixel(cx-w, cy-i+42, wf);
            DrawPixel(cx+w, cy-i+42, wf);
        }
        DrawDDALine((float)cx,(float)(cy-36),(float)lx,(float)ly,trunk);
        for (auto& f:fr)
            DrawBresenhamLine(lx,ly, lx+f[0],ly+f[1], wf);
        DrawBresenhamCircleOutline(lx-12,ly+8, 7, wf);
        DrawBresenhamCircleOutline(lx+5, ly+12,6, wf);
        DrawBresenhamCircleOutline(lx+18,ly+5, 7, wf);
    } else {
        for (int i=0; i<82; i++) {
            int w=(int)(87.0f*sinf((float)i/82.0f*PI));
            Color c=(i<15)?sandD:(i<52?sandL:grass);
            DrawRectangle(cx-w,cy-i+42,w*2,1,c);
        }
        for (int k=0;k<4;k++)
            DrawDDAThick((float)(cx+k*2),(float)(cy-36),
                         (float)(lx+k*2),(float)ly, trunk, 5);
        for (auto& f:fr) {
            DrawBresenhamThick(lx,ly, lx+f[0],ly+f[1], leaf,3);
            DrawBresenhamThick(lx,ly, lx+f[0]/2,ly+f[1]/2-10, leafD,2);
        }
        DrawCircle(lx-12,ly+8, 7, coco, coco);
        DrawCircle(lx+5, ly+12,6, coco, coco);
        DrawCircle(lx+18,ly+5, 7, coco, coco);
    }
}

// ================================================================
// DRAW — RAKIT
// ================================================================
void DrawRaft(int cx, int cy) {
    Color logB  = {137,88,42,255}, logD = {98,58,24,255};
    Color logL  = {178,128,68,255}, rope = {196,166,96,255};
    Color sail  = {236,216,166,255}, sailW= {216,190,136,200};
    Color sailLn= {176,146,96,255}, pole = {116,76,36,255};
    Color wf    = {200,180,100,220};

    int sx0=cx+5,sy0=cy-92, sx1=cx+68,sy1=cy-38, sx2=cx+5,sy2=cy-10;

    if (g_wireframe) {
        for (int i=0; i<5; i++) {
            int ly=cy-20+i*12;
            DrawBresenhamLine(cx-45,ly,    cx+45,ly,    wf);
            DrawBresenhamLine(cx-45,ly+10, cx+45,ly+10, wf);
            DrawBresenhamLine(cx-45,ly,    cx-45,ly+10, wf);
            DrawBresenhamLine(cx+45,ly,    cx+45,ly+10, wf);
        }
        int rxs[]={cx-28,cx,cx+28};
        for (int rx:rxs) DrawBresenhamLine(rx,cy-20, rx,cy+40, rope);
        DrawBresenhamLine(cx+5,cy+38, cx+5,cy-92, pole);
        DrawBresenhamLine(sx0,sy0, sx1,sy1, wf);
        DrawBresenhamLine(sx1,sy1, sx2,sy2, wf);
        DrawBresenhamLine(sx2,sy2, sx0,sy0, wf);
    } else {
        for (int i=0; i<5; i++) {
            int ly=cy-20+i*12;
            DrawRectangle(cx-45,ly,   90,10,logB);
            DrawRectangle(cx-45,ly,   90, 2,logD);
            DrawRectangle(cx-45,ly+7, 90, 3,logD);
            DrawBresenhamLine(cx-45,ly+4, cx+45,ly+4, logL);
            DrawCircle(cx-45,ly+5, 5, logD, logD);
            DrawCircle(cx+45,ly+5, 5, logD, logD);
        }
        int rxs[]={cx-28,cx,cx+28};
        for (int rx:rxs)
            DrawDDAThick((float)rx,(float)(cy-20),
                         (float)rx,(float)(cy+40), rope, 2);
        DrawBresenhamThick(cx+5,cy+38, cx+5,cy-92, pole, 4);
        for (int scanY=sy0; scanY<=sy2; scanY++) {
            int lx2=sx0, rx2;
            float t2;
            if (scanY<=sy1) {
                t2=(float)(scanY-sy0)/(sy1-sy0+1);
                rx2=sx0+(int)(t2*(sx1-sx0));
            } else {
                t2=(float)(scanY-sy1)/(sy2-sy1+1);
                rx2=sx1+(int)(t2*(sx2-sx1));
            }
            Color sc=((scanY-sy0)%14<3)?sailW:sail;
            DrawSpan(lx2,scanY,rx2-lx2,sc);
        }
        DrawBresenhamThick(sx0,sy0, sx1,sy1, sailLn,2);
        DrawBresenhamThick(sx1,sy1, sx2,sy2, sailLn,2);
        DrawBresenhamThick(sx2,sy2, sx0,sy0, sailLn,1);
        DrawDDALine((float)sx0,(float)sy0,
                    (float)(cx+5),(float)(cy+38), rope);
    }
}

// ================================================================
// DRAW — HIU
// ================================================================
void DrawShark(float cx, float cy, bool facingLeft) {
    Color body  = {76,96,116,255},  belly = {196,208,216,255};
    Color fin   = {56,76,96,255},   eyeC  = {6,6,6,255};
    Color teeth = {236,236,236,255}, gill  = {46,66,86,180};
    Color wf    = {100,160,220,220};

    int dir=facingLeft?-1:1;
    int bx=(int)cx, by=(int)cy, bw=60, bh=22;

    if (g_wireframe) {
        // Kontur elips badan dengan sample vertikal
        for (int dy2=-bh; dy2<=bh; dy2+=3) {
            float r=(float)bw*sqrtf(fmaxf(0.f,
                1.f-(float)(dy2*dy2)/(bh*bh)));
            DrawPixel(bx-(int)r, by+dy2, wf);
            DrawPixel(bx+(int)r, by+dy2, wf);
        }
        int tipX=bx+dir*15, tipY=by-bh-36;
        DrawBresenhamLine(bx-dir*10,by-bh, tipX,tipY, wf);
        DrawBresenhamLine(tipX,tipY, bx+dir*20,by-bh, wf);
        int tx=bx-dir*bw;
        DrawBresenhamLine(tx,by, tx-dir*26,by-22, wf);
        DrawBresenhamLine(tx,by, tx-dir*26,by+22, wf);
        DrawBresenhamCircleOutline(bx+dir*30, by-5, 4, wf);
        DrawDDALine((float)(bx+dir*(bw-5)),(float)(by+8),
                    (float)(bx+dir*(bw-25)),(float)(by+8), wf);
    } else {
        for (int dy2=-bh; dy2<=bh; dy2++) {
            int hw=(int)(bw*sqrtf(fmaxf(0.f,
                1.f-(float)(dy2*dy2)/(bh*bh))));
            DrawRectangle(bx-hw,by+dy2,hw*2,1,(dy2>6)?belly:body);
        }
        int tipX=bx+dir*15, tipY=by-bh-36;
        DrawBresenhamThick(bx-dir*10,by-bh, bx+dir*20,by-bh, fin,3);
        DrawBresenhamThick(bx-dir*10,by-bh, tipX,tipY, fin,3);
        DrawBresenhamThick(tipX,tipY, bx+dir*20,by-bh, fin,2);
        int tx=bx-dir*bw;
        DrawBresenhamThick(tx,by-5, tx-dir*26,by-23, fin,3);
        DrawBresenhamThick(tx,by+5, tx-dir*26,by+23, fin,3);
        DrawBresenhamLine(tx-dir*26,by-23, tx-dir*26,by+23, fin);
        DrawDDAThick((float)(bx+dir*10),(float)(by+5),
                     (float)(bx+dir*32),(float)(by+20), fin,3);
        DrawCircle(bx+dir*30,by-5, 5, body, body);
        DrawCircle(bx+dir*30,by-5, 3, eyeC, eyeC);
        DrawPixel(bx+dir*31,by-6, WHITE);
        int mx=bx+dir*bw-dir*5;
        for (int t=0;t<4;t++) {
            int tx2=mx-dir*t*7;
            DrawDDALine((float)tx2,(float)(by+8),
                        (float)(tx2+dir*3),(float)(by+12), teeth);
            DrawDDALine((float)(tx2+dir*3),(float)(by+12),
                        (float)(tx2+dir*6),(float)(by+8),  teeth);
        }
        for (int g=0;g<3;g++)
            DrawBresenhamLine(bx+dir*(20-g*8),by-10,
                              bx+dir*(20-g*8)-dir*3,by+12, gill);
    }
}

// ================================================================
// DRAW — TONG (Barrel)
// ================================================================
void DrawBarrel(float cx, float cy, float scale=1.0f) {
    Color stave={126,76,36,255}, hoop={76,56,26,255};
    Color lid  ={156,106,56,255}, wf  ={200,140,80,220};

    int bx=(int)cx, by=(int)cy;
    int bw=(int)(18*scale), bh=(int)(28*scale);

    if (g_wireframe) {
        DrawBresenhamLine(bx-bw,by-bh, bx+bw,by-bh, wf);
        DrawBresenhamLine(bx-bw,by+bh, bx+bw,by+bh, wf);
        DrawBresenhamLine(bx-bw,by-bh, bx-bw,by+bh, wf);
        DrawBresenhamLine(bx+bw,by-bh, bx+bw,by+bh, wf);
        DrawDDALine((float)(bx-bw),(float)by,
                    (float)(bx+bw),(float)by, wf);
        DrawDDALine((float)(bx-bw),(float)(by-bh+(int)(5*scale)),
                    (float)(bx+bw),(float)(by-bh+(int)(5*scale)), wf);
        DrawDDALine((float)(bx-bw),(float)(by+bh-(int)(5*scale)),
                    (float)(bx+bw),(float)(by+bh-(int)(5*scale)), wf);
    } else {
        for (int s=-2;s<=2;s++) {
            int sx=bx+s*(int)(7*scale);
            int bulge=(int)(3*scale*sinf((float)(s+2)/4.0f*PI));
            DrawRectangle(sx-bulge-2,by-bh,
                (int)(5*scale)+bulge*2, bh*2, stave);
        }
        int hys[]={by-bh+(int)(5*scale), by, by+bh-(int)(5*scale)};
        for (int hy:hys)
            DrawDDAThick((float)(bx-bw),(float)hy,
                         (float)(bx+bw),(float)hy, hoop, 3);
        DrawRectangle(bx-bw+2,by-bh, bw*2-4,(int)(4*scale), lid);
    }
}

// ================================================================
// DRAW — KAYU LOG
// ================================================================
void DrawWoodLog(float cx, float cy, float angle, float scale=1.0f) {
    Color logC ={136,91,46,255},  logD={96,61,26,255};
    Color barkL={156,111,61,200}, wf  ={180,130,70,220};

    int bx=(int)cx, by=(int)cy;
    int len=(int)(35*scale);
    float cs=cosf(angle), sn=sinf(angle);

    if (g_wireframe) {
        DrawBresenhamLine(
            bx-(int)(len*cs)-(int)(6*sn),
            by-(int)(len*sn)+(int)(6*cs),
            bx+(int)(len*cs)-(int)(6*sn),
            by+(int)(len*sn)+(int)(6*cs), wf);
        DrawBresenhamLine(
            bx-(int)(len*cs)+(int)(6*sn),
            by-(int)(len*sn)-(int)(6*cs),
            bx+(int)(len*cs)+(int)(6*sn),
            by+(int)(len*sn)-(int)(6*cs), wf);
        DrawBresenhamCircleOutline(
            bx+(int)(len*cs),by+(int)(len*sn),(int)(5*scale),wf);
        DrawBresenhamCircleOutline(
            bx-(int)(len*cs),by-(int)(len*sn),(int)(5*scale),wf);
    } else {
        for (int t=(int)(-6*scale); t<=(int)(6*scale); t++) {
            float px0=-len*cs+t*(-sn), py0=-len*sn+t*cs;
            float px1= len*cs+t*(-sn), py1= len*sn+t*cs;
            int at=abs(t);
            Color c=(at>4)?logD:(at>2?logC:barkL);
            DrawBresenhamLine(bx+(int)px0,by+(int)py0,
                              bx+(int)px1,by+(int)py1, c);
        }
        int ex0=bx+(int)(len*cs), ey0=by+(int)(len*sn);
        int ex1=bx-(int)(len*cs), ey1=by-(int)(len*sn);
        DrawCircle(ex0,ey0,(int)(5*scale),{116,76,36,255},{116,76,36,255});
        DrawCircle(ex1,ey1,(int)(5*scale),{116,76,36,255},{116,76,36,255});
        DrawBresenhamCircleOutline(ex0,ey0,(int)(5*scale),logD);
        DrawBresenhamCircleOutline(ex1,ey1,(int)(5*scale),logD);
    }
}

// ================================================================
// STRUCTS
// ================================================================
struct Collectible {
    float x, y, bobPhase, velX, collectTimer;
    bool  isBarrel, alive, collecting;
};
struct CollectParticle {
    float x, y, vy, life, maxLife;
    bool  isBarrel;
};
struct Shark { float x, y, speed, wavePhase; bool facingLeft; };
struct Cloud { float x, y, speed; };

// ================================================================
// MAIN
// ================================================================
int main() {
    srand((unsigned int)time(nullptr));
    InitWindow(SCREEN_W, SCREEN_H, "Rakit di Laut - Raft Ocean Survival");
    SetTargetFPS(60);

    // ---- Audio ----
    InitAudioDevice();
    AudioState audio;
    if (IsAudioDeviceReady()) {
        audio.windStream = LoadAudioStream(AUDIO_RATE, 16, 1);
        audio.waveStream = LoadAudioStream(AUDIO_RATE, 16, 1);
        SetAudioStreamVolume(audio.windStream, 0.18f);
        SetAudioStreamVolume(audio.waveStream, 0.22f);
        PlayAudioStream(audio.windStream);
        PlayAudioStream(audio.waveStream);
        audio.ready = true;
    }
    static short windBuf[AUDIO_BUFSIZE];
    static short waveBuf[AUDIO_BUFSIZE];

    // ---- Background ----
    RenderTexture2D bgTex = LoadRenderTexture(SCREEN_W, SCREEN_H);
    SkyColors lastSky = {BLACK,BLACK,BLACK,BLACK}, curSky;
    const int horizonY = (int)(SCREEN_H*0.54f);

    const float RAFT_Y_MIN = (float)horizonY + 35.0f;
    const float RAFT_Y_MAX = (float)(SCREEN_H*0.78f);

    float raftX=SCREEN_W*0.38f, raftY=SCREEN_H*0.56f;
    float raftVX=0, raftVY=0, raftBob=0;
    int   score=0, lives=3;
    float invincible=0;

    float dayTime  = 0.08f;
    const float DAY_SPEED = 0.0022f;

    std::vector<Vector2> stars;
    for (int i=0;i<200;i++)
        stars.push_back({(float)(rand()%SCREEN_W),
                         (float)(rand()%(int)(horizonY))});

    std::vector<Collectible>     items;
    std::vector<CollectParticle> particles;

    auto spawnItem = [&](bool atEdge) {
        Collectible c;
        c.x = atEdge
            ? (float)(SCREEN_W+60+rand()%200)
            : (float)(100+rand()%(SCREEN_W-200));
        c.y        = horizonY + 25.0f + (float)(rand()%140);
        c.bobPhase = (float)(rand()%628)/100.0f;
        c.isBarrel = (rand()%2==0);
        c.alive = true; c.collecting = false; c.collectTimer = 0;
        c.velX = -(1.3f+(float)(rand()%14)/10.0f);
        items.push_back(c);
    };
    for (int i=0;i<5;i++) spawnItem(false);

    std::vector<Shark> sharks;

    auto spawnSharkAt = [&](float startX, bool fl) {
        Shark s;
        s.facingLeft = fl;
        s.x = startX;
        s.y = (float)(horizonY + 60 + rand()%100);
        s.speed = 0.8f + (float)(rand()%10)/10.0f;
        s.wavePhase = (float)(rand()%628)/100.0f;
        sharks.push_back(s);
    };
    // 2 hiu awal langsung di layar
    spawnSharkAt(SCREEN_W*0.8f, true);
    spawnSharkAt(SCREEN_W*0.2f, false);

    auto spawnShark = [&]() {
        bool fl=(rand()%2==0);
        spawnSharkAt(fl?(float)(SCREEN_W+80):-80.0f, fl);
    };

    std::vector<Cloud> clouds;
    for (int i=0;i<5;i++)
        clouds.push_back({(float)(rand()%SCREEN_W),
                          (float)(50+rand()%120),
                          0.18f+(float)(rand()%12)/20.0f});

    float gameTime=0, waveOffset=0;
    float spawnTimer=0, sharkTimer=0, msgTimer=0;
    std::string message="";
    float islandX=SCREEN_W-155.0f, prevRaftX=raftX;

    // ================================================================
    // GAME LOOP
    // ================================================================
    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt>0.05f) dt=0.05f;
        gameTime+=dt; waveOffset+=dt;
        spawnTimer+=dt; sharkTimer+=dt;
        dayTime+=DAY_SPEED*dt;
        if (dayTime>=0.24f) dayTime-=0.24f;
        if (invincible>0) invincible-=dt;
        if (msgTimer>0)   msgTimer-=dt;

        // Toggle wireframe
        if (IsKeyPressed(KEY_TAB)) g_wireframe = !g_wireframe;

        // ---- Celestial positions ----
        float sunProg  = (dayTime-0.05f)/0.13f;
        int   sunX = (int)(-80 + sunProg*(SCREEN_W+160));
        int   sunY = (int)(horizonY+40 - sinf(sunProg*PI)*(horizonY-20));

        float moonT    = dayTime+0.12f;
        if (moonT>=0.24f) moonT-=0.24f;
        float moonProg = (moonT-0.05f)/0.13f;
        int   moonX = (int)(-80 + moonProg*(SCREEN_W+160));
        int   moonY = (int)(horizonY+40 - sinf(moonProg*PI)*(horizonY-20));

        float sunAlpha=0, moonAlpha=0, nightness=0;

        // ---- Sky / ocean colors ----
        if (dayTime < 0.05f) {
            float t=dayTime/0.05f;
            curSky.skyTop =
                
                    {(unsigned char)(5+t*50),
                     (unsigned char)(5+t*30),
                     (unsigned char)(30+t*60),
                     255};
            curSky.skyBottom =
                
                    {(unsigned char)(20+t*80),
                     (unsigned char)(15+t*40),
                     (unsigned char)(50+t*60),
                     255};
            curSky.waterDeep =
                
                    {(unsigned char)(10+t*20),
                     (unsigned char)(30+t*30),
                     (unsigned char)(70+t*40),
                     255};
            curSky.waterShallow =
                
                    {(unsigned char)(30+t*40),
                     (unsigned char)(60+t*40),
                     (unsigned char)(100+t*30),
                     255};
            nightness=1.0f-t; moonAlpha=1.0f-t; sunAlpha=0;
        } else if (dayTime < 0.07f) {
            float t=(dayTime-0.05f)/0.02f;
            curSky.skyTop =
                
                    {(unsigned char)(55+t*80),
                     (unsigned char)(35+t*80),
                     (unsigned char)(90+t*60),
                     255};
            curSky.skyBottom =
                
                    {(unsigned char)(100+t*100),
                     (unsigned char)(55+t*80),
                     (unsigned char)(110-t*40),
                     255};
            curSky.waterDeep =
                
                    {(unsigned char)(30+t*30),
                     (unsigned char)(60+t*40),
                     (unsigned char)(110+t*30),
                     255};
            curSky.waterShallow =
                
                    {(unsigned char)(70+t*50),
                     (unsigned char)(100+t*40),
                     (unsigned char)(130+t*20),
                     255};
            sunAlpha=t; nightness=0; moonAlpha=0;
        } else if (dayTime < 0.16f) {
            float t=(dayTime-0.07f)/0.09f;
            curSky.skyTop = {40,100,(unsigned char)(180+18*(1.0f-t)),255};
            curSky.skyBottom =
                {(unsigned char)(120+t*30),(unsigned char)(178+t*20),228,255};
            curSky.waterDeep = {0,(unsigned char)(78+t*40),(unsigned char)(138+t*30),255};
            curSky.waterShallow =
                
                    {(unsigned char)(t*28),
                     (unsigned char)(138+t*30),
                     (unsigned char)(178+t*20),
                     255};
            sunAlpha=1.0f; nightness=0; moonAlpha=0;
        } else if (dayTime < 0.18f) {
            float t=(dayTime-0.16f)/0.02f;
            curSky.skyTop =
                
                    {(unsigned char)(133-t*80),
                     (unsigned char)(138-t*100),
                     (unsigned char)(208-t*118),
                     255};
            curSky.skyBottom =
                
                    {(unsigned char)(238-t*80),
                     (unsigned char)(128-t*58),
                     (unsigned char)(78-t*48),
                     255};
            curSky.waterDeep =
                
                    {(unsigned char)(38-t*28),
                     (unsigned char)(118-t*78),
                     (unsigned char)(168-t*98),
                     255};
            curSky.waterShallow =
                
                    {(unsigned char)(98-t*68),
                     (unsigned char)(148-t*98),
                     (unsigned char)(178-t*98),
                     255};
            sunAlpha=1.0f-t; moonAlpha=t*0.5f; nightness=0;
        } else {
            float t=(dayTime-0.18f)/0.06f;
            curSky.skyTop =
                
                    {(unsigned char)(53-t*48),
                     (unsigned char)(38-t*33),
                     (unsigned char)(88-t*58),
                     255};
            curSky.skyBottom =
                
                    {(unsigned char)(28-t*8),
                     (unsigned char)(23-t*8),
                     (unsigned char)(58-t*28),
                     255};
            curSky.waterDeep =
                {10,(unsigned char)(28+(1.0f-t)*28),(unsigned char)(68+(1.0f-t)*28),255};
            curSky.waterShallow =
                {18,(unsigned char)(48+(1.0f-t)*28),(unsigned char)(88+(1.0f-t)*18),255};
            nightness=t; moonAlpha=0.5f+t*0.5f; sunAlpha=0;
        }

        if (ColorsChanged(curSky,lastSky)) {
            BakeBackground(bgTex,curSky,horizonY);
            lastSky=curSky;
        }

        // ---- Audio streaming ----
        if (audio.ready) {
            if (IsAudioStreamProcessed(audio.windStream)) {
                FillWindBuffer(windBuf,AUDIO_BUFSIZE,audio.windPhase,0.18f);
                UpdateAudioStream(audio.windStream,windBuf,AUDIO_BUFSIZE);
            }
            if (IsAudioStreamProcessed(audio.waveStream)) {
                float wv = 0.22f*(1.0f+nightness*0.3f);
                FillWaveBuffer(waveBuf,AUDIO_BUFSIZE,audio.wavePhase,wv);
                UpdateAudioStream(audio.waveStream,waveBuf,AUDIO_BUFSIZE);
            }
        }

        // ---- Input & physics ----
        float accel=455.0f, friction=0.87f;
        if (IsKeyDown(KEY_LEFT) ||IsKeyDown(KEY_A)) raftVX-=accel*dt;
        if (IsKeyDown(KEY_RIGHT)||IsKeyDown(KEY_D)) raftVX+=accel*dt;
        if (IsKeyDown(KEY_UP)   ||IsKeyDown(KEY_W)) raftVY-=accel*dt;
        if (IsKeyDown(KEY_DOWN) ||IsKeyDown(KEY_S)) raftVY+=accel*dt;
        raftVX*=friction; raftVY*=friction;
        raftX+=raftVX*dt; raftY+=raftVY*dt;
        raftX=Clamp(raftX,85.0f,(float)(SCREEN_W-85));
        raftY=Clamp(raftY,RAFT_Y_MIN,RAFT_Y_MAX);
        raftBob=sinf(gameTime*1.8f)*5.0f;

        // ---- Paralaks pulau ----
        islandX-=(raftX-prevRaftX)*0.8f;
        if (islandX> SCREEN_W+300) islandX=-200;
        if (islandX<-300)          islandX= SCREEN_W+200;
        prevRaftX=raftX;

        // ---- Spawn ----
        if (spawnTimer>3.8f) { spawnTimer=0; spawnItem(true); }
        if (sharkTimer>7.0f&&sharks.size()<4) { sharkTimer=0; spawnShark(); }

        // ---- Update collectibles ----
        for (auto& c:items) {
            if (!c.alive) continue;
            if (c.collecting) {
                c.collectTimer+=dt*3.5f;
                if (c.collectTimer>=1.0f) c.alive=false;
                continue;
            }
            c.x+=c.velX; c.bobPhase+=dt*1.5f;
            if (c.x<-100) { c.alive=false; continue; }
            float bobOff=sinf(c.bobPhase)*8.0f;
            float ddx=c.x-raftX, ddy=(c.y+bobOff)-(raftY+raftBob);
            if (sqrtf(ddx*ddx+ddy*ddy)<68.0f) {
                c.collecting=true; c.collectTimer=0;
                score+=c.isBarrel?10:5;
                message=c.isBarrel
                    ? TextFormat("+10 Tong! [%d]",score)
                    : TextFormat("+5  Kayu! [%d]",score);
                msgTimer=2.0f;
                for (int pp=0;pp<10;pp++) {
                    CollectParticle cp;
                    cp.x=c.x+(float)(rand()%40-20);
                    cp.y=c.y+bobOff+(float)(rand()%20-10);
                    cp.vy=-(60.0f+rand()%80);
                    cp.life=cp.maxLife=0.6f+(float)(rand()%5)/10.0f;
                    cp.isBarrel=c.isBarrel;
                    particles.push_back(cp);
                }
            }
        }
        items.erase(
            std::remove_if(items.begin(),items.end(),
                [](const Collectible& c){ return !c.alive; }),
            items.end());

        // ---- Update partikel ----
        for (auto& p:particles) {
            p.y+=p.vy*dt; p.vy+=55.0f*dt; p.life-=dt;
        }
        particles.erase(
            std::remove_if(particles.begin(),particles.end(),
                [](const CollectParticle& p){ return p.life<=0; }),
            particles.end());

        // ---- Update sharks ----
        for (auto& s:sharks) {
            s.wavePhase+=dt;
            // facingLeft=true  → menghadap & bergerak ke kiri (x berkurang)
            // facingLeft=false → menghadap & bergerak ke kanan (x bertambah)
            float dir=s.facingLeft?1.0f:-1.0f;
            s.x-=dir*s.speed;
            if (invincible<=0) {
                float sy=s.y+sinf(s.wavePhase*2.0f)*6.0f;
                float ddx=s.x-raftX, ddy=sy-(raftY+raftBob);
                if (sqrtf(ddx*ddx+ddy*ddy)<72.0f) {
                    lives--; invincible=2.2f;
                    message=(lives>0)
                        ? TextFormat("Hiu! Nyawa: %d",lives)
                        : "Game Over! Mengulang...";
                    msgTimer=2.5f;
                    if (lives<=0) {
                        lives=3; score=0;
                        raftX=SCREEN_W*0.38f; raftY=SCREEN_H*0.56f;
                    }
                }
            }
        }
        sharks.erase(
            std::remove_if(sharks.begin(),sharks.end(),
                [](const Shark& s){ return s.x<-200||s.x>SCREEN_W+200; }),
            sharks.end());
        if (sharks.empty()) { sharkTimer=0; spawnShark(); }

        // ---- Update clouds ----
        for (auto& c:clouds) {
            c.x+=c.speed;
            if (c.x>SCREEN_W+200) c.x=-200;
        }

        // ================================================================
        // RENDER
        // ================================================================
        BeginDrawing();

        // Latar
        if (g_wireframe) {
            ClearBackground({5,8,15,255});
            Color gc={0,60,80,70};
            for (int y=horizonY; y<SCREEN_H; y+=30)
                DrawBresenhamLine(0,y,SCREEN_W,y,gc);
            for (int x=0; x<SCREEN_W; x+=60)
                DrawBresenhamLine(x,horizonY,x,SCREEN_H,gc);
            DrawBresenhamLine(0,horizonY,SCREEN_W,horizonY,
                {0,160,210,180});
        } else {
            DrawTextureRec(bgTex.texture,
                {0,0,(float)SCREEN_W,-(float)SCREEN_H},{0,0},WHITE);
        }

        if (nightness>0.05f||moonAlpha>0.3f)
            DrawStars(stars,fmaxf(nightness,moonAlpha-0.2f));

        if (sunAlpha>0.01f  && sunY <horizonY+35)
            DrawSun(sunX,sunY,sunAlpha);
        if (moonAlpha>0.01f && moonY<horizonY+35)
            DrawMoon(moonX,moonY,moonAlpha);

        for (auto& c:clouds) DrawCloud((int)c.x,(int)c.y, sunAlpha*0.85f+0.1f);

        DrawIsland((int)islandX,(int)(SCREEN_H*0.57f));

        // Ombak
        Color wL={255,255,255,56}, wM={198,226,255,72};
        for (int row=0;row<20;row++) {
            float bY=horizonY+row*27.0f+8;
            float amp=4.0f+row*1.2f;
            float freq=0.015f-row*0.0003f;
            float spd=1.5f+row*0.1f;
            float px=0, py=WaveY(0,waveOffset,bY,amp,freq,spd);
            for (int x=5;x<=SCREEN_W;x+=5) {
                float cy2=WaveY((float)x,waveOffset,bY,amp,freq,spd);
                DrawDDALine(px,py,(float)x,cy2,(row%2==0)?wL:wM);
                px=(float)x; py=cy2;
            }
        }
        for (int x=0;x<SCREEN_W;x++) {
            float wy=WaveY((float)x,waveOffset,(float)horizonY,5.f,0.02f,2.f);
            DrawPixel(x,(int)wy,{255,255,255,88});
            DrawPixel(x,(int)wy+1,{198,226,255,48});
        }

        // Collectibles
        for (auto& c:items) {
            if (!c.alive) continue;
            float bobY=sinf(c.bobPhase)*8.0f, dY=c.y+bobY;
            if (c.collecting) {
                float sc=1.0f-c.collectTimer, al=1.0f-c.collectTimer;
                int sz=(int)(40.0f*sc); if(sz<2)sz=2;
                Color fc=c.isBarrel
                    ? Color{255,200,70,(unsigned char)(255*al)}
                    : Color{160,255,120,(unsigned char)(255*al)};
                DrawCircle((int)c.x,(int)dY,sz,fc,fc);
                DrawBresenhamCircleOutline((int)c.x,(int)dY,sz,
                    {255,255,255,(unsigned char)(200*al)});
            } else {
                if (c.isBarrel) DrawBarrel(c.x,dY);
                else            DrawWoodLog(c.x,dY,sinf(c.bobPhase)*0.3f);
                float pulse=0.5f+0.5f*sinf(gameTime*4.0f);
                DrawBresenhamCircleOutline((int)c.x,(int)dY,34,
                    {255,255,100,(unsigned char)(50+60*pulse)});
            }
        }

        // Partikel
        for (auto& p:particles) {
            float al=p.life/p.maxLife;
            int sz=(int)(8*al); if(sz<1)sz=1;
            Color pc=p.isBarrel
                ? Color{255,215,75,(unsigned char)(220*al)}
                : Color{155,240,115,(unsigned char)(220*al)};
            DrawRectangle((int)p.x-sz/2,(int)p.y-sz/2,sz,sz,pc);
        }

        // Sharks
        for (auto& s:sharks)
            DrawShark(s.x, s.y+sinf(s.wavePhase*2.0f)*6.0f, s.facingLeft);

        // Rakit (flash saat invincible)
        bool showRaft=(invincible<=0)||((int)(invincible*10)%2==0);
        if (showRaft) DrawRaft((int)raftX,(int)(raftY+raftBob));

        // ---- HUD ----
        DrawRectangle(12,12,220,100,{0,0,0,145});
        DrawBresenhamLine(12, 12,232, 12,{100,200,255,200});
        DrawBresenhamLine(12,112,232,112,{100,200,255,200});
        DrawBresenhamLine(12, 12, 12,112,{100,200,255,200});
        DrawBresenhamLine(232,12,232,112,{100,200,255,200});

        DrawText(TextFormat("SKOR: %d",score),22,20,22,{255,230,80,255});
        DrawText("NYAWA:",22,52,18,{255,255,255,200});
        for (int i=0;i<3;i++) {
            Color hc=(i<lives)?Color{255,72,72,255}:Color{78,78,78,140};
            int hx=100+i*30, hy=58;
            DrawCircle(hx-4,hy-2,6,hc,hc);
            DrawCircle(hx+4,hy-2,6,hc,hc);
            DrawBresenhamThick(hx-10,hy+2,hx,  hy+14,hc,3);
            DrawBresenhamThick(hx+10,hy+2,hx,  hy+14,hc,3);
        }
        int hours  =(int)(dayTime*100.0f);
        int minutes=(int)((dayTime*100.0f-hours)*60.0f);
        if (hours>=24) hours=0;
        DrawText(TextFormat("WAKTU: %02d:%02d",hours,minutes),
                 22,82,16,{200,226,255,255});

        // Kontrol + wireframe status
        DrawRectangle(SCREEN_W-235,12,223,82,{0,0,0,145});
        DrawText("WASD/Arrow: Gerak", SCREEN_W-230,18,14,{200,200,200,210});
        DrawText("Hindari Hiu!",      SCREEN_W-230,38,14,{255,115,115,210});
        DrawText("Ambil Tong & Kayu", SCREEN_W-230,58,14,{100,255,175,210});
        const char* wfLabel = g_wireframe
            ? "[TAB] WIREFRAME ON"
            : "[TAB] WIREFRAME OFF";
        Color wfLabelCol = g_wireframe
            ? Color{100,255,200,230}
            : Color{160,160,160,180};
        DrawText(wfLabel, SCREEN_W-230,76,12,wfLabelCol);

        DrawText(TextFormat("FPS: %d",GetFPS()),
                 SCREEN_W/2-30,14,16,{180,255,180,180});

        // Popup
        if (msgTimer>0 && !message.empty()) {
            float a=fminf(1.0f,msgTimer);
            int mw=MeasureText(message.c_str(),22), mx=SCREEN_W/2-mw/2;
            DrawRectangle(mx-14,SCREEN_H/2-30,mw+28,48,
                {0,0,0,(unsigned char)(155*a)});
            DrawBresenhamLine(mx-14,SCREEN_H/2-30,mx+mw+14,SCREEN_H/2-30,
                {255,240,100,(unsigned char)(200*a)});
            DrawBresenhamLine(mx-14,SCREEN_H/2+18,mx+mw+14,SCREEN_H/2+18,
                {255,240,100,(unsigned char)(200*a)});
            DrawText(message.c_str(),mx,SCREEN_H/2-18,22,
                {255,240,100,(unsigned char)(255*a)});
        }

        DrawText("RAKIT DI LAUT",
                 SCREEN_W/2-88,SCREEN_H-26,18,{198,226,255,135});
        EndDrawing();
    }

    // ---- Cleanup ----
    if (audio.ready) {
        StopAudioStream(audio.windStream);
        StopAudioStream(audio.waveStream);
        UnloadAudioStream(audio.windStream);
        UnloadAudioStream(audio.waveStream);
    }
    CloseAudioDevice();
    UnloadRenderTexture(bgTex);
    CloseWindow();
    return 0;
}
