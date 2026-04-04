# RAKIT DI LAUT 🚣 - Raft Ocean Survival Game
## C++ + Raylib | Algoritma Bresenham & DDA

---

## 📦 Deskripsi
Game survival rakit di laut menggunakan **C++ + Raylib** dengan semua gambar digambar menggunakan algoritma grafis komputer:
- **Bresenham Line Algorithm** – garis, badan hiu, layar rakit, kayu log
- **DDA (Digital Differential Analyzer)** – tali, sinar matahari, ombak, garis halus
- **Bresenham Circle** – matahari, bulan, kepala hiu (fill), barrel

---

## 🎮 Fitur
| Fitur | Detail |
|-------|--------|
| **Rakit** | Bergerak kiri/kanan/atas/bawah dengan layar kain |
| **Hiu** | Bergerak mengancam, animasi bergelombang, gigi terlihat |
| **Siklus Siang/Malam** | Matahari terbenam → bulan terbit, warna langit berubah gradual |
| **Ombak** | Dinamis, amplitudo dan frekuensi bervariasi |
| **Item** | Tong (+10 poin) dan Kayu (+5 poin) mengambang |
| **Pulau** | Background dengan pohon kelapa dan buah |
| **Awan** | Bergerak perlahan saat siang |
| **Bintang** | Muncul saat malam |
| **Refleksi** | Pantulan matahari/bulan di air |

---

## 🎯 Kontrol
```
W / ↑  = Maju (ke atas)
S / ↓  = Mundur (ke bawah)
A / ←  = Kiri
D / →  = Kanan
```
- **Hindari hiu** – nyawa berkurang jika terkena
- **Ambil tong & kayu** – mendekati item untuk mengumpulkan
- **3 nyawa** sebelum game over

---

## 🔧 Cara Build

### Windows (MinGW)
```bash
# Install raylib terlebih dahulu dari https://github.com/raysan5/raylib/releases
g++ main.cpp -o raft_game.exe -lraylib -lopengl32 -lgdi32 -lwinmm -std=c++17
```

### Linux
```bash
# Install raylib
sudo apt install libraylib-dev
# atau build dari source: https://github.com/raysan5/raylib

g++ main.cpp -o raft_game -lraylib -lm -lpthread -ldl -std=c++17
```

### CMake (semua platform)
```bash
mkdir build && cd build
cmake ..
make
./raft_game
```

### Web (Emscripten)
```bash
emcc main.cpp -o raft_game.html \
  -s USE_GLFW=3 -s ASYNCIFY \
  --shell-file minshell.html \
  -I/path/to/raylib/src \
  /path/to/raylib/src/libraylib.a
```

---

## 🏗️ Struktur File (sesuai repo)
```
garis-raylib/
└── src/
    ├── algo/          ← Implementasi Bresenham & DDA
    │   ├── bresenham.hpp
    │   └── dda.hpp
    ├── screens/       ← Game screens (gameplay, title, gameover)
    │   └── gameplay.cpp  ← isi dari main.cpp ini
    ├── ui/            ← UI elements (HUD, buttons)
    └── main.cpp       ← Entry point
```

---

## 📐 Algoritma yang Digunakan

### Bresenham Line
```cpp
void DrawBresenhamLine(int x0, int y0, int x1, int y1, Color color) {
    int dx = abs(x1-x0), dy = abs(y1-y0);
    int sx = x0<x1?1:-1, sy = y0<y1?1:-1;
    int err = dx - dy;
    while(true) {
        DrawPixel(x0, y0, color);
        if(x0==x1 && y0==y1) break;
        int e2 = 2*err;
        if(e2 > -dy) { err -= dy; x0 += sx; }
        if(e2 <  dx) { err += dx; y0 += sy; }
    }
}
```

### DDA Line
```cpp
void DrawDDALine(float x0, float y0, float x1, float y1, Color color) {
    float dx=x1-x0, dy=y1-y0;
    int steps = (int)(fabs(dx)>fabs(dy)?fabs(dx):fabs(dy));
    float xInc=dx/steps, yInc=dy/steps;
    for(int i=0; i<=steps; i++) {
        DrawPixel((int)roundf(x0), (int)roundf(y0), color);
        x0 += xInc; y0 += yInc;
    }
}
```

### Bresenham Circle (Midpoint)
```cpp
void DrawBresenhamCircle(int cx, int cy, int r, Color color) {
    int x=0, y=r, d=3-2*r;
    while(y>=x) {
        // plot 8 octants
        if(d>0){d+=4*(x-y)+10;y--;}else{d+=4*x+6;}
        x++;
    }
}
```

---

## 🎨 Objek yang Digambar

| Objek | Teknik |
|-------|--------|
| Matahari & sinarnya | Bresenham circle fill + DDA rays |
| Bulan | Bresenham circle fill overlap |
| Bintang | Pixel + DDA cross |
| Ombak laut | DDA sinusoidal lines |
| Langit gradient | Bresenham horizontal scanlines |
| Pulau + pohon kelapa | Bresenham scanlines + DDA fronds |
| Rakit dengan layar | Bresenham thick lines + DDA rope |
| Hiu detail | Bresenham ellipse fill + DDA gills/fins |
| Tong kayu | Bresenham planks + DDA hoops |
| Batang kayu | Bresenham thick rotated + circle caps |
| Awan | Bresenham circle fill overlapping |
