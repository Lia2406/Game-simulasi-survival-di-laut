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
// AUDIO — PCM synthesis
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

static void FillWindBuffer(short* buf, int count, float& phase, float vol) {
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

static void FillWaveBuffer(short* buf, int count, float& phase, float vol) {
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
// WIREFRAME TOGGLE
// ================================================================
static bool g_wireframe = false;

// ================================================================
// BRESENHAM LINE (dari bresenham.c Anda)
// ================================================================
void BresenhamLine(int x1, int y1, int x2, int y2, Color color) {
    int dx = abs(x2-x1), dy = abs(y2-y1);
    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx - dy;
    while (1) {
        DrawPixel(x1, y1, color);
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2*err;
        if (e2 > -dy) { err -= dy; x1 += sx; }
        if (e2 <  dx) { err += dx; y1 += sy; }
    }
}

void Bres_ThickLine(int x1, int y1, int x2, int y2, Color color, int thick) {
    float dx = (float)(x2-x1), dy = (float)(y2-y1);
    float len = sqrtf(dx*dx + dy*dy);
    if (len == 0) return;
    float px = -dy/len, py = dx/len;
    int half = thick/2;
    for (int t = -half; t <= half; t++) {
        int ox = (int)roundf(px*t), oy = (int)roundf(py*t);
        BresenhamLine(x1+ox, y1+oy, x2+ox, y2+oy, color);
    }
}

// ================================================================
// DDA LINE (dari dda.c Anda)
// ================================================================
void DDALine(int x1, int y1, int x2, int y2, Color color) {
    int dx = x2 - x1, dy = y2 - y1;
    int steps = (abs(dx) > abs(dy)) ? abs(dx) : abs(dy);
    if (steps == 0) { DrawPixel(x1, y1, color); return; }
    float xInc = (float)dx / steps;
    float yInc = (float)dy / steps;
    float x = (float)x1, y = (float)y1;
    for (int i = 0; i <= steps; i++) {
        DrawPixel((int)roundf(x), (int)roundf(y), color);
        x += xInc; y += yInc;
    }
}

void DDA_ThickLine(int x1, int y1, int x2, int y2, Color color, int thick) {
    float dx = (float)(x2-x1), dy = (float)(y2-y1);
    float len = sqrtf(dx*dx + dy*dy);
    if (len == 0) return;
    float px = -dy/len, py = dx/len;
    int half = thick/2;
    for (int t = -half; t <= half; t++) {
        int ox = (int)roundf(px*t), oy = (int)roundf(py*t);
        DDALine(x1+ox, y1+oy, x2+ox, y2+oy, color);
    }
}

// ================================================================
// MIDPOINT CIRCLE (menggantikan DrawBresenhamCircleOutline)
// Algoritma Midpoint Circle Standard
// ================================================================
void MidpointCircleOutline(int cx, int cy, int r, Color c) {
    int x = 0, y = r;
    int d = 1 - r;
    auto plot8 = [&](int px, int py) {
        DrawPixel(cx+px, cy+py, c); DrawPixel(cx-px, cy+py, c);
        DrawPixel(cx+px, cy-py, c); DrawPixel(cx-px, cy-py, c);
        DrawPixel(cx+py, cy+px, c); DrawPixel(cx-py, cy+px, c);
        DrawPixel(cx+py, cy-px, c); DrawPixel(cx-py, cy-px, c);
    };
    plot8(x, y);
    while (x < y) {
        x++;
        if (d < 0) {
            d += 2*x + 1;
        } else {
            y--;
            d += 2*(x - y) + 1;
        }
        plot8(x, y);
    }
}

// Fill circle dengan scanline (solid)
void MidpointCircleFill(int cx, int cy, int r, Color fill) {
    int x = 0, y = r;
    int d = 1 - r;
    auto hline = [&](int lx, int ly, int len) {
        if (len > 0) DrawRectangle(lx, ly, len, 1, fill);
    };
    hline(cx - r, cy, 2*r + 1);
    while (x < y) {
        x++;
        if (d < 0) {
            d += 2*x + 1;
        } else {
            y--;
            d += 2*(x - y) + 1;
        }
        hline(cx - x, cy + y, 2*x + 1);
        hline(cx - x, cy - y, 2*x + 1);
        hline(cx - y, cy + x, 2*y + 1);
        hline(cx - y, cy - x, 2*y + 1);
    }
}

// DrawCircle helper: wireframe → outline, fill → solid
void DrawCircleMid(int cx, int cy, int r, Color fill, Color outline) {
    if (g_wireframe) {
        MidpointCircleOutline(cx, cy, r, outline);
    } else {
        MidpointCircleFill(cx, cy, r, fill);
    }
}

void DrawSpan(int x, int y, int w, Color fill) {
    if (!g_wireframe && w > 0) DrawRectangle(x, y, w, 1, fill);
}

// ================================================================
// BACKGROUND GRADIENT
// ================================================================
struct SkyColors {
    Color skyTop, skyBottom, waterDeep, waterShallow;
};

bool ColorsChanged(const SkyColors& a, const SkyColors& b) {
    auto d = [](Color x, Color y) {
        return abs(x.r-y.r)+abs(x.g-y.g)+abs(x.b-y.b) > 8;
    };
    return d(a.skyTop,b.skyTop) || d(a.skyBottom,b.skyBottom)
        || d(a.waterDeep,b.waterDeep) || d(a.waterShallow,b.waterShallow);
}

void BakeBackground(RenderTexture2D& rt, const SkyColors& sc, int hY) {
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

float WaveY(float x, float t, float bY, float amp, float freq, float spd) {
    return bY + sinf(x*freq + t*spd)*amp + sinf(x*freq*0.7f + t*spd*1.3f)*amp*0.4f;
}

// ================================================================
// DRAW — LOGO POLBAN (melayang di langit)
// Struktur logo: persegi biru di kanan-bawah, 3 strip oranye diagonal,
// lingkaran putih (di tengah-kiri), dan "C" biru besar.
// ================================================================
// ================================================================
// DRAW — LOGO POLBAN
// Bentuk: Segi enam reguler
// Atas  : Oranye + 3 strip diagonal miring kanan-atas
// Bawah : Biru, bentuk "U" / baut (dipotong garis diagonal dari kiri-bawah)
// Semua menggunakan Bresenham scanline fill + Midpoint circle outline
// ================================================================
// ================================================================
// DRAW — LOGO POLBAN (Revisi Segienam Presisi)
// Bentuk: Segienam reguler. 
// Atas  : 3 Strip Oranye diagonal.
// Bawah : Biru bentuk "baut/U" dengan lubang tembus pandang.
// ================================================================
void DrawPolbanLogo(int cx, int cy, float scale, float alpha) {
    Color orange = {240, 130,  20, (unsigned char)(255*alpha)};
    Color blue   = { 20,  40, 160, (unsigned char)(255*alpha)};
    Color wfCol  = {255, 220,  80, (unsigned char)(200*alpha)};

    // Ukuran dasar (Bisa diperbesar dari pemanggilan fungsi scale di main)
    float R = 28.0f * scale; 

    // 1. Hitung 6 titik sudut Segienam (Flat-top)
    int hx[6], hy[6];
    for (int i = 0; i < 6; i++) {
        float angle = i * (PI / 3.0f) - (PI / 6.0f);
        hx[i] = cx + (int)(R * cosf(angle));
        hy[i] = cy + (int)(R * sinf(angle));
    }

    // Cari batas atas dan bawah segienam untuk di-scan
    int yMin = hy[0], yMax = hy[0];
    for (int i = 1; i < 6; i++) {
        if (hy[i] < yMin) yMin = hy[i];
        if (hy[i] > yMax) yMax = hy[i];
    }

    // Kemiringan pemisah warna dan strip oranye (sekitar 30 derajat)
    float slope = -0.577f; 

    // 2. SCANLINE FILL (Isi warna baris demi baris)
    for (int y = yMin; y <= yMax; y++) {
        // Cari titik potong baris Y ini dengan tepi segienam
        float xInt[6]; 
        int n = 0;
        for (int i = 0; i < 6; i++) {
            int j = (i + 1) % 6;
            if ((hy[i] <= y && y < hy[j]) || (hy[j] <= y && y < hy[i])) {
                float t = (float)(y - hy[i]) / (float)(hy[j] - hy[i]);
                xInt[n++] = hx[i] + t * (hx[j] - hx[i]);
            }
        }
        
        // Jika ada 2 titik potong (kiri dan kanan), isi piksel di antaranya
        if (n >= 2) {
            if (xInt[0] > xInt[1]) { float tmp = xInt[0]; xInt[0] = xInt[1]; xInt[1] = tmp; }
            int xL = (int)xInt[0];
            int xR = (int)xInt[1];

            for (int x = xL; x <= xR; x++) {
                // Rumus garis miring pemisah (Biru & Oranye)
                float ySplit = cy + slope * (x - cx) + (R * 0.15f); 

                if (y < ySplit) {
                    // --- AREA ORANYE (3 STRIP) ---
                    // Hitung jarak tegak lurus piksel ke arah kemiringan untuk membuat jarak strip
                    float dist = (x - cx) * (-slope) + (y - cy) * 1.0f;
                    float pitch = R * 0.65f; // Jarak antar pengulangan
                    float stripeW = R * 0.45f; // Ketebalan strip oranye

                    // Jika masuk dalam area ketebalan strip, warnai
                    float phase = fmodf(fabsf(dist + R * 2.0f), pitch);
                    if (phase < stripeW) {
                        if (!g_wireframe) DrawPixel(x, y, orange);
                    }
                } else {
                    // --- AREA BIRU (BAUT U) ---
                    // Hitung jarak piksel ke pusat untuk membuat lubang tembus pandang
                    float dx = x - cx;
                    float dy = y - (cy + R * 0.1f);
                    float distSq = dx * dx + dy * dy;
                    float cutoutR = R * 0.52f; // Besarnya radius lubang

                    // Jika di luar radius lubang, warnai biru
                    if (distSq > cutoutR * cutoutR) {
                        if (!g_wireframe) DrawPixel(x, y, blue);
                    }
                }
            }
        }
    }

    // 3. GAMBAR WIREFRAME (Hanya Garis Tepi)
    if (g_wireframe) {
        for (int i = 0; i < 6; i++) BresenhamLine(hx[i], hy[i], hx[(i + 1) % 6], hy[(i + 1) % 6], wfCol);
        MidpointCircleOutline(cx, (int)(cy + R * 0.1f), (int)(R * 0.52f), {100,200,255,200});
    }
}

// ================================================================
// DRAW — MATAHARI (menggunakan Midpoint Circle + DDA rays)
// ================================================================
void DrawSun(int cx, int cy, float alpha) {
    Color glow  = {255,240,100, (unsigned char)(55*alpha)};
    Color sunC  = {255,210, 50, (unsigned char)(255*alpha)};
    Color rayC  = {255,220, 80, (unsigned char)(175*alpha)};
    Color sunOut= {255,180, 30, (unsigned char)(255*alpha)};

    // Lingkaran glow luar (Midpoint)
    if (!g_wireframe) {
        MidpointCircleFill(cx, cy, 44, glow);
        MidpointCircleFill(cx, cy, 30, sunC);
    } else {
        MidpointCircleOutline(cx, cy, 30, sunOut);
    }

    // Sinar menggunakan DDA
    for (int i = 0; i < 12; i++) {
        float a = i/12.0f*2.0f*PI;
        DDA_ThickLine(
            (int)(cx+38*cosf(a)), (int)(cy+38*sinf(a)),
            (int)(cx+54*cosf(a)), (int)(cy+54*sinf(a)), rayC, 2);
    }
}

// ================================================================
// DRAW — BULAN (Midpoint circle)
// ================================================================
void DrawMoon(int cx, int cy, float alpha) {
    Color moonC  = {220,230,255,(unsigned char)(255*alpha)};
    Color shadow = { 14, 23, 54,(unsigned char)(255*alpha)};
    Color starC  = {255,255,210,(unsigned char)(195*alpha)};

    if (!g_wireframe) {
        MidpointCircleFill(cx,   cy,   22, moonC);
        MidpointCircleFill(cx+7, cy-4, 18, shadow);
    } else {
        MidpointCircleOutline(cx,   cy,   22, moonC);
        MidpointCircleOutline(cx+7, cy-4, 18, shadow);
    }
    // Bintang kecil pakai DDA
    int sp[][2] = {{45,25},{-55,18},{75,-38},{-28,-55},{58,55}};
    for (auto& s : sp) {
        DDALine(cx+s[0]-4, cy+s[1],   cx+s[0]+4, cy+s[1],   starC);
        DDALine(cx+s[0],   cy+s[1]-4, cx+s[0],   cy+s[1]+4, starC);
    }
}

// ================================================================
// DRAW — BINTANG
// ================================================================
void DrawStars(std::vector<Vector2>& stars, float alpha) {
    if (alpha < 0.02f) return;
    Color sc  = {255,255,220,(unsigned char)(255*alpha)};
    Color sc2 = {200,200,180,(unsigned char)(140*alpha)};
    for (int i = 0; i < (int)stars.size(); i++) {
        int sx=(int)stars[i].x, sy=(int)stars[i].y;
        DrawPixel(sx, sy, sc);
        if (i%7==0) {
            DrawPixel(sx-1,sy,sc2); DrawPixel(sx+1,sy,sc2);
            DrawPixel(sx,sy-1,sc2); DrawPixel(sx,sy+1,sc2);
        }
    }
}

// ================================================================
// DRAW — AWAN (Midpoint Circle)
// ================================================================
void DrawCloud(int cx, int cy, float alpha) {
    Color c1 = {255,255,255,(unsigned char)(188*alpha)};
    Color c2 = {208,218,234,(unsigned char)(138*alpha)};
    Color wf = {180,200,230,(unsigned char)(200*alpha)};

    if (g_wireframe) {
        MidpointCircleOutline(cx,    cy+6,  23, wf);
        MidpointCircleOutline(cx-26, cy+11, 19, wf);
        MidpointCircleOutline(cx,    cy+6,  27, wf);
        MidpointCircleOutline(cx+28, cy+11, 19, wf);
        MidpointCircleOutline(cx+10, cy-10, 17, wf);
        MidpointCircleOutline(cx-12, cy-6,  16, wf);
    } else {
        MidpointCircleFill(cx,    cy+6,  23, c2);
        MidpointCircleFill(cx-26, cy+11, 19, c1);
        MidpointCircleFill(cx,    cy+6,  27, c1);
        MidpointCircleFill(cx+28, cy+11, 19, c1);
        MidpointCircleFill(cx+10, cy-10, 17, c1);
        MidpointCircleFill(cx-12, cy-6,  16, c1);
    }
}

// ================================================================
// DRAW — PULAU
// ================================================================
void DrawIsland(int cx, int cy) {
    Color sandD = {173,146, 76,255}, sandL = {213,188,116,255};
    Color grass = { 56,126, 46,255}, trunk = {116, 76, 36,255};
    Color leaf  = { 36,156, 56,255}, leafD = { 16,106, 36,255};
    Color coco  = { 96, 66, 26,255};
    Color wf    = {100,200,100,200};

    int lx=cx+25, ly=cy-108;
    int fr[][2]={{-52,-18},{-32,-44},{0,-54},{32,-44},{56,-14},{50,22}};

    if (g_wireframe) {
        for (int i=0; i<82; i+=6) {
            int w=(int)(87.0f*sinf((float)i/82.0f*PI));
            DrawPixel(cx-w, cy-i+42, wf);
            DrawPixel(cx+w, cy-i+42, wf);
        }
        DDALine(cx, cy-36, lx, ly, trunk);
        for (auto& f:fr) BresenhamLine(lx,ly, lx+f[0],ly+f[1], wf);
        MidpointCircleOutline(lx-12,ly+8, 7, wf);
        MidpointCircleOutline(lx+5, ly+12,6, wf);
        MidpointCircleOutline(lx+18,ly+5, 7, wf);
    } else {
        for (int i=0; i<82; i++) {
            int w=(int)(87.0f*sinf((float)i/82.0f*PI));
            Color c=(i<15)?sandD:(i<52?sandL:grass);
            DrawRectangle(cx-w,cy-i+42,w*2,1,c);
        }
        for (int k=0;k<4;k++)
            DDA_ThickLine(cx+k*2, cy-36, lx+k*2, ly, trunk, 5);
        for (auto& f:fr) {
            Bres_ThickLine(lx,ly, lx+f[0],ly+f[1], leaf, 3);
            Bres_ThickLine(lx,ly, lx+f[0]/2,ly+f[1]/2-10, leafD, 2);
        }
        MidpointCircleFill(lx-12,ly+8, 7, coco);
        MidpointCircleFill(lx+5, ly+12,6, coco);
        MidpointCircleFill(lx+18,ly+5, 7, coco);
    }
}

// ================================================================
// DRAW — ORANG DI ATAS RAKIT
// Menggunakan Midpoint Circle (kepala) + Bresenham/DDA (tubuh/lengan/kaki)
// ================================================================
void DrawPersonOnRaft(int cx, int cy, float gameTime) {
    // Animasi ayun tangan sederhana
    float armSwing = sinf(gameTime*2.0f) * 0.3f;
    float legSwing = sinf(gameTime*2.0f) * 0.25f;

    Color skinC  = {230,185,130,255};  // warna kulit
    Color shirtC = {255, 60, 60,255};  // baju merah
    Color pantC  = {40,  60,120,255};  // celana biru
    Color hairC  = {50,  30, 15,255};  // rambut
    Color shoeC  = {40,  20, 10,255};  // sepatu
    Color wfCol  = {255,200,150,200};

    int headR = 10;
    int headX = cx;
    int headY = cy - 58;

    // -- Kepala (Midpoint Circle) --
    if (!g_wireframe) {
        MidpointCircleFill(headX, headY, headR, skinC);
        // Rambut (setengah lingkaran atas)
        for (int dy2 = -headR; dy2 <= -2; dy2++) {
            int hw = (int)(headR * sqrtf(fmaxf(0.f,
                1.f - (float)(dy2*dy2)/(headR*headR))));
            DrawRectangle(headX-hw, headY+dy2, hw*2+1, 1, hairC);
        }
        // Mata (pixel)
        DrawPixel(headX-4, headY-2, {20,20,20,255});
        DrawPixel(headX+4, headY-2, {20,20,20,255});
        // Senyum (DDA)
        DDALine(headX-3, headY+4, headX,   headY+6, {20,20,20,255});
        DDALine(headX,   headY+6, headX+3, headY+4, {20,20,20,255});
    } else {
        MidpointCircleOutline(headX, headY, headR, wfCol);
    }

    // -- Leher --
    if (!g_wireframe) {
        DrawRectangle(headX-3, headY+headR, 6, 6, skinC);
    } else {
        BresenhamLine(headX-3,headY+headR, headX-3,headY+headR+5, wfCol);
        BresenhamLine(headX+3,headY+headR, headX+3,headY+headR+5, wfCol);
    }

    // -- Badan (baju) --
    int bodyTop = headY + headR + 5;
    int bodyBot = cy - 20;
    int bodyW   = 12;

    if (!g_wireframe) {
        for (int row = bodyTop; row <= bodyBot; row++) {
            float t2 = (float)(row-bodyTop)/(bodyBot-bodyTop);
            int w2 = bodyW + (int)(t2 * 3);
            DrawRectangle(cx-w2, row, w2*2, 1, shirtC);
        }
    } else {
        BresenhamLine(cx-bodyW, bodyTop, cx-bodyW-3, bodyBot, wfCol);
        BresenhamLine(cx+bodyW, bodyTop, cx+bodyW+3, bodyBot, wfCol);
        BresenhamLine(cx-bodyW, bodyTop, cx+bodyW, bodyTop, wfCol);
    }

    // -- Lengan kiri (Bresenham) --
    {
        float ang = -0.8f + armSwing;
        int ax1 = cx - bodyW;
        int ay1 = bodyTop + 8;
        int ax2 = ax1 - (int)(22*cosf(ang+0.3f));
        int ay2 = ay1 + (int)(22*sinf(ang+0.3f));
        if (!g_wireframe) {
            Bres_ThickLine(ax1,ay1, ax2,ay2, shirtC, 5);
            MidpointCircleFill(ax2, ay2, 4, skinC); // tangan
        } else {
            BresenhamLine(ax1,ay1, ax2,ay2, wfCol);
        }
    }

    // -- Lengan kanan (DDA) --
    {
        float ang = 0.8f - armSwing;
        int ax1 = cx + bodyW;
        int ay1 = bodyTop + 8;
        int ax2 = ax1 + (int)(22*cosf(ang-0.3f));
        int ay2 = ay1 + (int)(22*sinf(ang-0.3f));
        if (!g_wireframe) {
            DDA_ThickLine(ax1,ay1, ax2,ay2, shirtC, 5);
            MidpointCircleFill(ax2, ay2, 4, skinC);
        } else {
            DDALine(ax1,ay1, ax2,ay2, wfCol);
        }
    }

    // -- Celana --
    int pantTop = bodyBot;
    int pantBot = cy - 4;

    if (!g_wireframe) {
        // kaki kiri
        int legOff = (int)(8*sinf(legSwing));
        for (int row = pantTop; row <= pantBot; row++) {
            DrawRectangle(cx-10, row+legOff, 9, 1, pantC);
        }
        // kaki kanan
        for (int row = pantTop; row <= pantBot; row++) {
            DrawRectangle(cx+2, row-legOff, 9, 1, pantC);
        }
        // Sepatu
        MidpointCircleFill(cx-6, pantBot+4, 5, shoeC);
        MidpointCircleFill(cx+6, pantBot+4, 5, shoeC);
    } else {
        BresenhamLine(cx-10, pantTop, cx-10, pantBot, wfCol);
        BresenhamLine(cx+10, pantTop, cx+10, pantBot, wfCol);
    }
}

// ================================================================
// DRAW — RAKIT
// ================================================================
void DrawRaft(int cx, int cy) {
    Color logB  = {137, 88, 42,255}, logD = {98, 58, 24,255};
    Color logL  = {178,128, 68,255}, rope = {196,166, 96,255};
    Color sail  = {236,216,166,255}, sailW= {216,190,136,200};
    Color sailLn= {176,146, 96,255}, pole = {116, 76, 36,255};
    Color wf    = {200,180,100,220};

    int sx0=cx+5,sy0=cy-92, sx1=cx+68,sy1=cy-38, sx2=cx+5,sy2=cy-10;

    if (g_wireframe) {
        for (int i=0; i<5; i++) {
            int ly=cy-20+i*12;
            BresenhamLine(cx-45,ly,    cx+45,ly,    wf);
            BresenhamLine(cx-45,ly+10, cx+45,ly+10, wf);
            BresenhamLine(cx-45,ly,    cx-45,ly+10, wf);
            BresenhamLine(cx+45,ly,    cx+45,ly+10, wf);
        }
        int rxs[]={cx-28,cx,cx+28};
        for (int rx:rxs) BresenhamLine(rx,cy-20, rx,cy+40, rope);
        BresenhamLine(cx+5,cy+38, cx+5,cy-92, pole);
        BresenhamLine(sx0,sy0, sx1,sy1, wf);
        BresenhamLine(sx1,sy1, sx2,sy2, wf);
        BresenhamLine(sx2,sy2, sx0,sy0, wf);
    } else {
        for (int i=0; i<5; i++) {
            int ly=cy-20+i*12;
            DrawRectangle(cx-45,ly,   90,10,logB);
            DrawRectangle(cx-45,ly,   90, 2,logD);
            DrawRectangle(cx-45,ly+7, 90, 3,logD);
            BresenhamLine(cx-45,ly+4, cx+45,ly+4, logL);
            MidpointCircleFill(cx-45,ly+5, 5, logD);
            MidpointCircleFill(cx+45,ly+5, 5, logD);
        }
        int rxs[]={cx-28,cx,cx+28};
        for (int rx:rxs)
            DDA_ThickLine(rx, cy-20, rx, cy+40, rope, 2);
        Bres_ThickLine(cx+5,cy+38, cx+5,cy-92, pole, 4);
        for (int scanY=sy0; scanY<=sy2; scanY++) {
            int lx2=sx0;
            int rx2;
            float t2;
            if (scanY <= sy1) {
                t2 = (float)(scanY-sy0)/(sy1-sy0+1);
                rx2 = sx0+(int)(t2*(sx1-sx0));
            } else {
                t2 = (float)(scanY-sy1)/(sy2-sy1+1);
                rx2 = sx1+(int)(t2*(sx2-sx1));
            }
            Color sc=((scanY-sy0)%14<3)?sailW:sail;
            DrawSpan(lx2,scanY,rx2-lx2,sc);
        }
        Bres_ThickLine(sx0,sy0, sx1,sy1, sailLn, 2);
        Bres_ThickLine(sx1,sy1, sx2,sy2, sailLn, 2);
        Bres_ThickLine(sx2,sy2, sx0,sy0, sailLn, 1);
        DDALine(sx0, sy0, cx+5, cy+38, rope);
    }
}

// ================================================================
// DRAW — HIU
// ================================================================
void DrawShark(float cx, float cy, bool facingLeft) {
    Color body  = { 76, 96,116,255}, belly = {196,208,216,255};
    Color fin   = { 56, 76, 96,255}, eyeC  = {  6,  6,  6,255};
    Color teeth = {236,236,236,255}, gill  = { 46, 66, 86,180};
    Color wf    = {100,160,220,220};

    int dir=facingLeft?-1:1;
    int bx=(int)cx, by=(int)cy, bw=60, bh=22;

    if (g_wireframe) {
        for (int dy2=-bh; dy2<=bh; dy2+=3) {
            float r=(float)bw*sqrtf(fmaxf(0.f,
                1.f-(float)(dy2*dy2)/(bh*bh)));
            DrawPixel(bx-(int)r, by+dy2, wf);
            DrawPixel(bx+(int)r, by+dy2, wf);
        }
        int tipX=bx+dir*15, tipY=by-bh-36;
        BresenhamLine(bx-dir*10,by-bh, tipX,tipY, wf);
        BresenhamLine(tipX,tipY, bx+dir*20,by-bh, wf);
        int tx=bx-dir*bw;
        BresenhamLine(tx,by, tx-dir*26,by-22, wf);
        BresenhamLine(tx,by, tx-dir*26,by+22, wf);
        MidpointCircleOutline(bx+dir*30, by-5, 4, wf);
        DDALine(bx+dir*(bw-5), by+8, bx+dir*(bw-25), by+8, wf);
    } else {
        for (int dy2=-bh; dy2<=bh; dy2++) {
            int hw=(int)(bw*sqrtf(fmaxf(0.f,
                1.f-(float)(dy2*dy2)/(bh*bh))));
            DrawRectangle(bx-hw,by+dy2,hw*2,1,(dy2>6)?belly:body);
        }
        int tipX=bx+dir*15, tipY=by-bh-36;
        Bres_ThickLine(bx-dir*10,by-bh, bx+dir*20,by-bh, fin, 3);
        Bres_ThickLine(bx-dir*10,by-bh, tipX,tipY, fin, 3);
        Bres_ThickLine(tipX,tipY, bx+dir*20,by-bh, fin, 2);
        int tx=bx-dir*bw;
        Bres_ThickLine(tx,by-5, tx-dir*26,by-23, fin, 3);
        Bres_ThickLine(tx,by+5, tx-dir*26,by+23, fin, 3);
        BresenhamLine(tx-dir*26,by-23, tx-dir*26,by+23, fin);
        DDA_ThickLine(bx+dir*10, by+5, bx+dir*32, by+20, fin, 3);
        MidpointCircleFill(bx+dir*30, by-5, 5, body);
        MidpointCircleFill(bx+dir*30, by-5, 3, eyeC);
        DrawPixel(bx+dir*31, by-6, WHITE);
        int mx=bx+dir*bw-dir*5;
        for (int t=0;t<4;t++) {
            int tx2=mx-dir*t*7;
            DDALine(tx2, by+8, tx2+dir*3, by+12, teeth);
            DDALine(tx2+dir*3, by+12, tx2+dir*6, by+8, teeth);
        }
        for (int g=0;g<3;g++)
            BresenhamLine(bx+dir*(20-g*8),by-10,
                          bx+dir*(20-g*8)-dir*3,by+12, gill);
    }
}

// ================================================================
// DRAW — TONG (Barrel) — Midpoint circles
// ================================================================
void DrawBarrel(float cx, float cy, float scale=1.0f) {
    Color stave={126,76,36,255}, hoop={76,56,26,255};
    Color lid  ={156,106,56,255}, wf  ={200,140,80,220};

    int bx=(int)cx, by=(int)cy;
    int bw=(int)(18*scale), bh=(int)(28*scale);

    if (g_wireframe) {
        BresenhamLine(bx-bw,by-bh, bx+bw,by-bh, wf);
        BresenhamLine(bx-bw,by+bh, bx+bw,by+bh, wf);
        BresenhamLine(bx-bw,by-bh, bx-bw,by+bh, wf);
        BresenhamLine(bx+bw,by-bh, bx+bw,by+bh, wf);
        DDALine(bx-bw, by, bx+bw, by, wf);
    } else {
        for (int s=-2;s<=2;s++) {
            int sx=bx+s*(int)(7*scale);
            int bulge=(int)(3*scale*sinf((float)(s+2)/4.0f*PI));
            DrawRectangle(sx-bulge-2,by-bh,(int)(5*scale)+bulge*2,bh*2,stave);
        }
        int hys[]={(int)(by-bh+(int)(5*scale)),by,(int)(by+bh-(int)(5*scale))};
        for (int hy:hys)
            DDA_ThickLine(bx-bw, hy, bx+bw, hy, hoop, 3);
        DrawRectangle(bx-bw+2,by-bh,bw*2-4,(int)(4*scale),lid);
    }
}

// ================================================================
// DRAW — KAYU LOG — Midpoint circles di ujung
// ================================================================
void DrawWoodLog(float cx, float cy, float angle, float scale=1.0f) {
    Color logC={136,91,46,255}, logD={96,61,26,255};
    Color barkL={156,111,61,200}, wf={180,130,70,220};

    int bx=(int)cx, by=(int)cy;
    int len=(int)(35*scale);
    float cs=cosf(angle), sn=sinf(angle);

    if (g_wireframe) {
        BresenhamLine(
            bx-(int)(len*cs)-(int)(6*sn), by-(int)(len*sn)+(int)(6*cs),
            bx+(int)(len*cs)-(int)(6*sn), by+(int)(len*sn)+(int)(6*cs), wf);
        BresenhamLine(
            bx-(int)(len*cs)+(int)(6*sn), by-(int)(len*sn)-(int)(6*cs),
            bx+(int)(len*cs)+(int)(6*sn), by+(int)(len*sn)-(int)(6*cs), wf);
        MidpointCircleOutline(bx+(int)(len*cs),by+(int)(len*sn),(int)(5*scale),wf);
        MidpointCircleOutline(bx-(int)(len*cs),by-(int)(len*sn),(int)(5*scale),wf);
    } else {
        for (int t=(int)(-6*scale); t<=(int)(6*scale); t++) {
            float px0=-len*cs+t*(-sn), py0=-len*sn+t*cs;
            float px1= len*cs+t*(-sn), py1= len*sn+t*cs;
            int at=abs(t);
            Color c=(at>4)?logD:(at>2?logC:barkL);
            BresenhamLine(bx+(int)px0,by+(int)py0,
                          bx+(int)px1,by+(int)py1, c);
        }
        int ex0=bx+(int)(len*cs), ey0=by+(int)(len*sn);
        int ex1=bx-(int)(len*cs), ey1=by-(int)(len*sn);
        MidpointCircleFill(ex0,ey0,(int)(5*scale),{116,76,36,255});
        MidpointCircleFill(ex1,ey1,(int)(5*scale),{116,76,36,255});
        MidpointCircleOutline(ex0,ey0,(int)(5*scale),logD);
        MidpointCircleOutline(ex1,ey1,(int)(5*scale),logD);
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
// PANEL KONTROL (HUD kanan bawah)
// Pengatur: kecepatan awan, arah matahari/bulan, kecepatan benda langit
// ================================================================
void DrawControlPanel(
    int px, int py,
    float cloudSpeed,   float cloudSpeedMin, float cloudSpeedMax,
    float daySpeed,     float daySpeedMin,   float daySpeedMax,
    float sunOffsetX,
    bool  manualSun,
    int   horizonY
) {
    int panW = 270, panH = 215;
    Color panBg  = {0,0,0,175};
    Color border = {100,200,255,180};
    Color txtC   = {220,235,255,240};
    Color barBg  = {40,50,70,200};
    Color barFg  = {80,200,255,220};
    Color barFg2 = {255,200,80,220};
    Color barFg3 = {180,255,120,220};

    // Background panel
    DrawRectangle(px, py, panW, panH, panBg);
    BresenhamLine(px,py,       px+panW,py,       border);
    BresenhamLine(px,py+panH,  px+panW,py+panH,  border);
    BresenhamLine(px,py,       px,py+panH,        border);
    BresenhamLine(px+panW,py,  px+panW,py+panH,   border);

    // Judul
    DrawText("[ PANEL KONTROL ]", px+10, py+8, 14, {100,220,255,255});
    DDALine(px+5, py+25, px+panW-5, py+25, {80,160,200,150});

    // ---- Bar: Kecepatan Awan ----
    DrawText("Awan (Q/E):", px+10, py+32, 13, txtC);
    DrawRectangle(px+120, py+32, 130, 12, barBg);
    float rCloud = (cloudSpeed - cloudSpeedMin)/(cloudSpeedMax - cloudSpeedMin);
    DrawRectangle(px+120, py+32, (int)(130*rCloud), 12, barFg);
    BresenhamLine(px+120, py+32, px+250, py+32, border);
    BresenhamLine(px+120, py+44, px+250, py+44, border);
    DrawText(TextFormat("%.2f", cloudSpeed), px+255, py+31, 12, barFg);

    // ---- Bar: Kecepatan Waktu/Langit ----
    DrawText("Waktu (R/F):", px+10, py+54, 13, txtC);
    DrawRectangle(px+120, py+54, 130, 12, barBg);
    float rDay = (daySpeed - daySpeedMin)/(daySpeedMax - daySpeedMin);
    DrawRectangle(px+120, py+54, (int)(130*rDay), 12, barFg2);
    BresenhamLine(px+120, py+54, px+250, py+54, border);
    BresenhamLine(px+120, py+66, px+250, py+66, border);
    DrawText(TextFormat("%.4f", daySpeed), px+255, py+53, 11, barFg2);

    // ---- Kontrol manual matahari ----
    DrawText("Matahari:", px+10, py+76, 13, txtC);
    const char* modeLabel = manualSun ? "MANUAL [M]" : "AUTO   [M]";
    Color modeCol = manualSun ? Color{255,200,80,255} : Color{180,255,120,255};
    DrawText(modeLabel, px+120, py+76, 13, modeCol);

    if (manualSun) {
        DrawText("<Kiri/Kanan>", px+10, py+92, 12, {255,230,100,200});
        // Mini preview posisi matahari
        int miniW = 240, miniH = 22;
        int miniX = px+10, miniY = py+108;
        DrawRectangle(miniX, miniY, miniW, miniH, {10,20,50,200});
        BresenhamLine(miniX, miniY,       miniX+miniW, miniY,       {80,120,180,150});
        BresenhamLine(miniX, miniY+miniH, miniX+miniW, miniY+miniH, {80,120,180,150});
        // Posisi matahari di mini bar
        float sunRatio = (sunOffsetX + 80.0f) / (SCREEN_W + 160.0f);
        sunRatio = fmaxf(0.01f, fminf(0.99f, sunRatio));
        int sunDotX = miniX + (int)(miniW * sunRatio);
        int sunDotY = miniY + miniH/2;
        MidpointCircleFill(sunDotX, sunDotY, 7, {255,220,60,255});
        MidpointCircleOutline(sunDotX, sunDotY, 7, {255,180,30,255});
        DrawText(TextFormat("X=%.0f", sunOffsetX), px+10, py+133, 12, {255,220,80,200});
    } else {
        DrawText("(ikuti waktu otomatis)", px+10, py+92, 11, {160,180,200,180});
        // Bar posisi matahari auto
        float sunProg = sunOffsetX / (float)SCREEN_W;
        sunProg = fmaxf(0,fminf(1,sunProg));
        int miniW = 240, miniH = 16;
        int miniX = px+10, miniY = py+108;
        DrawRectangle(miniX, miniY, miniW, miniH, {10,20,50,200});
        BresenhamLine(miniX, miniY,       miniX+miniW, miniY,       {80,120,180,150});
        BresenhamLine(miniX, miniY+miniH, miniX+miniW, miniY+miniH, {80,120,180,150});
        int sdx = miniX + (int)(miniW * sunProg);
        int sdy = miniY + miniH/2;
        MidpointCircleFill(sdx, sdy, 6, {255,220,60,220});
    }

    // ---- Hotkey ringkasan ----
    DDALine(px+5, py+155, px+panW-5, py+155, {80,160,200,100});
    DrawText("TAB=Wireframe | WASD=Gerak", px+8, py+190, 12, {160,180,200,180});
    DrawText("Hindari Hiu | Kumpulkan Tong/Kayu", px+8, py+174, 11, {160,180,200,150});
}

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

    // ---- Kontrol benda langit ----
    float dayTime  = 0.08f;
    float daySpeed = 0.0022f;
    const float DAY_SPEED_MIN = 0.0001f;
    const float DAY_SPEED_MAX = 0.015f;

    // Kontrol manual matahari
    bool  manualSun   = false;
    float manualSunX  = (float)(SCREEN_W/2);  // posisi X manual
    float manualSunY  = (float)(horizonY - 80);

    // Kontrol kecepatan awan
    float cloudSpeedBase = 0.35f;
    const float CLOUD_SPEED_MIN = 0.05f;
    const float CLOUD_SPEED_MAX = 3.0f;

    // Logo Polban melayang
    float logoX    = SCREEN_W * 0.72f;
    float logoY    = 80.0f;
    float logoBobT = 0.0f;
    float logoAlpha= 1.0f;

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
                          cloudSpeedBase + (float)(rand()%12)/20.0f});

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
        gameTime+=dt; waveOffset+=dt; logoBobT+=dt;
        spawnTimer+=dt; sharkTimer+=dt;
        if (!manualSun) {
            dayTime += daySpeed * dt;
            if (dayTime >= 0.24f) dayTime -= 0.24f;
        }
        if (invincible>0) invincible-=dt;
        if (msgTimer>0)   msgTimer-=dt;

        // ---- Kontrol wireframe ----
        if (IsKeyPressed(KEY_TAB)) g_wireframe = !g_wireframe;

        // ---- Kontrol kecepatan awan (Q/E) ----
        if (IsKeyDown(KEY_Q)) {
            cloudSpeedBase -= 0.5f * dt;
            if (cloudSpeedBase < CLOUD_SPEED_MIN) cloudSpeedBase = CLOUD_SPEED_MIN;
        }
        if (IsKeyDown(KEY_E)) {
            cloudSpeedBase += 0.5f * dt;
            if (cloudSpeedBase > CLOUD_SPEED_MAX) cloudSpeedBase = CLOUD_SPEED_MAX;
        }

        // ---- Kontrol kecepatan waktu/langit (R/F) ----
        if (IsKeyDown(KEY_R)) {
            daySpeed += 0.001f * dt;
            if (daySpeed > DAY_SPEED_MAX) daySpeed = DAY_SPEED_MAX;
        }
        if (IsKeyDown(KEY_F)) {
            daySpeed -= 0.001f * dt;
            if (daySpeed < DAY_SPEED_MIN) daySpeed = DAY_SPEED_MIN;
        }

        // ---- Toggle mode manual matahari (M) ----
        if (IsKeyPressed(KEY_M)) {
            manualSun = !manualSun;
            // Sinkronisasi posisi saat beralih ke manual
            if (manualSun) {
                float sunProg = (dayTime-0.05f)/0.13f;
                manualSunX = -80.0f + sunProg*(SCREEN_W+160.0f);
                manualSunY = (float)(horizonY+40) - sinf(sunProg*PI)*(horizonY-20.0f);
            }
        }

        // ---- Kontrol manual posisi matahari (Kiri/Kanan/Atas/Bawah) ----
        if (manualSun) {
            float sunMoveSpeed = 180.0f;
            if (IsKeyDown(KEY_LEFT))  manualSunX -= sunMoveSpeed * dt;
            if (IsKeyDown(KEY_RIGHT)) manualSunX += sunMoveSpeed * dt;
            if (IsKeyDown(KEY_UP))    manualSunY -= sunMoveSpeed * dt;
            if (IsKeyDown(KEY_DOWN))  manualSunY += sunMoveSpeed * dt;
            manualSunX = Clamp(manualSunX, -100.0f, (float)(SCREEN_W+100));
            manualSunY = Clamp(manualSunY, 10.0f, (float)(horizonY+30));
        }

        // ---- Kontrol rakit (WASD saat auto, Arrow saat manual sun) ----
        float accel=455.0f, friction=0.87f;
        if (!manualSun) {
            if (IsKeyDown(KEY_LEFT) ||IsKeyDown(KEY_A)) raftVX-=accel*dt;
            if (IsKeyDown(KEY_RIGHT)||IsKeyDown(KEY_D)) raftVX+=accel*dt;
            if (IsKeyDown(KEY_UP)   ||IsKeyDown(KEY_W)) raftVY-=accel*dt;
            if (IsKeyDown(KEY_DOWN) ||IsKeyDown(KEY_S)) raftVY+=accel*dt;
        } else {
            // Mode manual: hanya WASD untuk rakit
            if (IsKeyDown(KEY_A)) raftVX-=accel*dt;
            if (IsKeyDown(KEY_D)) raftVX+=accel*dt;
            if (IsKeyDown(KEY_W)) raftVY-=accel*dt;
            if (IsKeyDown(KEY_S)) raftVY+=accel*dt;
        }
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

        // ---- Logo Polban melayang di langit ----
        // Bob pelan ke atas-bawah, bergerak lambat ke kiri
        float logoBobY = sinf(logoBobT * 0.6f) * 8.0f;
        logoX -= cloudSpeedBase * 0.2f * dt * 60.0f; // ikut kecepatan awan
        if (logoX < -120.0f) logoX = (float)(SCREEN_W + 100);

        // ---- Spawn ----
        if (spawnTimer>3.8f) { spawnTimer=0; spawnItem(true); }
        if (sharkTimer>7.0f&&sharks.size()<4) { sharkTimer=0; spawnShark(); }

        // ---- Update clouds dengan kecepatan baru ----
        for (auto& c:clouds) {
            c.speed = cloudSpeedBase + (rand()%100)/200.0f;
            c.x += c.speed;
            if (c.x>SCREEN_W+200) {
                c.x = -200;
                c.y = (float)(50+rand()%120);
            }
        }

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
            float dir=s.facingLeft?1.0f:-1.0f;
            s.x-=dir*s.speed;
            if (invincible<=0) {
                float sy2=s.y+sinf(s.wavePhase*2.0f)*6.0f;
                float ddx=s.x-raftX, ddy=sy2-(raftY+raftBob);
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

        // ================================================================
        // CELESTIAL POSITIONS
        // ================================================================
        float sunProg = (dayTime-0.05f)/0.13f;
        int sunX, sunY;
        if (manualSun) {
            sunX = (int)manualSunX;
            sunY = (int)manualSunY;
        } else {
            sunX = (int)(-80 + sunProg*(SCREEN_W+160));
            sunY = (int)(horizonY+40 - sinf(sunProg*PI)*(horizonY-20));
        }

        float moonT    = dayTime+0.12f;
        if (moonT>=0.24f) moonT-=0.24f;
        float moonProg = (moonT-0.05f)/0.13f;
        int   moonX = (int)(-80 + moonProg*(SCREEN_W+160));
        int   moonY = (int)(horizonY+40 - sinf(moonProg*PI)*(horizonY-20));

        float sunAlpha=0, moonAlpha=0, nightness=0;

        // ================================================================
        // SKY / OCEAN COLORS
        // ================================================================
        if (dayTime < 0.05f) {
            float t=dayTime/0.05f;
            curSky.skyTop = {(unsigned char)(5+t*50),(unsigned char)(5+t*30),(unsigned char)(30+t*60),255};
            curSky.skyBottom = {(unsigned char)(20+t*80),(unsigned char)(15+t*40),(unsigned char)(50+t*60),255};
            curSky.waterDeep = {(unsigned char)(10+t*20),(unsigned char)(30+t*30),(unsigned char)(70+t*40),255};
            curSky.waterShallow = {(unsigned char)(30+t*40),(unsigned char)(60+t*40),(unsigned char)(100+t*30),255};
            nightness=1.0f-t; moonAlpha=1.0f-t; sunAlpha=0;
        } else if (dayTime < 0.07f) {
            float t=(dayTime-0.05f)/0.02f;
            curSky.skyTop = {(unsigned char)(55+t*80),(unsigned char)(35+t*80),(unsigned char)(90+t*60),255};
            curSky.skyBottom = {(unsigned char)(100+t*100),(unsigned char)(55+t*80),(unsigned char)(110-t*40),255};
            curSky.waterDeep = {(unsigned char)(30+t*30),(unsigned char)(60+t*40),(unsigned char)(110+t*30),255};
            curSky.waterShallow = {(unsigned char)(70+t*50),(unsigned char)(100+t*40),(unsigned char)(130+t*20),255};
            sunAlpha=t; nightness=0; moonAlpha=0;
        } else if (dayTime < 0.16f) {
            float t=(dayTime-0.07f)/0.09f;
            curSky.skyTop = {40,100,(unsigned char)(180+18*(1.0f-t)),255};
            curSky.skyBottom = {(unsigned char)(120+t*30),(unsigned char)(178+t*20),228,255};
            curSky.waterDeep = {0,(unsigned char)(78+t*40),(unsigned char)(138+t*30),255};
            curSky.waterShallow = {(unsigned char)(t*28),(unsigned char)(138+t*30),(unsigned char)(178+t*20),255};
            sunAlpha=1.0f; nightness=0; moonAlpha=0;
        } else if (dayTime < 0.18f) {
            float t=(dayTime-0.16f)/0.02f;
            curSky.skyTop = {(unsigned char)(133-t*80),(unsigned char)(138-t*100),(unsigned char)(208-t*118),255};
            curSky.skyBottom = {(unsigned char)(238-t*80),(unsigned char)(128-t*58),(unsigned char)(78-t*48),255};
            curSky.waterDeep = {(unsigned char)(38-t*28),(unsigned char)(118-t*78),(unsigned char)(168-t*98),255};
            curSky.waterShallow = {(unsigned char)(98-t*68),(unsigned char)(148-t*98),(unsigned char)(178-t*98),255};
            sunAlpha=1.0f-t; moonAlpha=t*0.5f; nightness=0;
        } else {
            float t=(dayTime-0.18f)/0.06f;
            curSky.skyTop = {(unsigned char)(53-t*48),(unsigned char)(38-t*33),(unsigned char)(88-t*58),255};
            curSky.skyBottom = {(unsigned char)(28-t*8),(unsigned char)(23-t*8),(unsigned char)(58-t*28),255};
            curSky.waterDeep = {10,(unsigned char)(28+(1.0f-t)*28),(unsigned char)(68+(1.0f-t)*28),255};
            curSky.waterShallow = {18,(unsigned char)(48+(1.0f-t)*28),(unsigned char)(88+(1.0f-t)*18),255};
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

        // ================================================================
        // RENDER
        // ================================================================
        BeginDrawing();

        // Latar
        if (g_wireframe) {
            ClearBackground({5,8,15,255});
            Color gc={0,60,80,70};
            for (int y=horizonY; y<SCREEN_H; y+=30)
                BresenhamLine(0,y,SCREEN_W,y,gc);
            for (int x=0; x<SCREEN_W; x+=60)
                BresenhamLine(x,horizonY,x,SCREEN_H,gc);
            BresenhamLine(0,horizonY,SCREEN_W,horizonY,{0,160,210,180});
        } else {
            DrawTextureRec(bgTex.texture,
                {0,0,(float)SCREEN_W,-(float)SCREEN_H},{0,0},WHITE);
        }

        // Bintang
        if (nightness>0.05f||moonAlpha>0.3f)
            DrawStars(stars,fmaxf(nightness,moonAlpha-0.2f));

        // Matahari & Bulan
        if (sunAlpha>0.01f  && sunY <horizonY+35)
            DrawSun(sunX,sunY,sunAlpha);
        if (moonAlpha>0.01f && moonY<horizonY+35)
            DrawMoon(moonX,moonY,moonAlpha);

        // Mode manual: selalu gambar matahari
        if (manualSun) {
            DrawSun(sunX, sunY, fmaxf(sunAlpha, 0.85f));
        }

        // ---- Awan ----
        for (auto& c:clouds) DrawCloud((int)c.x,(int)c.y, sunAlpha*0.85f+0.1f);

        // ---- Logo Polban melayang di langit ----
        // Hanya tampil di siang hari (atau selalu dengan alpha)
        {
            float la = fmaxf(0.3f, sunAlpha * 0.9f + 0.1f);
            if (nightness > 0.5f) la = fmaxf(0.15f, la*(1.0f-nightness*0.8f));
            DrawPolbanLogo(
                (int)logoX,
                (int)(logoY + logoBobY),
                2.5f,  // scale
                la
            );
            // Label "POLBAN" di bawah logo
            if (!g_wireframe) {
                Color labelC = {255,255,255,(unsigned char)(200*la)};
                DrawText("POLBAN", (int)logoX - 22, (int)(logoY + logoBobY) + 50, 14, labelC);
            }
        }

        // ---- Pulau ----
        DrawIsland((int)islandX,(int)(SCREEN_H*0.57f));

        // ---- Ombak ----
        Color wL={255,255,255,56}, wM={198,226,255,72};
        for (int row=0;row<20;row++) {
            float bY=horizonY+row*27.0f+8;
            float amp=4.0f+row*1.2f;
            float freq=0.015f-row*0.0003f;
            float spd=1.5f+row*0.1f;
            float px=0, py=WaveY(0,waveOffset,bY,amp,freq,spd);
            for (int x=5;x<=SCREEN_W;x+=5) {
                float cy2=WaveY((float)x,waveOffset,bY,amp,freq,spd);
                DDALine((int)px,(int)py,x,(int)cy2,(row%2==0)?wL:wM);
                px=(float)x; py=cy2;
            }
        }
        for (int x=0;x<SCREEN_W;x++) {
            float wy=WaveY((float)x,waveOffset,(float)horizonY,5.f,0.02f,2.f);
            DrawPixel(x,(int)wy,{255,255,255,88});
            DrawPixel(x,(int)wy+1,{198,226,255,48});
        }

        // ---- Collectibles ----
        for (auto& c:items) {
            if (!c.alive) continue;
            float bobY=sinf(c.bobPhase)*8.0f, dY=c.y+bobY;
            if (c.collecting) {
                float sc=1.0f-c.collectTimer, al=1.0f-c.collectTimer;
                int sz=(int)(40.0f*sc); if(sz<2)sz=2;
                Color fc=c.isBarrel
                    ? Color{255,200,70,(unsigned char)(255*al)}
                    : Color{160,255,120,(unsigned char)(255*al)};
                MidpointCircleFill((int)c.x,(int)dY,sz,fc);
                MidpointCircleOutline((int)c.x,(int)dY,sz,{255,255,255,(unsigned char)(200*al)});
            } else {
                if (c.isBarrel) DrawBarrel(c.x,dY);
                else            DrawWoodLog(c.x,dY,sinf(c.bobPhase)*0.3f);
                float pulse=0.5f+0.5f*sinf(gameTime*4.0f);
                MidpointCircleOutline((int)c.x,(int)dY,34,
                    {255,255,100,(unsigned char)(50+60*pulse)});
            }
        }

        // ---- Partikel ----
        for (auto& p:particles) {
            float al=p.life/p.maxLife;
            int sz=(int)(8*al); if(sz<1)sz=1;
            Color pc=p.isBarrel
                ? Color{255,215,75,(unsigned char)(220*al)}
                : Color{155,240,115,(unsigned char)(220*al)};
            DrawRectangle((int)p.x-sz/2,(int)p.y-sz/2,sz,sz,pc);
        }

        // ---- Sharks ----
        for (auto& s:sharks)
            DrawShark(s.x, s.y+sinf(s.wavePhase*2.0f)*6.0f, s.facingLeft);

        // ---- Rakit + Orang ----
        bool showRaft=(invincible<=0)||((int)(invincible*10)%2==0);
        if (showRaft) {
            DrawRaft((int)raftX,(int)(raftY+raftBob));
            // Gambar orang di atas rakit (posisi sedikit lebih tinggi dari rakit)
            DrawPersonOnRaft((int)raftX, (int)(raftY+raftBob)-8, gameTime);
        }

        // ================================================================
        // HUD — SKOR & NYAWA
        // ================================================================
        DrawRectangle(12,12,220,100,{0,0,0,145});
        BresenhamLine(12, 12,232, 12,{100,200,255,200});
        BresenhamLine(12,112,232,112,{100,200,255,200});
        BresenhamLine(12, 12, 12,112,{100,200,255,200});
        BresenhamLine(232,12,232,112,{100,200,255,200});

        DrawText(TextFormat("SKOR: %d",score),22,20,22,{255,230,80,255});
        DrawText("NYAWA:",22,52,18,{255,255,255,200});
        for (int i=0;i<3;i++) {
            Color hc=(i<lives)?Color{255,72,72,255}:Color{78,78,78,140};
            int hx=100+i*30, hy=58;
            MidpointCircleFill(hx-4,hy-2,6,hc);
            MidpointCircleFill(hx+4,hy-2,6,hc);
            Bres_ThickLine(hx-10,hy+2,hx,  hy+14,hc,3);
            Bres_ThickLine(hx+10,hy+2,hx,  hy+14,hc,3);
        }
        int hours  =(int)(dayTime*100.0f);
        int minutes=(int)((dayTime*100.0f-hours)*60.0f);
        if (hours>=24) hours=0;
        DrawText(TextFormat("WAKTU: %02d:%02d",hours,minutes),22,82,16,{200,226,255,255});

        // ================================================================
        // PANEL KONTROL (kanan bawah)
        // ================================================================
        DrawControlPanel(
            SCREEN_W - 285, 15,
            cloudSpeedBase, CLOUD_SPEED_MIN, CLOUD_SPEED_MAX,
            daySpeed,       DAY_SPEED_MIN,   DAY_SPEED_MAX,
            manualSun ? manualSunX : (float)sunX,
            manualSun,
            horizonY
        );

        // Wireframe status
        const char* wfLabel = g_wireframe ? "[TAB] WIREFRAME ON" : "[TAB] WIREFRAME OFF";
        Color wfLabelCol = g_wireframe ? Color{100,255,200,230} : Color{160,160,160,180};
        DrawText(wfLabel, SCREEN_W-355, SCREEN_H-115, 12, wfLabelCol);

        DrawText(TextFormat("FPS: %d",GetFPS()), SCREEN_W/2-30,14,16,{180,255,180,180});

        // Mode manual info
        if (manualSun) {
            DrawRectangle(SCREEN_W/2-130, 10, 260, 24, {0,0,0,150});
            DrawText("MODE MANUAL: Arrow=Gerak Matahari | WASD=Rakit",
                SCREEN_W/2-125, 14, 12, {255,220,80,255});
        }

        // ---- Popup ----
        if (msgTimer>0 && !message.empty()) {
            float a=fminf(1.0f,msgTimer);
            int mw=MeasureText(message.c_str(),22), mx=SCREEN_W/2-mw/2;
            DrawRectangle(mx-14,SCREEN_H/2-30,mw+28,48,{0,0,0,(unsigned char)(155*a)});
            BresenhamLine(mx-14,SCREEN_H/2-30,mx+mw+14,SCREEN_H/2-30,{255,240,100,(unsigned char)(200*a)});
            BresenhamLine(mx-14,SCREEN_H/2+18,mx+mw+14,SCREEN_H/2+18,{255,240,100,(unsigned char)(200*a)});
            DrawText(message.c_str(),mx,SCREEN_H/2-18,22,{255,240,100,(unsigned char)(255*a)});
        }

        DrawText("RAKIT DI LAUT",SCREEN_W/2-100,SCREEN_H-26,18,{198,226,255,135});
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