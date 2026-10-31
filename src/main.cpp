// Space Taxi Mayhem
// A Space Taxi (C64) / Space Taxi 3 (Amiga) style game.  C++17, SDL2, SDL2_ttf.
//
// All pixel art is generated procedurally at load time into a 320x240 software
// framebuffer.  Collision detection is pixel perfect: every solid/hazard pixel of
// the level is kept in a per-pixel mask that is tested against the taxi sprite mask.

#include <SDL.h>
#include <SDL_ttf.h>
#include <vector>
#include <string>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <unordered_map>
#include "audio.hpp"
#ifdef __SWITCH__
#include <switch.h>
#endif

// ------------------------------------------------------------------ constants
static const int SW = 320, SH = 240;          // screen
static const int TS = 8, MW = 40, MH = 27;     // tiles
static const int WW = MW * TS, WH = MH * TS;   // world (320x216)
static const int MAPY = 0;                     // world y offset on screen (playfield flush with the top)
static const int HUDY = 216;                   // status bar: 16 px directly below the playfield
static const int HUDH = 16;                    // level name / fares line below that (y 232..239)
static const int MAXP = 5;
static const int NLEVELS = 20;

static const float G0 = 40.f;          // gravity px/s^2 (scaled per level)
static const float THRUST_UP = 100.f;
static const float THRUST_DOWN = 45.f;
static const float THRUST_SIDE = 72.f;
static const float LAND_VY = 46.f;     // max vertical speed for a safe landing
static const float LAND_VX = 30.f;
static const float MAXVX = 115.f, MAXVY = 150.f;
static const float FUEL_MAX = 100.f;

// economy: fuel is expensive, fares are small, mistakes cost real money
static const int   FUEL_PRICE = 1;          // $ per fuel unit (a full tank costs $100)
static const int   FARE_BASE = 10;          // paid for every delivery
static const float TIP_START = 30.f;        // tip when the passenger gets in...
static const float TIP_DECAY = 2.f;         // ...shrinking by this many $ per second of riding
static const int   CRASH_FEE = 50;          // repair bill when your taxi is destroyed
static const int   GATE_BONUS = 100;        // for flying the "Up, please!" passenger out
static const long  EXTRA_LIFE_EVERY = 1000;
static const float ROUGH_VY = 30.f;         // touchdown faster than this (but under LAND_VY) = rough landing
static const int   ROUGH_FARE_CUT = 5;      // $ off the fare per rough landing with a passenger aboard (tip is lost too)

// ------------------------------------------------------------------ utilities
static inline uint32_t rgb(int r, int g, int b) {
    r = std::clamp(r, 0, 255); g = std::clamp(g, 0, 255); b = std::clamp(b, 0, 255);
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}
static inline int CR(uint32_t c) { return (c >> 16) & 255; }
static inline int CG(uint32_t c) { return (c >> 8) & 255; }
static inline int CB(uint32_t c) { return c & 255; }
static inline uint32_t mix(uint32_t a, uint32_t b, float t) {
    t = std::clamp(t, 0.f, 1.f);
    return rgb((int)(CR(a) + (CR(b) - CR(a)) * t), (int)(CG(a) + (CG(b) - CG(a)) * t), (int)(CB(a) + (CB(b) - CB(a)) * t));
}
static inline uint32_t shade(uint32_t c, float f) { return rgb((int)(CR(c) * f), (int)(CG(c) * f), (int)(CB(c) * f)); }
static const int BAYER[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
static inline float bayer(int x, int y) { return (BAYER[(x & 3) + ((y & 3) << 2)] + 0.5f) / 16.f; }
static inline uint32_t hash32(uint32_t x) { x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16; return x; }
static inline uint32_t hash2(int x, int y, int s) { return hash32((uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ (uint32_t)s * 83492791u); }
static inline float h01(int x, int y, int s) { return (hash2(x, y, s) & 0xFFFF) / 65535.f; }
static float vnoise(float x, float y, int s) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    float fx = x - xi, fy = y - yi;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    float a = h01(xi, yi, s), b = h01(xi + 1, yi, s), c = h01(xi, yi + 1, s), d = h01(xi + 1, yi + 1, s);
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
}
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed = 1) : s(seed ? seed : 1) {}
    uint32_t u() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float f() { return (u() & 0xFFFFFF) / 16777216.f; }
    float r(float a, float b) { return a + (b - a) * f(); }
    int i(int n) { return n > 0 ? (int)(u() % (uint32_t)n) : 0; }
};
static Rng rng(0xC0FFEE);

// ------------------------------------------------------------------ framebuffer
static uint32_t fb[SW * SH];
static inline void pset(int x, int y, uint32_t c) { if ((unsigned)x < (unsigned)SW && (unsigned)y < (unsigned)SH) fb[y * SW + x] = c; }
static inline void pblend(int x, int y, uint32_t c, float a) {
    if ((unsigned)x < (unsigned)SW && (unsigned)y < (unsigned)SH) fb[y * SW + x] = mix(fb[y * SW + x], c, a);
}
static void rectf(int x, int y, int w, int h, uint32_t c) {
    for (int j = std::max(0, y); j < std::min(SH, y + h); j++)
        for (int i = std::max(0, x); i < std::min(SW, x + w); i++) fb[j * SW + i] = c;
}
static void rectb(int x, int y, int w, int h, uint32_t c, float a) {
    for (int j = std::max(0, y); j < std::min(SH, y + h); j++)
        for (int i = std::max(0, x); i < std::min(SW, x + w); i++) fb[j * SW + i] = mix(fb[j * SW + i], c, a);
}
static void rectline(int x, int y, int w, int h, uint32_t c) {
    rectf(x, y, w, 1, c); rectf(x, y + h - 1, w, 1, c); rectf(x, y, 1, h, c); rectf(x + w - 1, y, 1, h, c);
}

// ------------------------------------------------------------------ themes
struct Theme {
    uint32_t skyTop, skyBot;
    uint32_t metal[5];
    uint32_t rock[5];
    uint32_t planetA, planetB;
    uint32_t hillFar, hillNear;
    int hillStyle;       // 0 rolling, 1 jagged, 2 city
    bool ring;
    uint32_t accent;
};
static const Theme THEMES[7] = {
    // 0 blue space station
    {rgb(4, 4, 22), rgb(36, 22, 70),
     {rgb(18, 26, 52), rgb(38, 58, 104), rgb(66, 96, 156), rgb(116, 148, 206), rgb(196, 218, 255)},
     {rgb(30, 30, 44), rgb(56, 56, 78), rgb(86, 84, 112), rgb(128, 124, 156), rgb(184, 180, 210)},
     rgb(120, 40, 20), rgb(255, 170, 90), rgb(26, 18, 54), rgb(18, 12, 38), 0, true, rgb(90, 200, 255)},
    // 1 mars
    {rgb(22, 4, 12), rgb(120, 46, 34),
     {rgb(40, 26, 24), rgb(84, 54, 44), rgb(132, 90, 66), rgb(184, 136, 100), rgb(240, 200, 160)},
     {rgb(58, 18, 10), rgb(110, 42, 20), rgb(160, 72, 34), rgb(206, 112, 58), rgb(246, 172, 110)},
     rgb(70, 60, 90), rgb(220, 210, 240), rgb(92, 30, 26), rgb(66, 20, 18), 1, false, rgb(255, 160, 60)},
    // 2 green tech
    {rgb(0, 8, 8), rgb(10, 46, 40),
     {rgb(10, 36, 28), rgb(18, 74, 52), rgb(36, 124, 82), rgb(86, 186, 118), rgb(184, 250, 190)},
     {rgb(16, 30, 34), rgb(28, 54, 60), rgb(44, 84, 90), rgb(70, 124, 128), rgb(130, 180, 176)},
     rgb(40, 20, 80), rgb(150, 220, 255), rgb(8, 34, 30), rgb(4, 24, 20), 0, true, rgb(120, 255, 160)},
    // 3 ice
    {rgb(8, 16, 46), rgb(80, 136, 196),
     {rgb(34, 40, 60), rgb(64, 74, 100), rgb(104, 116, 144), rgb(158, 170, 196), rgb(222, 230, 250)},
     {rgb(38, 66, 116), rgb(66, 116, 176), rgb(114, 174, 226), rgb(176, 222, 248), rgb(244, 254, 255)},
     rgb(160, 120, 200), rgb(255, 240, 255), rgb(54, 90, 150), rgb(36, 64, 116), 1, false, rgb(180, 240, 255)},
    // 4 neon city
    {rgb(10, 0, 26), rgb(74, 12, 84),
     {rgb(36, 14, 56), rgb(76, 28, 106), rgb(126, 58, 166), rgb(186, 108, 218), rgb(250, 190, 255)},
     {rgb(30, 20, 50), rgb(56, 38, 84), rgb(88, 62, 120), rgb(130, 98, 164), rgb(190, 160, 220)},
     rgb(200, 40, 120), rgb(255, 200, 120), rgb(40, 8, 56), rgb(22, 4, 34), 2, false, rgb(255, 80, 220)},
    // 5 lava
    {rgb(18, 0, 0), rgb(140, 40, 4),
     {rgb(40, 30, 30), rgb(76, 60, 56), rgb(116, 94, 84), rgb(166, 140, 120), rgb(226, 200, 170)},
     {rgb(40, 18, 14), rgb(78, 34, 20), rgb(118, 54, 26), rgb(170, 88, 40), rgb(236, 150, 70)},
     rgb(80, 10, 0), rgb(255, 220, 80), rgb(70, 14, 4), rgb(40, 8, 4), 1, false, rgb(255, 120, 20)},
    // 6 moon
    {rgb(0, 0, 4), rgb(20, 20, 40),
     {rgb(30, 34, 44), rgb(58, 64, 80), rgb(94, 102, 122), rgb(140, 148, 168), rgb(206, 212, 228)},
     {rgb(40, 40, 46), rgb(74, 74, 84), rgb(114, 114, 124), rgb(160, 160, 170), rgb(216, 216, 226)},
     rgb(20, 60, 140), rgb(120, 220, 160), rgb(26, 26, 36), rgb(16, 16, 24), 0, false, rgb(200, 220, 255)},
};

// ------------------------------------------------------------------ level data
struct PlatDef { float x0, y0, x1, y1; int w; float period, phase; char pad; int sw; };
struct MineDef { float x0, y0, x1, y1, period, phase, r; };
struct LevelDef {
    std::string name; int theme = 0; float gravity = 1; int quota = 5;
    std::vector<std::string> map; std::vector<PlatDef> plats; std::vector<MineDef> mines;
};
#include "levels.inc"
static std::vector<LevelDef> LEVELDEFS;

// ------------------------------------------------------------------ text
static TTF_Font* font8 = nullptr;
static TTF_Font* font16 = nullptr;
static TTF_Font* font24 = nullptr;
struct TextMask { int w = 0, h = 0; std::vector<uint8_t> m; };
static std::unordered_map<std::string, TextMask> textCache;

static const TextMask& textMask(TTF_Font* f, int size, const std::string& s) {
    std::string key = std::to_string(size) + "|" + s;
    auto it = textCache.find(key);
    if (it != textCache.end()) return it->second;
    if (textCache.size() > 600) textCache.clear();
    TextMask tm;
    if (f && !s.empty()) {
        SDL_Color white = {255, 255, 255, 255};
        SDL_Surface* surf = TTF_RenderUTF8_Solid(f, s.c_str(), white);
        if (surf) {
            tm.w = surf->w; tm.h = surf->h; tm.m.resize(tm.w * tm.h);
            SDL_LockSurface(surf);
            for (int y = 0; y < tm.h; y++)
                for (int x = 0; x < tm.w; x++)
                    tm.m[y * tm.w + x] = ((uint8_t*)surf->pixels)[y * surf->pitch + x] ? 1 : 0;
            SDL_UnlockSurface(surf);
            SDL_FreeSurface(surf);
        }
    }
    return textCache[key] = tm;
}
static int textW(const std::string& s, int size = 8) { return (int)s.size() * size; }
static void drawText(int x, int y, const std::string& s, uint32_t c, uint32_t shadow = 0xFF000000u, int size = 8) {
    TTF_Font* f = size == 8 ? font8 : (size == 16 ? font16 : font24);
    const TextMask& tm = textMask(f, size, s);
    for (int j = 0; j < tm.h; j++)
        for (int i = 0; i < tm.w; i++)
            if (tm.m[j * tm.w + i]) {
                if (shadow) pset(x + i + 1, y + j + 1, shadow);
            }
    for (int j = 0; j < tm.h; j++)
        for (int i = 0; i < tm.w; i++)
            if (tm.m[j * tm.w + i]) pset(x + i, y + j, c);
}
static void drawTextC(int cx, int y, const std::string& s, uint32_t c, uint32_t shadow = 0xFF000000u, int size = 8) {
    drawText(cx - textW(s, size) / 2, y, s, c, shadow, size);
}
// Gradient filled, outlined text for logos and banners
static void drawFancyText(int cx, int y, const std::string& s, int size, uint32_t top, uint32_t mid, uint32_t bot, uint32_t outline) {
    TTF_Font* f = size == 8 ? font8 : (size == 16 ? font16 : font24);
    const TextMask& tm = textMask(f, size, s);
    int x = cx - tm.w / 2;
    for (int j = 0; j < tm.h; j++)
        for (int i = 0; i < tm.w; i++)
            if (tm.m[j * tm.w + i]) {
                for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) pset(x + i + dx, y + j + dy, outline);
                pset(x + i + 2, y + j + 2, outline); pset(x + i + 3, y + j + 3, shade(outline, 0.5f));
            }
    for (int j = 0; j < tm.h; j++) {
        float t = tm.h > 1 ? (float)j / (tm.h - 1) : 0;
        for (int i = 0; i < tm.w; i++)
            if (tm.m[j * tm.w + i]) {
                float tt = t + (bayer(i, j) - 0.5f) * 0.18f;
                uint32_t c = tt < 0.5f ? mix(top, mid, tt * 2) : mix(mid, bot, (tt - 0.5f) * 2);
                if (j == 0 || !tm.m[(j - 1) * tm.w + i]) c = mix(c, rgb(255, 255, 255), 0.6f);
                pset(x + i, y + j, c);
            }
    }
}

// tiny 3x5 font for pad labels
static const char* TINY_CH = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ+$!-.";
static const uint8_t TINY[][5] = {
    {7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},{7,4,7,1,7},{7,4,7,5,7},{7,1,2,2,2},{7,5,7,5,7},{7,5,7,1,7},
    {2,5,7,5,5},{6,5,6,5,6},{3,4,4,4,3},{6,5,5,5,6},{7,4,6,4,7},{7,4,6,4,4},{3,4,5,5,3},{5,5,7,5,5},{7,2,2,2,7},{1,1,1,5,2},
    {5,5,6,5,5},{4,4,4,4,7},{5,7,7,5,5},{6,5,5,5,5},{2,5,5,5,2},{7,5,7,4,4},{2,5,5,6,3},{6,5,6,5,5},{3,4,2,1,6},{7,2,2,2,2},
    {5,5,5,5,7},{5,5,5,5,2},{5,5,7,7,5},{5,5,2,5,5},{5,5,2,2,2},{7,1,2,4,7},{0,2,7,2,0},{3,6,2,3,6},
    {2,2,2,0,2},{0,0,7,0,0},{0,0,0,0,2}};
template <class F> static void tinyText(int x, int y, const char* s, F put) {
    for (int k = 0; s[k]; k++, x += 4) {
        const char* p = strchr(TINY_CH, s[k]);
        if (!p || s[k] == ' ') continue;
        const uint8_t* g = TINY[p - TINY_CH];
        for (int j = 0; j < 5; j++) for (int i = 0; i < 3; i++) if (g[j] & (4 >> i)) put(x + i, y + j);
    }
}

// small 3x5 text with a dark 1 px outline, horizontally centred on cx (used for in-level labels)
static void drawTinyOutlinedC(int cx, int y, const std::string& s, uint32_t c) {
    int w = (int)s.size() * 4 - 1;
    int x = std::clamp(cx - w / 2, 1, SW - w - 1);
    const uint32_t outline = rgb(16, 8, 24);
    const int ox[8] = {-1, 0, 1, -1, 1, -1, 0, 1}, oy[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
    for (int d = 0; d < 8; d++) tinyText(x + ox[d], y + oy[d], s.c_str(), [&](int px, int py) { pset(px, py, outline); });
    tinyText(x, y, s.c_str(), [&](int px, int py) { pset(px, py, c); });
}

// on-screen button names (SDL's "A"/"B" are Cross/Circle on the PS Vita)
#if defined(__vita__)
#define JOIN_LABEL "X"
#define GEAR_LABEL "X/O"
#define QUIT_LABEL "O"
#else
#define JOIN_LABEL "A"
#define GEAR_LABEL "A/B"
#define QUIT_LABEL "B"
#endif

// ------------------------------------------------------------------ sprites
// Taxi, facing right. Y body, y shadow, H highlight, W/w windows, K dark, L roof sign,
// G/g grey metal, R headlight, T tail light
static const char* TAXI_SPR[10] = {
    "....gLLg........",
    "...KHHHHHHK.....",
    "..KWWwKWWWwK....",
    "..YWwwKWWwwYYYY.",
    "HHHHHHHHHHHHHHHR",
    "KKYYKKYYKKYYKKYY",
    "TyyyyyyyyyyyyyyY",
    ".gGGg......gGGg.",
    "..gG........Gg..",   // gear
    ".GGGG......GGGG.",   // gear
};
static uint8_t taxiMask[2][3][16 * 10];   // [face][gear rows drawn: 0,1,2] - exactly the drawn pixels
struct PColor { uint32_t main, dark, light; const char* name; };
static const PColor PCOL[MAXP] = {
    {rgb(250, 200, 30), rgb(186, 124, 18), rgb(255, 240, 150), "YELLOW"},
    {rgb(232, 52, 44), rgb(146, 24, 26), rgb(255, 156, 132), "RED"},
    {rgb(44, 200, 232), rgb(20, 114, 160), rgb(176, 242, 255), "CYAN"},
    {rgb(76, 212, 62), rgb(30, 128, 40), rgb(186, 255, 164), "GREEN"},
    {rgb(222, 74, 222), rgb(136, 30, 150), rgb(255, 176, 255), "PINK"},
};
static uint32_t taxiPix(char ch, int pc, bool flash) {
    const PColor& P = PCOL[pc];
    uint32_t c = 0;
    switch (ch) {
        case 'Y': c = P.main; break;  case 'y': c = P.dark; break;  case 'H': c = P.light; break;
        case 'W': c = rgb(150, 220, 255); break;  case 'w': c = rgb(60, 110, 170); break;
        case 'K': c = rgb(24, 20, 30); break;  case 'L': c = flash ? rgb(255, 255, 200) : rgb(255, 150, 40); break;
        case 'G': c = rgb(150, 150, 165); break;  case 'g': c = rgb(80, 80, 96); break;
        case 'R': c = rgb(255, 255, 210); break;  case 'T': c = rgb(255, 40, 40); break;
        default: break;
    }
    return c;
}
static void buildTaxiMasks() {
    // Must match blitTaxi(): body rows 0-7; gear fully out draws sprite rows 8+9,
    // half out draws only sprite row 9 (the feet) at y = 8.
    memset(taxiMask, 0, sizeof taxiMask);
    for (int f = 0; f < 2; f++)
        for (int g = 0; g < 3; g++)
            for (int y = 0; y < 10; y++) {
                int sy = y;
                if (y == 8) { if (g == 0) continue; sy = g == 1 ? 9 : 8; }
                if (y == 9 && g < 2) continue;
                for (int x = 0; x < 16; x++) {
                    int sx = f ? 15 - x : x;
                    taxiMask[f][g][y * 16 + x] = TAXI_SPR[sy][sx] != '.';
                }
            }
}
// passengers: 6x10 sprites composed from a body (gender x pose) and a head (hair style x view).
// H hair, S skin, E eye, C top (also caps), P trousers, D skirt, L bare legs, B shoes.
// Poses: 0 stand (front), 1/2 walk (side, facing right), 3/4 wave (front).
static const char* PBODY[2][5][10] = {
    {   // man
        {"......", "......", "......", "CCCCC.", "CCCCC.", "SCCCS.", ".PPP..", ".P.P..", ".P.P..", ".B.B.."},
        {"......", "......", "......", ".CCC..", ".CCC..", ".CCS..", ".PPP..", ".P.P..", "P...P.", "B...B."},
        {"......", "......", "......", ".CCC..", ".CCC..", "SCC...", ".PPP..", "..P...", "..P...", "..BB.."},
        {".....S", ".....C", ".....C", "CCCCC.", "CCCC..", "SCCC..", ".PPP..", ".P.P..", ".P.P..", ".B.B.."},
        {"....S.", "....C.", ".....C", "CCCCC.", "CCCC..", "SCCC..", ".PPP..", ".P.P..", ".P.P..", ".B.B.."},
    },
    {   // woman
        {"......", "......", "......", "CCCCC.", "CCCCC.", "SDDDS.", "DDDDD.", ".L.L..", ".L.L..", ".B.B.."},
        {"......", "......", "......", ".CCC..", ".CCC..", ".DDS..", "DDDD..", ".L.L..", "L...L.", "B...B."},
        {"......", "......", "......", ".CCC..", ".CCC..", "SDD...", ".DDDD.", "..L...", "..L...", "..BB.."},
        {".....S", ".....C", ".....C", "CCCCC.", "CCCC..", "SDDD..", "DDDDD.", ".L.L..", ".L.L..", ".B.B.."},
        {"....S.", "....C.", ".....C", "CCCCC.", "CCCC..", "SDDD..", "DDDDD.", ".L.L..", ".L.L..", ".B.B.."},
    },
};
// hair styles: 0 short, 1 afro, 2 cap, 3 bald (men) | 4 long, 5 bob, 6 ponytail (women)
static const char* PHEAD[7][2][4] = {   // [style][0 front / 1 side][rows 0-3]
    {{".HHH..", ".ESE..", ".SSS..", "......"}, {".HHH..", ".HSE..", ".SSS..", "......"}},
    {{"HHHHH.", "HESEH.", ".SSS..", "......"}, {"HHHH..", "HHSE..", ".HSS..", "......"}},
    {{"CCCCC.", ".ESE..", ".SSS..", "......"}, {".CCCC.", ".HSE..", ".SSS..", "......"}},
    {{".SSS..", "HESEH.", ".SSS..", "......"}, {".SSS..", "HSSE..", ".SSS..", "......"}},
    {{".HHH..", "HESEH.", "HSSSH.", "H...H."}, {".HHH..", "HHSE..", "HHSS..", "HH...."}},
    {{".HHH..", "HESEH.", "HSSSH.", "......"}, {".HHH..", "HHSE..", ".HSS..", "......"}},
    {{".HHH..", ".ESE..", ".SSS..", "......"}, {".HHH..", "HHSE..", "H.SS..", "H....."}},
};
static const uint32_t SKIN_TONES[6] = {rgb(255, 224, 196), rgb(241, 194, 150), rgb(224, 172, 105),
                                       rgb(198, 134, 66), rgb(141, 85, 36), rgb(96, 58, 30)};
static const uint32_t HAIR_COLS[7] = {rgb(26, 20, 20), rgb(70, 42, 22), rgb(122, 72, 32), rgb(232, 200, 112),
                                      rgb(176, 64, 26), rgb(206, 206, 212), rgb(48, 34, 30)};
static const uint32_t TOP_COLS[10] = {rgb(230, 40, 60), rgb(40, 180, 90), rgb(250, 200, 40), rgb(80, 120, 250), rgb(250, 250, 250),
                                      rgb(40, 210, 220), rgb(240, 120, 30), rgb(170, 80, 220), rgb(255, 120, 170), rgb(120, 200, 60)};
static const uint32_t PANTS_COLS[5] = {rgb(40, 50, 120), rgb(36, 36, 44), rgb(110, 76, 44), rgb(96, 96, 106), rgb(170, 150, 100)};
static const uint32_t SKIRT_COLS[6] = {rgb(200, 30, 60), rgb(120, 40, 160), rgb(20, 140, 140), rgb(230, 90, 160), rgb(40, 60, 150), rgb(30, 30, 36)};

static const char* MINE_SPR[9] = {
    "....X....", ".X..o..X.", "..ooooo..", "..oOOOo..", "XooOROooX", "..oOOOo..", "..ooooo..", ".X..o..X.", "....X....",
};

// ------------------------------------------------------------------ runtime level
struct PadE { int x, y, w; char id; int plat; };
struct PlatE { PlatDef d; float t = 0; int x = 0, y = 0, w = 0; int dx = 0, dy = 0; float vx = 0, vy = 0; int pad = -1; };   // dx/dy: whole pixels moved this frame, vx/vy: exact px/s
struct MineE { MineDef d; float x = 0, y = 0; };
struct Slot { int pad, x; };
struct Particle { float x, y, vx, vy, life, max; uint32_t c; float g; int size; };
struct FloatText { float x, y, life; std::string s; uint32_t c; };

struct Level {
    int idx = 0;
    const LevelDef* def = nullptr;
    const Theme* th = nullptr;
    char tile[MH][MW];
    std::vector<uint32_t> img;              // background + static tiles
    std::vector<uint8_t> sSolid, sHaz;      // static masks
    std::vector<uint8_t> solid, haz;        // per-frame masks
    std::vector<PadE> pads;
    std::vector<PlatE> plats;
    std::vector<MineE> mines;
    std::vector<SDL_Point> switches;        // world px of tile
    std::vector<Slot> slots;
    float windX[MH][MW], windY[MH][MW];
    SDL_Rect tpA{0, 0, 0, 0}, tpB{0, 0, 0, 0};
    bool hasTp = false;
    bool switchOn = false;
    float switchCd = 0;
    bool quotaDone = false;
    bool upDone = false;        // the "Up, please!" passenger has been flown out: everyone else may exit now
    float gateAnim = 0;
    int delivered = 0;
    float time = 0;
    bool laserA = false, laserB = false;
    char T(int x, int y) const { if (x < 0 || y < 0 || x >= MW || y >= MH) return '#'; return tile[y][x]; }
};
static Level LV;

// ------------------------------------------------------------------ game state
struct Input { bool u = 0, d = 0, l = 0, r = 0, a = 0, b = 0, start = 0, back = 0; };
enum Src { SRC_NONE = 0, SRC_PAD, SRC_KEYA, SRC_KEYB };

struct Player {
    bool joined = false; int src = SRC_NONE; SDL_JoystickID jid = -1;
    Input cur, prev;
    long score = 0; int lives = 5; long nextLife = EXTRA_LIFE_EVERY; int fares = 0, levelFares = 0;
    bool alive = false; float deadT = 0;
    float x = 0, y = 0, vx = 0, vy = 0; int face = 0;
    bool gear = true; float gearExt = 2; bool landed = false; int landPad = -1, landPlat = -1;
    float roughT = 0;     // > 0 while the taxi shakes after a rough landing
    bool exited = false;  // flew out through the exit gate this level
    float invuln = 0, fuel = FUEL_MAX; int pass = -1; float tip = 0;
    float refuelAcc = 0, noFuelT = 0; bool noMoneyShown = false;
    float fuelSndT = 0, lowFuelT = 0, tpCd = 0, landedT = 0, airT = 9;
    bool thU = 0, thD = 0, thL = 0, thR = 0;
    bool pressed(bool Input::*b) const { return cur.*b && !(prev.*b); }
    float cx() const { return x + 8; }
    int gearRows() const { return (int)std::lround(gearExt); }   // gear rows currently drawn
    float cy() const { return y + 5; }
};
static Player P[MAXP];

// PS_EXIT: delivered, walking to the pad edge; PS_BEAM: standing there, teleporting away.
// Only PS_WAIT passengers can be hit by taxis.
enum PState { PS_NONE = 0, PS_WAIT, PS_RIDE, PS_EXIT, PS_BEAM };
struct Passenger {
    int state = PS_NONE; int pad = -1; char dest = 0; bool up = false;
    float relx = 0;              // x relative to the pad (so passengers ride moving pads)
    float exitX = 0;             // pad-relative x of the edge a delivered passenger walks to
    float homeX = 0;             // pad-relative spot where the passenger appeared (and returns to)
    float waveT = 0;             // time until the next (silent) wave
    int taxi = -1; float heyT = 0, anim = 0, beamT = 0, beamDur = 0.9f, dir = 1; int voice = 0;
    int gender = 0, style = 0; uint32_t skin = 0, hair = 0, top = 0, bottom = 0;
    bool noTip = false; int fareCut = 0;   // after rough landings with this passenger aboard
};
static std::vector<Passenger> PASS(12);

enum GState { GS_TITLE, GS_INTRO, GS_PLAY, GS_DONE, GS_OVER, GS_WIN, GS_PAUSE };
static GState gstate = GS_TITLE;
static float stateT = 0;
static int curLevel = 0;
static int startLevel_ = 0;
static std::vector<Particle> parts;
static std::vector<FloatText> ftexts;
static float shakeT = 0;
static float spawnT = 0, upSpawnT = 0;
static int upCarrier = -1;
static Audio audio;
static std::string assetDir;
static bool headless = false;
static bool autoplay = false;
static float tglobal = 0;
static std::vector<uint32_t> titleBg;

static float panOf(float x) { return std::clamp((x - WW / 2) / (WW / 2.f), -1.f, 1.f) * 0.8f; }

// ------------------------------------------------------------------ background art
static void genBackground(const Theme& th, uint32_t* out, int w, int h, int seed) {
    Rng r(seed * 7919 + 17);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float t = (float)y / (h - 1);
            float q = t * 10 + bayer(x, y) - 0.5f;
            int lvl = std::clamp((int)std::floor(q + 0.5f), 0, 10);
            out[y * w + x] = mix(th.skyTop, th.skyBot, lvl / 10.f);
        }
    }
    // nebula wisps
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float n = vnoise(x * 0.025f, y * 0.05f, seed) * 0.6f + vnoise(x * 0.07f, y * 0.12f, seed + 3) * 0.4f;
            if (n > 0.62f && bayer(x, y) < (n - 0.62f) * 3.f) out[y * w + x] = mix(out[y * w + x], th.accent, 0.18f);
        }
    // stars
    for (int i = 0; i < 160; i++) {
        int x = r.i(w), y = (int)(std::pow(r.f(), 1.5f) * h * 0.85f);
        float b = r.f();
        uint32_t sc = (i % 5 == 0) ? rgb(255, 230, 180) : (i % 7 == 0 ? rgb(170, 200, 255) : rgb(255, 255, 255));
        uint32_t base = out[y * w + x];
        out[y * w + x] = mix(base, sc, 0.25f + 0.75f * b);
        if (b > 0.94f) {
            for (int k = 1; k <= 2; k++) {
                float a = k == 1 ? 0.6f : 0.25f;
                if (x - k >= 0) out[y * w + x - k] = mix(out[y * w + x - k], sc, a);
                if (x + k < w) out[y * w + x + k] = mix(out[y * w + x + k], sc, a);
                if (y - k >= 0) out[(y - k) * w + x] = mix(out[(y - k) * w + x], sc, a);
                if (y + k < h) out[(y + k) * w + x] = mix(out[(y + k) * w + x], sc, a);
            }
        }
    }
    // planet
    float cx = r.r(40, w - 40.f), cy = r.r(18, h * 0.42f), rad = r.r(14, 28);
    float lx = -0.65f, ly = -0.55f;
    for (int y = (int)(cy - rad * 2.2f); y <= (int)(cy + rad * 2.2f); y++)
        for (int x = (int)(cx - rad * 2.2f); x <= (int)(cx + rad * 2.2f); x++) {
            if (x < 0 || y < 0 || x >= w || y >= h) continue;
            float nx = (x - cx) / rad, ny = (y - cy) / rad, d2 = nx * nx + ny * ny;
            bool behind = false;
            if (th.ring) {
                float ex = (x - cx) / (rad * 2.0f), ey = (y - cy) / (rad * 0.42f);
                float e = ex * ex + ey * ey;
                if (e > 0.62f && e < 1.0f && !(d2 < 1 && y < cy)) {
                    float band = std::fmod(e * 9.f, 1.f);
                    uint32_t rc = mix(th.planetA, th.planetB, 0.5f + 0.4f * band);
                    out[y * w + x] = mix(out[y * w + x], rc, 0.55f + 0.2f * bayer(x, y));
                    behind = true;
                }
            }
            if (d2 >= 1.f || behind) continue;
            float nz = std::sqrt(1 - d2);
            float lit = std::clamp(-(nx * lx + ny * ly) * 0.75f + nz * 0.45f, 0.f, 1.f);
            float tex = vnoise(x * 0.18f, y * 0.45f + nx * 2, seed + 9);
            float v = lit * 0.8f + (tex - 0.5f) * 0.35f;
            int lvl = std::clamp((int)std::floor(v * 5 + bayer(x, y) - 0.2f), 0, 5);
            uint32_t pc = mix(shade(th.planetA, 0.35f), th.planetB, lvl / 5.f);
            if (d2 > 0.86f && lit > 0.4f) pc = mix(pc, rgb(255, 255, 255), 0.35f);
            out[y * w + x] = pc;
        }
    // hills / mountains / city
    float s1 = r.r(0, 100), s2 = r.r(0, 100);
    for (int layer = 0; layer < 2; layer++) {
        uint32_t col = layer == 0 ? th.hillFar : th.hillNear;
        float base = h * (layer == 0 ? 0.70f : 0.82f);
        if (th.hillStyle == 2 && layer == 1) {
            int x = 0; Rng b(seed + 99);
            while (x < w) {
                int bw = 8 + b.i(18), bh = (int)(h * (0.18f + b.f() * 0.32f));
                int top = h - bh;
                for (int yy = top; yy < h; yy++)
                    for (int xx = x; xx < std::min(w, x + bw); xx++) {
                        uint32_t c = col;
                        if (xx == x) c = mix(col, th.accent, 0.25f);
                        if (yy > top + 2 && ((xx - x) % 4 == 2) && ((yy - top) % 5 == 2) && h01(xx, yy, seed) < 0.35f)
                            c = mix(rgb(255, 230, 140), th.accent, h01(xx, yy, seed + 1) * 0.6f);
                        out[yy * w + xx] = c;
                    }
                if (b.f() < 0.3f) for (int yy = top - 6; yy < top; yy++) out[std::max(0, yy) * w + std::min(w - 1, x + bw / 2)] = col;
                x += bw + b.i(3);
            }
            continue;
        }
        for (int x = 0; x < w; x++) {
            float hh;
            if (th.hillStyle == 1)
                hh = base - std::fabs(std::sin(x * 0.045f + s1 + layer)) * 22 - vnoise(x * 0.12f, layer * 5.f, seed) * 10 - std::fabs(std::sin(x * 0.11f + s2)) * 8;
            else
                hh = base + std::sin(x * 0.028f + s1 + layer * 2) * 9 + std::sin(x * 0.073f + s2) * 5 + vnoise(x * 0.1f, layer * 3.f, seed) * 6;
            for (int y = std::max(0, (int)hh); y < h; y++) {
                uint32_t c = col;
                if (y - (int)hh < 1) c = mix(col, th.skyBot, 0.5f);
                else if (th.hillStyle == 1 && y - (int)hh < 3 && layer == 0) c = mix(col, rgb(255, 255, 255), 0.25f);
                else if (bayer(x, y) < 0.12f) c = mix(col, th.skyBot, 0.15f);
                out[y * w + x] = c;
            }
        }
    }
}

// ------------------------------------------------------------------ static tiles
static bool isWall(char c) { return c == '#' || c == '%'; }
static bool isPadC(char c) { return (c >= '1' && c <= '9') || c == 'F'; }

static inline void putS(int x, int y, uint32_t c, bool solid, bool hz = false) {
    if (x < 0 || y < 0 || x >= WW || y >= WH) return;
    LV.img[y * WW + x] = c;
    if (solid) LV.sSolid[y * WW + x] = 1;
    if (hz) LV.sHaz[y * WW + x] = 1;
}

// Draw a landing pad (static or on a platform) via callback put(x,y,color)
template <class F> static void paintPad(int x0, int y0, int w, char id, bool fuel, F put) {
    for (int py = 0; py < 8; py++)
        for (int px = 0; px < w; px++) {
            uint32_t c;
            bool end = px == 0 || px == w - 1, end2 = px == 1 || px == w - 2;
            if (py == 0) c = end ? rgb(150, 150, 164) : rgb(232, 232, 214);
            else if (py == 7) c = rgb(24, 24, 30);
            else if (end) c = rgb(124, 124, 140);
            else if (end2) c = rgb(78, 78, 94);
            else if (py == 1) c = fuel ? rgb(170, 50, 40) : rgb(78, 82, 100);
            else if (py == 6) c = fuel ? rgb(70, 10, 10) : rgb(30, 32, 42);
            else c = fuel ? rgb(120, 24, 22) : rgb(46, 50, 64);
            put(x0 + px, y0 + py, c);
        }
    char lab[8];
    if (fuel) snprintf(lab, sizeof lab, "FUEL"); else snprintf(lab, sizeof lab, w >= 24 ? "PAD %c" : "%c", id);
    int tw = (int)strlen(lab) * 4 - 1;
    uint32_t tc = fuel ? rgb(255, 240, 220) : rgb(255, 210, 60);
    tinyText(x0 + (w - tw) / 2, y0 + 2, lab, [&](int x, int y) { put(x, y, tc); });
}

static void paintMetal(int tx, int ty) {
    const Theme& th = *LV.th;
    const uint32_t* m = th.metal;
    bool N = !isWall(LV.T(tx, ty - 1)), S = !isWall(LV.T(tx, ty + 1)), Wx = !isWall(LV.T(tx - 1, ty)), E = !isWall(LV.T(tx + 1, ty));
    for (int py = 0; py < 8; py++)
        for (int px = 0; px < 8; px++) {
            int gx = tx * 8 + px, gy = ty * 8 + py;
            int lx = gx & 15, ly = gy & 15;
            uint32_t c = m[2];
            // gentle vertical panel shading
            if (ly > 9 && bayer(gx, gy) < (ly - 9) / 10.f) c = mix(m[2], m[1], 0.6f);
            if (ly < 4 && bayer(gx, gy) < (4 - ly) / 8.f) c = mix(m[2], m[3], 0.5f);
            if (lx == 0 || ly == 0) c = m[3];
            if (lx == 15 || ly == 15) c = m[1];
            if ((lx == 3 || lx == 12) && (ly == 3 || ly == 12)) c = m[4];
            if ((lx == 4 || lx == 13) && (ly == 4 || ly == 13)) c = m[1];
            if (N) { if (py == 0) c = m[4]; else if (py == 1) c = m[3]; }
            if (Wx) { if (px == 0) c = m[4]; else if (px == 1 && py > 0) c = mix(c, m[3], 0.6f); }
            if (S) { if (py == 7) c = m[0]; else if (py == 6) c = m[1]; }
            if (E) { if (px == 7) c = m[0]; else if (px == 6 && py < 7) c = mix(c, m[1], 0.7f); }
            bool hole = (N && Wx && px == 0 && py == 0) || (N && E && px == 7 && py == 0) ||
                        (S && Wx && px == 0 && py == 7) || (S && E && px == 7 && py == 7);
            if (!hole) putS(gx, gy, c, true);
        }
}
static void paintRock(int tx, int ty) {
    const Theme& th = *LV.th;
    const uint32_t* r = th.rock;
    bool N = !isWall(LV.T(tx, ty - 1)), S = !isWall(LV.T(tx, ty + 1)), Wx = !isWall(LV.T(tx - 1, ty)), E = !isWall(LV.T(tx + 1, ty));
    int seed = LV.idx * 31 + 5;
    for (int py = 0; py < 8; py++)
        for (int px = 0; px < 8; px++) {
            int gx = tx * 8 + px, gy = ty * 8 + py;
            // rough organic edges
            float edge = h01(gx, gy, seed + 1);
            if (N && py == 0 && edge < 0.35f) continue;
            if (S && py == 7 && edge < 0.35f) continue;
            if (Wx && px == 0 && edge < 0.35f) continue;
            if (E && px == 7 && edge < 0.35f) continue;
            if (N && Wx && px + py < 2) continue;
            if (N && E && (7 - px) + py < 2) continue;
            if (S && Wx && px + (7 - py) < 2) continue;
            if (S && E && (7 - px) + (7 - py) < 2) continue;
            float n = vnoise(gx * 0.22f, gy * 0.22f, seed) * 0.65f + vnoise(gx * 0.6f, gy * 0.6f, seed + 2) * 0.35f;
            int idx = std::clamp((int)std::floor(n * 3.4f + bayer(gx, gy) * 0.8f - 0.2f), 0, 3);
            uint32_t c = r[std::max(1, idx)];
            float cr = vnoise(gx * 0.12f, gy * 0.12f, seed + 7);
            if (std::fabs(cr - 0.5f) < 0.025f) c = r[0];
            if (N && py <= 1) c = py == 0 ? r[4] : r[3];
            else if (N && py == 2 && bayer(gx, gy) < 0.5f) c = r[3];
            if (Wx && px == 0 && !(N && py <= 1)) c = r[3];
            if (S && py >= 6) c = py == 7 ? r[0] : mix(c, r[0], 0.5f);
            if (E && px == 7 && !(N && py == 0)) c = r[1];
            putS(gx, gy, c, true);
        }
}
static void paintGirder(int tx, int ty) {
    const uint32_t* m = LV.th->metal;
    for (int py = 0; py < 8; py++)
        for (int px = 0; px < 8; px++) {
            int gx = tx * 8 + px, gy = ty * 8 + py;
            uint32_t c = 0;
            if (py == 0) c = m[4];
            else if (py == 1) c = m[3];
            else if (py == 6) c = m[2];
            else if (py == 7) c = m[0];
            else if (((gx + gy) & 7) == 0 || ((gx - gy) & 7) == 0) c = m[2];
            else if (((gx + gy) & 7) == 1 || ((gx - gy) & 7) == 1) c = m[1];
            if (c) putS(gx, gy, c, true);
        }
}
static void paintPipe(int tx, int ty, bool vertical) {
    const uint32_t* m = LV.th->metal;
    for (int py = 0; py < 8; py++)
        for (int px = 0; px < 8; px++) {
            int gx = tx * 8 + px, gy = ty * 8 + py;
            int a = vertical ? px : py, b = vertical ? gy : gx;
            if (a < 2 || a > 5) continue;
            uint32_t c = a == 2 ? m[1] : (a == 3 ? m[3] : (a == 4 ? m[2] : m[0]));
            if ((b & 15) == 0 || (b & 15) == 1) c = a == 3 ? m[4] : m[2];
            uint32_t bg = LV.img[gy * WW + gx];
            putS(gx, gy, mix(bg, c, 0.55f), false);
        }
}
static void paintSpikes(int tx, int ty) {
    int dir = 0;    // 0 up, 1 down, 2 right, 3 left
    if (isWall(LV.T(tx, ty + 1))) dir = 0;
    else if (isWall(LV.T(tx, ty - 1))) dir = 1;
    else if (isWall(LV.T(tx - 1, ty))) dir = 2;
    else if (isWall(LV.T(tx + 1, ty))) dir = 3;
    for (int k = 0; k < 2; k++)
        for (int a = 0; a < 8; a++)   // a = distance from base (0 base .. 7 tip)
            for (int b = 0; b < 4; b++) {
                int half = a < 4 ? 2 : (a < 7 ? 1 : 0);
                int bb = b - 2;                         // -2..1
                if (bb < -half || bb >= half) { if (!(a == 7 && (b == 1 || b == 2) && false)) continue; }
                if (a == 7) continue;
                uint32_t c = b < 2 ? rgb(220, 224, 236) : rgb(110, 112, 130);
                if (a == 6) c = rgb(255, 255, 255);
                if (a == 0) c = rgb(70, 70, 84);
                int u = k * 4 + b, v = 7 - a;           // along base / perpendicular
                int px, py;
                switch (dir) {
                    case 0: px = u; py = v; break;
                    case 1: px = u; py = 7 - v; break;
                    case 2: px = 7 - v; py = u; break;
                    default: px = v; py = u; break;
                }
                putS(tx * 8 + px, ty * 8 + py, c, false, true);
            }
}

static void buildLevel(int idx) {
    Level& L = LV;
    L = Level();
    L.idx = idx;
    L.def = &LEVELDEFS[idx];
    L.th = &THEMES[L.def->theme % 7];
    L.img.assign(WW * WH, 0);
    L.sSolid.assign(WW * WH, 0); L.sHaz.assign(WW * WH, 0);
    L.solid.assign(WW * WH, 0); L.haz.assign(WW * WH, 0);
    for (int y = 0; y < MH; y++)
        for (int x = 0; x < MW; x++) {
            L.tile[y][x] = L.def->map[y][x];
            L.windX[y][x] = L.windY[y][x] = 0;
        }
    genBackground(*L.th, L.img.data(), WW, WH, idx + 1);

    int tpAx0 = 99, tpAy0 = 99, tpAx1 = -1, tpAy1 = -1, tpBx0 = 99, tpBy0 = 99, tpBx1 = -1, tpBy1 = -1;
    for (int y = 0; y < MH; y++)
        for (int x = 0; x < MW; x++) {
            char c = L.tile[y][x];
            switch (c) {
                case '#': paintMetal(x, y); break;
                case '%': paintRock(x, y); break;
                case '=': paintGirder(x, y); break;
                case '|': paintPipe(x, y, true); break;
                case '-': paintPipe(x, y, false); break;
                case 'X': paintSpikes(x, y); break;
                case 'S': L.switches.push_back({x * 8, y * 8}); break;
                case '^': L.windY[y][x] = -95; break;
                case 'v': L.windY[y][x] = 60; break;
                case '<': L.windX[y][x] = -70; break;
                case '>': L.windX[y][x] = 70; break;
                case 'T': tpAx0 = std::min(tpAx0, x); tpAy0 = std::min(tpAy0, y); tpAx1 = std::max(tpAx1, x); tpAy1 = std::max(tpAy1, y); break;
                case 't': tpBx0 = std::min(tpBx0, x); tpBy0 = std::min(tpBy0, y); tpBx1 = std::max(tpBx1, x); tpBy1 = std::max(tpBy1, y); break;
                default: break;
            }
        }
    if (tpAx1 >= 0 && tpBx1 >= 0) {
        L.hasTp = true;
        L.tpA = {tpAx0 * 8, tpAy0 * 8, (tpAx1 - tpAx0 + 1) * 8, (tpAy1 - tpAy0 + 1) * 8};
        L.tpB = {tpBx0 * 8, tpBy0 * 8, (tpBx1 - tpBx0 + 1) * 8, (tpBy1 - tpBy0 + 1) * 8};
    }
    // switches are pressure plates on the floor: not solid, drawn dynamically (see drawWorld)
    // static pads
    for (int y = 0; y < MH; y++) {
        int x = 0;
        while (x < MW) {
            char c = L.tile[y][x];
            if (isPadC(c)) {
                int x0 = x;
                while (x < MW && L.tile[y][x] == c) x++;
                PadE p{x0 * 8, y * 8, (x - x0) * 8, c, -1};
                L.pads.push_back(p);
                paintPad(p.x, p.y, p.w, c, c == 'F', [&](int px, int py, uint32_t col) { putS(px, py, col, true); });
            } else x++;
        }
    }
    // spawn slots (static pads only), numbered first then fuel
    std::vector<int> order;
    for (int i = 0; i < (int)L.pads.size(); i++) order.push_back(i);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        char ca = L.pads[a].id == 'F' ? ':' : L.pads[a].id, cb = L.pads[b].id == 'F' ? ':' : L.pads[b].id;
        return ca < cb; });
    for (int pi : order) {
        PadE& p = L.pads[pi];
        int n = std::max(1, (p.w - 4) / 20);
        int sp = p.w / n;
        for (int i = 0; i < n; i++) L.slots.push_back({pi, p.x + sp * i + (sp - 16) / 2});
    }
    // platforms & their pads
    for (auto& d : L.def->plats) {
        PlatE pe; pe.d = d; pe.w = d.w * 8;
        float u = 0.5f - 0.5f * std::cos(2 * 3.14159265f * d.phase);
        pe.x = (int)std::lround((d.x0 + (d.x1 - d.x0) * u) * 8);
        pe.y = (int)std::lround((d.y0 + (d.y1 - d.y0) * u) * 8);
        pe.t = 0;
        if (d.pad) {
            pe.pad = (int)L.pads.size();
            L.pads.push_back({pe.x, pe.y, pe.w, d.pad, (int)L.plats.size()});
        }
        L.plats.push_back(pe);
    }
    for (auto& d : L.def->mines) { MineE me; me.d = d; L.mines.push_back(me); }
}

// ------------------------------------------------------------------ dynamic masks
static inline bool doorSolid(char c, const Level& L) { return (c == 'D' && !L.switchOn) || (c == 'E' && L.switchOn); }

static void rebuildMasks() {
    Level& L = LV;
    L.solid = L.sSolid;
    L.haz = L.sHaz;
    for (int ty = 0; ty < MH; ty++)
        for (int tx = 0; tx < MW; tx++) {
            char c = L.tile[ty][tx];
            if (doorSolid(c, L)) {
                for (int py = 0; py < 8; py++) for (int px = 0; px < 8; px++) L.solid[(ty * 8 + py) * WW + tx * 8 + px] = 1;
            } else if (c == 'G') {
                // solid exactly where the two sliding door halves are drawn
                int gx0 = 0, gx1 = 0;
                for (int x = 0; x < MW; x++) if (L.tile[0][x] == 'G') { if (!gx1) gx0 = x * 8; gx1 = x * 8 + 8; }
                int half = (gx1 - gx0) / 2, open = (int)(half * L.gateAnim);
                for (int py = 0; py < 8; py++)
                    for (int px = 0; px < 8; px++) {
                        int x = tx * 8 + px;
                        if (x < gx0 + half - open || x >= gx0 + half + open) L.solid[(ty * 8 + py) * WW + x] = 1;
                    }
            } else if ((c == 'L' && L.laserA) || (c == 'l' && L.laserB)) {
                bool vert = L.T(tx, ty - 1) == c || L.T(tx, ty + 1) == c;
                for (int py = 0; py < 8; py++)
                    for (int px = 0; px < 8; px++) {
                        int a = vert ? px : py;
                        if (a >= 2 && a <= 5) L.haz[(ty * 8 + py) * WW + tx * 8 + px] = 1;
                    }
            }
        }
    for (auto& p : L.plats)
        for (int py = 0; py < 8; py++)
            for (int px = 0; px < p.w; px++) {
                int x = p.x + px, y = p.y + py;
                if (x >= 0 && y >= 0 && x < WW && y < WH) L.solid[y * WW + x] = 1;
            }
    for (auto& m : L.mines) {
        int mx = (int)std::lround(m.x) - 4, my = (int)std::lround(m.y) - 4;
        for (int j = 0; j < 9; j++)
            for (int i = 0; i < 9; i++)
                if (MINE_SPR[j][i] != '.') {
                    int x = mx + i, y = my + j;
                    if (x >= 0 && y >= 0 && x < WW && y < WH) L.haz[y * WW + x] = 1;
                }
    }
}

// sprite mask vs world mask. Outside world is solid (except above when openTop)
static bool hitMask(const std::vector<uint8_t>& mask, const uint8_t* spr, int sw, int sh, int px, int py, bool openTop, bool outsideSolid = true) {
    for (int j = 0; j < sh; j++) {
        int wy = py + j;
        for (int i = 0; i < sw; i++) {
            if (!spr[j * sw + i]) continue;
            int wx = px + i;
            if (wx < 0 || wx >= WW || wy >= WH || wy < 0) {
                if (wy < 0 && openTop) continue;
                if (outsideSolid) return true;
                continue;
            }
            if (mask[wy * WW + wx]) return true;
        }
    }
    return false;
}
static uint8_t bodyOnly[2][16 * 10], gearOnly[2][3][16 * 10];
static void buildSubMasks() {
    for (int f = 0; f < 2; f++)
        for (int i = 0; i < 160; i++) {
            bodyOnly[f][i] = i < 128 ? taxiMask[f][2][i] : 0;
            for (int g = 0; g < 3; g++) gearOnly[f][g][i] = i >= 128 ? taxiMask[f][g][i] : 0;
        }
}

// ------------------------------------------------------------------ effects
static void addPart(float x, float y, float vx, float vy, float life, uint32_t c, float g = 0, int size = 1) {
    if (parts.size() > 2500) return;
    parts.push_back({x, y, vx, vy, life, life, c, g, size});
}
static void addText(float x, float y, const std::string& s, uint32_t c) { ftexts.push_back({x, y, 1.6f, s, c}); }

static void explode(float x, float y, int pc) {
    for (int i = 0; i < 36; i++) {
        float a = rng.r(0, 6.283f), s = rng.r(20, 110);
        uint32_t c = i % 3 == 0 ? PCOL[pc].main : (i % 3 == 1 ? PCOL[pc].dark : rgb(150, 150, 165));
        addPart(x, y, std::cos(a) * s, std::sin(a) * s - 30, rng.r(0.8f, 2.0f), c, 90, rng.f() < 0.3f ? 2 : 1);
    }
    for (int i = 0; i < 60; i++) {
        float a = rng.r(0, 6.283f), s = rng.r(10, 70);
        uint32_t c = mix(rgb(255, 255, 200), rgb(255, 60, 0), rng.f());
        addPart(x, y, std::cos(a) * s, std::sin(a) * s, rng.r(0.2f, 0.7f), c, -10, rng.f() < 0.5f ? 2 : 1);
    }
    for (int i = 0; i < 18; i++)
        addPart(x + rng.r(-6, 6), y + rng.r(-4, 4), rng.r(-10, 10), rng.r(-25, -5), rng.r(0.8f, 1.6f), rgb(90, 90, 100), -8, 2);
    shakeT = std::max(shakeT, 0.35f);
}

// ------------------------------------------------------------------ players & passengers
static int numJoined() { int n = 0; for (auto& p : P) if (p.joined) n++; return n; }

static int lifeSteals = 0;   // checked by the self-test
static void killPlayer(int i, const char* why) {
    Player& p = P[i];
    if (!p.alive) return;
    p.alive = false; p.deadT = 2.2f; p.lives--;
    long fee = std::min<long>(CRASH_FEE, p.score);
    p.score -= fee;
    p.fuel = FUEL_MAX;            // the replacement taxi comes with a full tank
    // multiplayer: instead of dropping out, take a life from whoever has the most
    // (never from a player who is on their last life)
    if (p.lives <= 0 && numJoined() > 1) {
        int donor = -1;
        for (int j = 0; j < MAXP; j++)
            if (j != i && P[j].joined && P[j].lives >= 2 && (donor < 0 || P[j].lives > P[donor].lives)) donor = j;
        if (donor >= 0) {
            P[donor].lives--; p.lives = 1; lifeSteals++;
            addText(p.cx(), p.y - 14, "LIFE FROM P" + std::to_string(donor + 1) + "!", PCOL[i].light);
        }
    }
    if (getenv("BOTDBG")) printf("  KILL p%d %s at %.1f,%.1f v=%.1f,%.1f gear=%d t=%.1f\n", i, why ? why : "-", p.x, p.y, p.vx, p.vy, p.gear, LV.time);
    explode(p.cx(), p.cy(), i);
    audio.play(SFX_EXPLODE, 1.f, panOf(p.cx()));
    if (why) addText(p.cx(), p.y - 4, why, PCOL[i].light);
    if (fee > 0) addText(p.cx(), p.y + 6, "-$" + std::to_string(fee), rgb(255, 90, 90));
    if (p.pass >= 0) {
        Passenger& q = PASS[p.pass];
        audio.say(q.voice, VC_OUCH, panOf(p.cx()));
        if (q.up) upSpawnT = 2.0f;
        q.state = PS_NONE;
        p.pass = -1;
    }
    p.landed = false; p.landPad = p.landPlat = -1;
    audio.thrust(i, 0, 0);
}

static void placeOnSlot(Player& p, const Slot& s) {
    PadE& pad = LV.pads[s.pad];
    p.x = (float)s.x; p.y = (float)(pad.y - 10);
    p.vx = p.vy = 0; p.gear = true; p.gearExt = 2; p.landed = true; p.landPad = s.pad; p.landPlat = -1;
    p.alive = true; p.pass = -1; p.tpCd = 0; p.landedT = 1; p.noFuelT = 0;   // fuel carries over
    p.face = p.x + 8 < WW / 2 ? 0 : 1;
}

static bool slotFree(const Slot& s, int self) {
    PadE& pad = LV.pads[s.pad];
    float sx = s.x + 8.f, sy = pad.y - 5.f;
    for (int j = 0; j < MAXP; j++) {
        if (j == self || !P[j].joined || !P[j].alive) continue;
        if (std::fabs(P[j].cx() - sx) < 26 && std::fabs(P[j].cy() - sy) < 22) return false;
    }
    return true;
}
static void respawn(int i) {
    float best = -1; int bi = -1;
    for (int k = 0; k < (int)LV.slots.size(); k++) {
        const Slot& s = LV.slots[k];
        if (!slotFree(s, i)) continue;
        float sx = s.x + 8.f, sy = LV.pads[s.pad].y - 5.f;
        float md = 1e9f;
        for (int j = 0; j < MAXP; j++)
            if (j != i && P[j].joined && P[j].alive) md = std::min(md, std::hypot(P[j].cx() - sx, P[j].cy() - sy));
        // avoid hazards near the slot
        bool hz = false;
        for (int yy = LV.pads[s.pad].y - 14; yy < LV.pads[s.pad].y && !hz; yy++)
            for (int xx = s.x - 2; xx < s.x + 18; xx++)
                if (xx >= 0 && yy >= 0 && xx < WW && yy < WH && LV.haz[yy * WW + xx]) { hz = true; break; }
        if (hz) md *= 0.1f;
        md += rng.f();
        if (md > best) { best = md; bi = k; }
    }
    if (bi < 0) return;  // try again next frame
    placeOnSlot(P[i], LV.slots[bi]);
    P[i].invuln = 2.5f;
    P[i].deadT = 0;
    audio.play(SFX_JOIN, 0.5f, panOf(P[i].cx()));
}

static std::vector<int> numberedPads() {
    std::vector<int> v;
    for (int i = 0; i < (int)LV.pads.size(); i++) if (LV.pads[i].id != 'F') v.push_back(i);
    return v;
}
static int padByChar(char c) { for (int i = 0; i < (int)LV.pads.size(); i++) if (LV.pads[i].id == c) return i; return -1; }

static void spawnPassenger(bool up) {
    std::vector<int> np = numberedPads();
    std::vector<int> cand;
    for (int pi : np) {
        bool busy = false;
        for (auto& q : PASS) if (q.state == PS_WAIT && q.pad == pi) busy = true;
        for (auto& q : PASS) if (q.state == PS_RIDE && padByChar(q.dest) == pi) busy = true;
        if (!busy) cand.push_back(pi);
    }
    if (cand.empty()) return;
    {
        std::vector<int> freeC;
        for (int pi : cand) {
            bool parked = false;
            for (auto& p : P) if (p.joined && p.alive && p.landed && p.landPad == pi) parked = true;
            if (!parked) freeC.push_back(pi);
        }
        if (!freeC.empty()) cand = freeC;
    }
    int slot = -1;
    for (int i = 0; i < (int)PASS.size(); i++) if (PASS[i].state == PS_NONE) { slot = i; break; }
    if (slot < 0) return;
    Passenger& q = PASS[slot];
    q = Passenger();
    q.pad = cand[rng.i((int)cand.size())];
    q.up = up;
    if (!up) {
        std::vector<int> d;
        for (int pi : np) if (pi != q.pad) d.push_back(pi);
        q.dest = LV.pads[d[rng.i((int)d.size())]].id;
    }
    const PadE& pad = LV.pads[q.pad];
    q.relx = rng.f() < 0.5f ? 0.f : (float)(pad.w - 6);    // at the very end of the pad
    q.homeX = q.relx;
    q.state = PS_WAIT; q.heyT = 0.2f; q.anim = 0; q.waveT = 5.f + rng.f() * 4.f;
    q.gender = rng.i(2);
    q.style = q.gender ? 4 + rng.i(3) : rng.i(4);
    q.skin = SKIN_TONES[rng.i(6)];
    q.hair = HAIR_COLS[rng.i(7)];
    q.top = TOP_COLS[rng.i(10)];
    q.bottom = q.gender ? SKIRT_COLS[rng.i(6)] : PANTS_COLS[rng.i(5)];
    q.voice = q.gender ? 1 : (rng.f() < 0.5f ? 0 : 2);   // voice 1 is the female voice
    q.dir = q.relx < pad.w / 2 ? 1.f : -1.f;
}

// What the passenger aboard would pay if delivered right now: fare minus rough-landing
// cuts (never below 0) plus the shrinking tip (lost after a rough landing).
static int currentFare(const Player& p) {
    if (p.pass < 0) return 0;
    const Passenger& q = PASS[p.pass];
    if (q.up) return GATE_BONUS;
    return std::max(0, FARE_BASE - q.fareCut) + (q.noTip ? 0 : (int)p.tip);
}
// Top-bar money text: the score, or "+$N" counting down while carrying a passenger.
static std::string hudMoneyText(const Player& p) {
    char buf[24];
    if (p.alive && p.pass >= 0) snprintf(buf, sizeof buf, "+$%d", currentFare(p));
    else snprintf(buf, sizeof buf, "$%ld", std::min(p.score, 999999L));
    return buf;
}

static void deliver(int pi) {
    Player& p = P[pi];
    Passenger& q = PASS[p.pass];
    int pay = currentFare(p);
    p.score += pay; p.fares++; p.levelFares++;
    LV.delivered++;
    q.state = PS_EXIT; q.taxi = -1; q.anim = 0;
    q.pad = p.landPad;
    const PadE& pad = LV.pads[p.landPad];
    float lo = 1.f, hi = (float)(pad.w - 7);
    q.relx = std::clamp(p.cx() - 3 - pad.x, lo, hi);
    q.exitX = (q.relx - lo < hi - q.relx) ? lo : hi;          // nearest edge of the pad
    q.dir = q.exitX > q.relx ? 1.f : -1.f;
    p.pass = -1;
    audio.say(q.voice, VC_THANKS, panOf(p.cx()));
    audio.play(SFX_TIP, 0.8f, panOf(p.cx()));
    addText(p.cx(), p.y - 6, "$" + std::to_string(pay), PCOL[pi].light);
}

// A floor switch is pressed by a taxi resting on the floor right under it with its gear
// over the plate (the switch tile sits directly on the floor).
static bool switchPressedBy(const Player& p, SDL_Point s) {
    int bottom = (int)std::lround(p.y) + 10;
    return p.landed && bottom == s.y + 8 && p.x + 14 >= s.x + 1 && p.x + 1 <= s.x + 6;
}

static int padReminderCount = 0;   // checked by the self-test
static void onLanded(int i) {
    Player& p = P[i];
    p.landPad = -1; p.landPlat = -1;
    int bottom = (int)std::lround(p.y) + 10;
    for (int k = 0; k < (int)LV.plats.size(); k++) {
        PlatE& pl = LV.plats[k];
        if (pl.y == bottom && p.cx() >= pl.x - 6 && p.cx() <= pl.x + pl.w + 6) p.landPlat = k;
    }
    for (int k = 0; k < (int)LV.pads.size(); k++) {
        PadE& pd = LV.pads[k];
        if (pd.y == bottom && p.cx() >= pd.x && p.cx() <= pd.x + pd.w) p.landPad = k;
    }
    // floor switches
    for (auto& s : LV.switches) {
        if (switchPressedBy(p, s) && LV.switchCd <= 0) {
            LV.switchOn = !LV.switchOn; LV.switchCd = 0.6f;
            audio.play(SFX_SWITCH, 0.8f, panOf(p.cx()));
            audio.play(SFX_DOOR, 0.6f, 0);
            addText(s.x + 4.f, s.y - 6.f, "CLICK", rgb(255, 255, 255));
        }
    }
    // dust
    for (int k = 0; k < 8; k++) addPart(p.cx() + rng.r(-8, 8), (float)bottom - 1, rng.r(-25, 25), rng.r(-12, -2), rng.r(0.3f, 0.6f), rgb(170, 170, 180), 20);
    if (p.pass >= 0 && p.landPad >= 0) {
        Passenger& q = PASS[p.pass];
        char id = LV.pads[p.landPad].id;
        if (!q.up && id == q.dest) deliver(i);
        else if (!q.up && id != 'F') {
            // wrong pad: the passenger reminds the driver where they want to go
            audio.say(q.voice, VC_PAD1 + (q.dest - '1'), panOf(p.cx()));
            addText(p.cx(), p.y - 6, std::string("PAD ") + q.dest, PCOL[i].light);
            padReminderCount++;
        }
    }
}

// True when every bottom pixel of the extended landing gear (sprite row 9, both feet)
// has a solid pixel directly underneath it.
static bool feetFullySupported(int face, int x, int y) {
    const uint8_t* g = gearOnly[face][2];
    for (int c = 0; c < 16; c++) {
        if (!g[9 * 16 + c]) continue;
        int wx = x + c, wy = y + 10;
        if (wx < 0 || wx >= WW || wy < 0 || wy >= WH || !LV.solid[wy * WW + wx]) return false;
    }
    return true;
}

// The carrier of the "Up, please!" passenger leaves first; after that everybody may exit.
static bool canExit(const Player& p) {
    return LV.gateAnim >= 1.f && ((p.pass >= 0 && PASS[p.pass].up) || LV.upDone);
}

static void updateTaxi(int i, float dt) {
    Player& p = P[i];
    const Input& in = p.cur;
    if (p.invuln > 0) p.invuln -= dt;
    if (p.roughT > 0) p.roughT -= dt;
    if (p.tpCd > 0) p.tpCd -= dt;

    // landing gear toggle (A / B)
    if (p.pressed(&Input::a) || p.pressed(&Input::b)) {
        if (!p.landed) {
            p.gear = !p.gear;
            audio.play(p.gear ? SFX_GEAR_DOWN : SFX_GEAR_UP, 0.6f, panOf(p.cx()));
        } else audio.play(SFX_BUMP, 0.4f, panOf(p.cx()));
    }
    p.gearExt += (p.gear ? 1 : -1) * dt * 10.f;
    p.gearExt = std::clamp(p.gearExt, 0.f, 2.f);

    bool hasFuel = p.fuel > 0;
    p.thU = in.u && hasFuel; p.thD = in.d && hasFuel && !p.landed; p.thL = in.l && hasFuel && !p.landed && !p.gear; p.thR = in.r && hasFuel && !p.landed && !p.gear;
    // as in the original: no sideways thrust while the landing gear is down (or extending),
    // so lining up for a landing has to be done before lowering the gear
    if (p.thL && !p.thR) p.face = 1;
    if (p.thR && !p.thL) p.face = 0;
    float burn = (p.thU ? 1.f : 0.f) + ((p.thL || p.thR) ? 0.6f : 0.f) + (p.thD ? 0.5f : 0.f);
    if (burn > 0) {
        p.fuel = std::max(0.f, p.fuel - burn * 2.0f * dt);
        if (p.fuel < 20) { p.lowFuelT -= dt; if (p.lowFuelT <= 0) { audio.play(SFX_LOWFUEL, 0.5f, panOf(p.cx())); p.lowFuelT = 1.0f; } }
    }
    audio.thrust(i, (p.thU ? 0.7f : 0.f) + ((p.thL || p.thR || p.thD) ? 0.35f : 0.f), panOf(p.cx()));

    // exhaust particles
    if (p.thU && rng.f() < 0.7f) addPart(p.cx() + rng.r(-1, 1), p.y + 11, rng.r(-8, 8) + p.vx * 0.3f, rng.r(40, 80) + p.vy * 0.3f, rng.r(0.15f, 0.35f), mix(rgb(255, 230, 120), rgb(255, 80, 0), rng.f()), 0);
    if ((p.thL || p.thR) && rng.f() < 0.5f) {
        float ex = p.face == 0 ? p.x - 1 : p.x + 16, dir = p.face == 0 ? -1.f : 1.f;
        addPart(ex, p.y + 5 + rng.r(-1, 1), dir * rng.r(40, 70) + p.vx * 0.3f, rng.r(-6, 6), rng.r(0.1f, 0.25f), mix(rgb(255, 230, 120), rgb(255, 80, 0), rng.f()), 0);
    }

    if (p.landed) {
        p.landedT += dt;
        // refuel: $FUEL_PRICE per unit, only while you can pay for it
        bool onFuelPad = p.landPad >= 0 && LV.pads[p.landPad].id == 'F';
        if (onFuelPad && p.fuel < FUEL_MAX) {
            p.refuelAcc += 35.f * dt;
            bool pumped = false;
            while (p.refuelAcc >= 1.f && p.fuel < FUEL_MAX && p.score >= FUEL_PRICE) {
                p.fuel = std::min(FUEL_MAX, p.fuel + 1.f); p.score -= FUEL_PRICE; p.refuelAcc -= 1.f; pumped = true;
            }
            if (p.refuelAcc > 1.f) p.refuelAcc = 1.f;
            if (pumped) {
                p.fuelSndT -= dt;
                if (p.fuelSndT <= 0) { audio.play(SFX_FUEL, 0.5f, panOf(p.cx())); p.fuelSndT = 0.12f; }
            } else if (p.score < FUEL_PRICE && !p.noMoneyShown) {
                addText(p.cx(), p.y - 6, "NO MONEY!", rgb(255, 90, 90));
                audio.play(SFX_LOWFUEL, 0.6f, panOf(p.cx()));
                p.noMoneyShown = true;
            }
        }
        // stranded: empty tank and no way to refuel here -> self-destruct (costs a life, fresh tank)
        if (p.fuel <= 0 && !(onFuelPad && p.score >= FUEL_PRICE)) {
            if (p.noFuelT == 0) addText(p.cx(), p.y - 6, "OUT OF FUEL!", rgb(255, 90, 90));
            p.noFuelT += dt;
            if (p.noFuelT >= 4.f) { killPlayer(i, "OUT OF FUEL!"); return; }
        } else p.noFuelT = 0;
        // still supported?
        if (!hitMask(LV.solid, gearOnly[p.face][2], 16, 10, (int)std::lround(p.x), (int)std::lround(p.y) + 1, false)) {
            p.landed = false; p.landPad = p.landPlat = -1;
        } else if (p.thU) {
            if (p.landPlat >= 0) { p.vx = LV.plats[p.landPlat].vx; p.vy = std::min(0.f, LV.plats[p.landPlat].vy); }
            else { p.vx = 0; p.vy = 0; }
            p.landed = false; p.landPad = p.landPlat = -1;
            p.vy -= 18; p.y -= 1; p.airT = 0;
        } else return;
    }

    p.airT += dt;
    float g = G0 * LV.def->gravity;
    int tx = std::clamp((int)(p.cx() / 8), 0, MW - 1), ty = std::clamp((int)(p.cy() / 8), 0, MH - 1);
    float side = THRUST_SIDE;
    float ax = ((p.thR ? 1 : 0) - (p.thL ? 1 : 0)) * side + LV.windX[ty][tx];
    float ay = g - (p.thU ? THRUST_UP : 0) + (p.thD ? THRUST_DOWN : 0) + LV.windY[ty][tx];
    p.vx += ax * dt; p.vy += ay * dt;
    p.vx -= p.vx * (p.gear ? 0.55f : 0.35f) * dt;
    p.vy -= p.vy * 0.08f * dt;
    p.vx = std::clamp(p.vx, -MAXVX, MAXVX); p.vy = std::clamp(p.vy, -MAXVY, MAXVY);

    bool openTop = canExit(p);
    float sx = p.x, sy = p.y;
    float nx = p.x + p.vx * dt, ny = p.y + p.vy * dt;
    int steps = (int)std::ceil(std::max(std::fabs(nx - sx), std::fabs(ny - sy)) / 0.75f) + 1;
    const int gr = p.gearRows();
    const uint8_t* full = taxiMask[p.face][gr];
    for (int s = 1; s <= steps; s++) {
        float fx = sx + (nx - sx) * s / steps, fy = sy + (ny - sy) * s / steps;
        if (!openTop && fy < 0) { fy = 0; ny = 0; p.vy = std::max(p.vy, 0.f); }
        int ix = (int)std::lround(fx), iy = (int)std::lround(fy);
        if (hitMask(LV.haz, full, 16, 10, ix, iy, openTop, false)) { killPlayer(i, "ZAPPED!"); return; }
        if (hitMask(LV.solid, bodyOnly[p.face], 16, 10, ix, iy, openTop)) { killPlayer(i, "CRASH!"); return; }
        if (gr > 0 && hitMask(LV.solid, gearOnly[p.face][gr], 16, 10, ix, iy, openTop)) {
            if (!p.gear) { killPlayer(i, "CRASH!"); return; }   // gear still visible but retracting
            // landing speed is judged relative to the surface (moving platforms!)
            float svx = 0, svy = 0;
            for (auto& pl : LV.plats)
                if (pl.y >= iy + 6 && pl.y <= iy + 12 && ix + 14 >= pl.x && ix + 2 <= pl.x + pl.w) { svx = pl.vx; svy = pl.vy; }
            float rvx = p.vx - svx, rvy = p.vy - svy;
            if (rvy > -14 && rvy <= LAND_VY && std::fabs(rvx) <= LAND_VX) {
                // touching down locks the gear fully out; lift the taxi so it rests on it
                p.gearExt = 2;
                int k = 0;
                for (; k <= 4; k++) if (!hitMask(LV.solid, gearOnly[p.face][2], 16, 10, ix, iy - k, openTop)) break;
                if (k > 4 || hitMask(LV.solid, bodyOnly[p.face], 16, 10, ix, iy - k, openTop)) { killPlayer(i, "CRASH!"); return; }
                // every pixel of the gear's feet must rest on solid ground, or it's a crash
                if (!feetFullySupported(p.face, ix, iy - k)) { killPlayer(i, "BAD LANDING!"); return; }
                if (rvy > ROUGH_VY) {
                    // rough landing: the taxi shakes; a passenger aboard loses the tip and part of the fare
                    p.roughT = 0.45f;
                    audio.play(SFX_BUMP, 1.f, panOf(p.cx()));
                    addText(p.cx(), p.y - 14, "ROUGH LANDING!", rgb(255, 200, 80));
                    if (p.pass >= 0 && !PASS[p.pass].up) {
                        Passenger& q = PASS[p.pass];
                        q.noTip = true; q.fareCut += ROUGH_FARE_CUT;
                        addText(p.cx(), p.y - 4, "NO TIP! -$" + std::to_string(ROUGH_FARE_CUT), rgb(255, 90, 90));
                    }
                }
                p.x = (float)ix; p.y = (float)(iy - k);
                p.vx = p.vy = 0; p.landed = true; p.landedT = 0; p.noMoneyShown = false; p.noFuelT = 0;
                audio.play(SFX_LAND, 0.7f, panOf(p.cx()));
                onLanded(i);
                return;
            }
            killPlayer(i, "TOO FAST!");
            return;
        }
    }
    p.x = nx; p.y = ny;

    // teleporters
    if (LV.hasTp && p.tpCd <= 0) {
        SDL_Rect* from = nullptr; SDL_Rect* to = nullptr;
        int cx = (int)p.cx(), cy = (int)p.cy();
        SDL_Point c{cx, cy};
        if (SDL_PointInRect(&c, &LV.tpA)) { from = &LV.tpA; to = &LV.tpB; }
        else if (SDL_PointInRect(&c, &LV.tpB)) { from = &LV.tpB; to = &LV.tpA; }
        if (from) {
            for (int k = 0; k < 20; k++) addPart(p.cx(), p.cy(), rng.r(-60, 60), rng.r(-60, 60), 0.4f, rgb(255, 200, 255), 0);
            p.x = to->x + to->w / 2.f - 8; p.y = to->y + to->h / 2.f - 5;
            for (int k = 0; k < 20; k++) addPart(p.cx(), p.cy(), rng.r(-60, 60), rng.r(-60, 60), 0.4f, rgb(200, 255, 255), 0);
            p.tpCd = 1.0f;
            audio.play(SFX_TELEPORT, 0.8f, panOf(p.cx()));
        }
    }
    // flew out through the open exit gate?
    if (openTop && p.y + 10 < 0) {
        if (p.pass >= 0 && PASS[p.pass].up) {
            upCarrier = i;
            p.score += GATE_BONUS;
            addText(WW / 2.f, 20, "+$" + std::to_string(GATE_BONUS), PCOL[i].light);
            LV.upDone = true;
            if (numJoined() > 1) addText(WW / 2.f, 34, "EVERYONE OUT!", rgb(120, 255, 120));
        }
        if (p.pass >= 0) PASS[p.pass].state = PS_NONE;
        p.pass = -1;
        p.exited = true; p.alive = false; p.deadT = 0;
        audio.thrust(i, 0, 0);
        audio.play(SFX_TELEPORT, 0.5f, panOf(p.cx()));
    }
}

static int heyCount = 0;   // number of "Hey, taxi!" calls (checked by the self-test)
static void checkLevelExit() {
    if (!LV.upDone || gstate != GS_PLAY) return;
    for (auto& p : P) if (p.joined && !p.exited && (p.alive || p.lives > 0)) return;   // someone still has to get out
    gstate = GS_DONE; stateT = 0;
    audio.silenceThrust();
    audio.play(SFX_LEVELDONE);
}

static void updatePassengers(float dt) {
    for (int k = 0; k < (int)PASS.size(); k++) {
        Passenger& q = PASS[k];
        if (q.state == PS_NONE) continue;
        q.anim += dt;
        if (q.state == PS_WAIT) {
            const PadE& pad = LV.pads[q.pad];
            float px = pad.x + q.relx + 3;
            // find nearest empty taxi landed on this pad
            int best = -1; float bd = 1e9f;
            for (int i = 0; i < MAXP; i++) {
                Player& p = P[i];
                if (!p.joined || !p.alive || !p.landed || p.landPad != q.pad || p.pass >= 0) continue;
                float d = std::fabs(p.cx() - px);
                if (d < bd) { bd = d; best = i; }
            }
            if (best >= 0) {
                Player& p = P[best];
                float dx = p.cx() - px;
                q.dir = dx > 0 ? 1.f : -1.f;
                float step = 14.f * dt;
                if (std::fabs(dx) <= step + 1.f) {
                    // board
                    q.state = PS_RIDE; q.taxi = best; p.pass = k; p.tip = TIP_START;
                    audio.play(SFX_PICKUP, 0.7f, panOf(p.cx()));
                    if (q.up) { audio.say(q.voice, VC_UP, panOf(p.cx())); addText(p.cx(), p.y - 6, "UP!", PCOL[best].light); }
                    else {
                        audio.say(q.voice, VC_PAD1 + (q.dest - '1'), panOf(p.cx()));
                        addText(p.cx(), p.y - 6, std::string("PAD ") + q.dest, PCOL[best].light);
                    }
                } else q.relx += q.dir * step;
                q.relx = std::clamp(q.relx, 0.f, (float)(pad.w - 6));
            } else if (std::fabs(q.homeX - q.relx) > 0.5f) {
                // the taxi left before they got in: walk back to where they were waiting
                q.dir = q.homeX > q.relx ? 1.f : -1.f;
                float step = 14.f * dt;
                q.relx = std::fabs(q.homeX - q.relx) <= step ? q.homeX : q.relx + q.dir * step;
            } else {
                q.relx = q.homeX;
                // "Hey, taxi!" exactly once, right after appearing; afterwards just wave now and then
                if (q.heyT > 0) {
                    q.heyT -= dt;
                    if (q.heyT <= 0) { audio.say(q.voice, VC_HEY, panOf(px)); q.anim = 0; heyCount++; }
                }
                q.waveT -= dt;
                if (q.waveT <= 0) { q.anim = 0; q.waveT = 6.f + rng.f() * 4.f; }
            }
        } else if (q.state == PS_RIDE) {
            Player& p = P[q.taxi];
            if (p.pass == k) p.tip = std::max(0.f, p.tip - TIP_DECAY * dt);
        } else if (q.state == PS_EXIT) {
            // walk (slowly) to the edge of the pad, never beyond it
            float step = 12.f * dt;
            if (std::fabs(q.exitX - q.relx) <= step) {
                q.relx = q.exitX; q.state = PS_BEAM; q.beamT = q.beamDur = 0.9f; q.anim = 0;
                const PadE& pd = LV.pads[q.pad];
                audio.play(SFX_TELEPORT, 0.35f, panOf(pd.x + q.relx));
            } else q.relx += q.dir * step;
        } else if (q.state == PS_BEAM) {
            q.beamT -= dt;
            if (q.beamT <= 0) q.state = PS_NONE;
        }
    }
    // spawn logic
    // Up to three passengers wait at once (each on its own pad). Additional passengers appear
    // about 15 s apart; if nobody is waiting at all, the next one comes within ~3 s.
    int active = 0, waiting = 0;
    bool upActive = false;
    for (auto& q : PASS) {
        if ((q.state == PS_WAIT || q.state == PS_RIDE) && !q.up) active++;
        if (q.state == PS_WAIT && !q.up) waiting++;
        if ((q.state == PS_WAIT || q.state == PS_RIDE) && q.up) upActive = true;
    }
    int np = (int)numberedPads().size();
    const int MAX_WAITING = std::min(3, std::max(1, np - 1));
    if (!LV.quotaDone) {
        if (waiting < MAX_WAITING && LV.delivered + active < LV.def->quota) {
            if (waiting == 0) spawnT = std::min(spawnT, 3.0f);
            spawnT -= dt;
            if (spawnT <= 0) { spawnPassenger(false); spawnT = 13.f + rng.f() * 4.f; }
        }
        if (LV.delivered >= LV.def->quota) {
            LV.quotaDone = true; upSpawnT = 1.5f;
            audio.play(SFX_GATE);
            addText(WW / 2.f, 30, "EXIT OPEN!", rgb(120, 255, 120));
        }
    } else if (!upActive && !LV.upDone) {
        upSpawnT -= dt;
        if (upSpawnT <= 0) { spawnPassenger(true); upSpawnT = 3.f; }
    }
}

static void updateWorld(float dt) {
    Level& L = LV;
    L.time += dt;
    if (L.switchCd > 0) L.switchCd -= dt;
    // lasers: A on for the first half of a 4s cycle, B for the second
    float lt = std::fmod(L.time, 4.f);
    bool na = lt < 2.f, nb = !na;
    if (na != L.laserA || nb != L.laserB) {
        bool any = false;
        for (int y = 0; y < MH && !any; y++) for (int x = 0; x < MW; x++) if (L.tile[y][x] == 'L' || L.tile[y][x] == 'l') { any = true; break; }
        if (any) audio.play(SFX_LASER, 0.4f, 0);
    }
    L.laserA = na; L.laserB = nb;
    if (L.quotaDone && L.gateAnim < 1.f) L.gateAnim = std::min(1.f, L.gateAnim + dt * 0.7f);

    // platforms
    for (auto& p : L.plats) {
        bool move = p.d.sw == 0 || (p.d.sw == 1 && L.switchOn) || (p.d.sw == 2 && !L.switchOn);
        if (move) p.t += dt;
        float u = 0.5f - 0.5f * std::cos(2 * 3.14159265f * (p.t / p.d.period + p.d.phase));
        int nx = (int)std::lround((p.d.x0 + (p.d.x1 - p.d.x0) * u) * 8);
        int ny = (int)std::lround((p.d.y0 + (p.d.y1 - p.d.y0) * u) * 8);
        p.dx = nx - p.x; p.dy = ny - p.y; p.x = nx; p.y = ny;
        // exact velocity (derivative of the eased motion) for landing / take-off physics
        float du = move ? 3.14159265f / p.d.period * std::sin(2 * 3.14159265f * (p.t / p.d.period + p.d.phase)) : 0.f;
        p.vx = (p.d.x1 - p.d.x0) * 8 * du; p.vy = (p.d.y1 - p.d.y0) * 8 * du;
        if (p.pad >= 0) { L.pads[p.pad].x = p.x; L.pads[p.pad].y = p.y; }
    }
    // mines
    for (auto& m : L.mines) {
        float ph = L.time / m.d.period + m.d.phase;
        if (m.d.r > 0) {
            m.x = (m.d.x0 + std::cos(ph * 6.2832f) * m.d.r) * 8;
            m.y = (m.d.y0 + std::sin(ph * 6.2832f) * m.d.r) * 8;
        } else {
            float u = 0.5f - 0.5f * std::cos(ph * 6.2832f);
            m.x = (m.d.x0 + (m.d.x1 - m.d.x0) * u) * 8;
            m.y = (m.d.y0 + (m.d.y1 - m.d.y0) * u) * 8;
        }
    }
    rebuildMasks();
    // carry landed taxis on platforms, then crush / zap checks for everyone
    for (int i = 0; i < MAXP; i++) {
        Player& p = P[i];
        if (!p.joined || !p.alive) continue;
        if (p.landed && p.landPlat >= 0) { p.x += L.plats[p.landPlat].dx; p.y += L.plats[p.landPlat].dy; }
        int ix = (int)std::lround(p.x), iy = (int)std::lround(p.y);
        bool openTop = canExit(p);
        if (hitMask(L.haz, taxiMask[p.face][p.gearRows()], 16, 10, ix, iy, openTop, false)) { killPlayer(i, "ZAPPED!"); continue; }
        if (hitMask(L.solid, bodyOnly[p.face], 16, 10, ix, iy, openTop)) { killPlayer(i, "CRUSHED!"); continue; }
        if (p.landed && hitMask(L.solid, gearOnly[p.face][2], 16, 10, ix, iy, openTop)) {
            // platform pushed up into the gear: ride it up a pixel or two
            int k = 1;
            for (; k <= 3; k++) if (!hitMask(L.solid, gearOnly[p.face][2], 16, 10, ix, iy - k, openTop)) break;
            if (k > 3 || hitMask(L.solid, bodyOnly[p.face], 16, 10, ix, iy - k, openTop)) { killPlayer(i, "CRUSHED!"); continue; }
            p.y -= k;
        }
    }
}

static void taxiCollisions() {
    for (int a = 0; a < MAXP; a++)
        for (int b = a + 1; b < MAXP; b++) {
            Player& A = P[a]; Player& B = P[b];
            if (!A.joined || !B.joined || !A.alive || !B.alive || A.invuln > 0 || B.invuln > 0) continue;
            int ax = (int)std::lround(A.x), ay = (int)std::lround(A.y), bx = (int)std::lround(B.x), by = (int)std::lround(B.y);
            if (std::abs(ax - bx) >= 16 || std::abs(ay - by) >= 10) continue;
            const uint8_t* ma = taxiMask[A.face][A.gearRows()]; const uint8_t* mb = taxiMask[B.face][B.gearRows()];
            bool hit = false;
            for (int y = 0; y < 10 && !hit; y++)
                for (int x = 0; x < 16; x++) {
                    if (!ma[y * 16 + x]) continue;
                    int u = ax + x - bx, v = ay + y - by;
                    if (u >= 0 && u < 16 && v >= 0 && v < 10 && mb[v * 16 + u]) { hit = true; break; }
                }
            if (hit) { killPlayer(a, "COLLISION!"); killPlayer(b, nullptr); }
        }
}

static void updateParticles(float dt) {
    for (auto& p : parts) {
        p.vy += p.g * dt; p.x += p.vx * dt; p.y += p.vy * dt; p.vx *= (1 - 1.5f * dt); p.life -= dt;
    }
    parts.erase(std::remove_if(parts.begin(), parts.end(), [](const Particle& p) { return p.life <= 0; }), parts.end());
    for (auto& t : ftexts) { t.y -= 14 * dt; t.life -= dt; }
    ftexts.erase(std::remove_if(ftexts.begin(), ftexts.end(), [](const FloatText& t) { return t.life <= 0; }), ftexts.end());
    if (shakeT > 0) shakeT -= dt;
}

// ------------------------------------------------------------------ level flow
static void startLevel(int idx) {
    curLevel = idx;
    buildLevel(idx);
    for (auto& q : PASS) q = Passenger();
    parts.clear(); ftexts.clear();
    spawnT = 1.0f; upSpawnT = 0; upCarrier = -1;
    rebuildMasks();
    int s = 0;
    for (int i = 0; i < MAXP; i++) {
        Player& p = P[i];
        p.levelFares = 0; p.exited = false;
        if (!p.joined) continue;
        if (p.lives <= 0) { p.alive = false; continue; }
        if (s < (int)LV.slots.size()) placeOnSlot(p, LV.slots[s++]);
        else { p.alive = false; p.deadT = 0.5f; }
        p.invuln = 0;
    }
    gstate = GS_INTRO; stateT = 0;
    audio.silenceThrust();
}

static void newGame() {
    for (auto& p : P) {
        p.score = 0; p.lives = 5; p.nextLife = EXTRA_LIFE_EVERY; p.fuel = FUEL_MAX; p.fares = 0; p.alive = false; p.pass = -1;
    }
    audio.play(SFX_START);
    startLevel(startLevel_);
}

// ------------------------------------------------------------------ rendering
static void blitTaxi(int i, int sx, int sy) {
    const Player& p = P[i];
    bool flash = ((int)(tglobal * 6)) & 1;
    int gearRows = (int)std::lround(p.gearExt);
    for (int y = 0; y < 10; y++) {
        int yy = y;
        if (y == 8 && gearRows < 2) continue;          // upper leg hidden while retracting
        if (y == 9) { if (gearRows == 0) continue; yy = gearRows == 1 ? 8 : 9; }
        for (int x = 0; x < 16; x++) {
            int srcx = p.face ? 15 - x : x;
            char ch = TAXI_SPR[y][srcx];
            if (ch == '.') continue;
            uint32_t c = taxiPix(ch, i, flash);
            if (p.fuel <= 0 && ch == 'L') c = rgb(60, 60, 60);
            pset(sx + x, sy + yy, c);
        }
    }
    // flames
    auto flame = [&](int fx, int fy, int dx, int dy, int len) {
        for (int k = 0; k < len; k++) {
            float t = (float)k / len;
            uint32_t c = t < 0.3f ? rgb(255, 255, 220) : (t < 0.6f ? rgb(255, 210, 60) : rgb(255, 90, 20));
            pset(fx + dx * k, fy + dy * k, c);
            if (k < len - 1) pblend(fx + dx * k + (dy != 0), fy + dy * k + (dx != 0), c, 0.6f);
        }
    };
    if (p.thU) {   // main engine sits under the chassis between the wheel pods
        int len = 3 + rng.i(4);
        flame(sx + 7, sy + 7, 0, 1, len);
        flame(sx + 8, sy + 7, 0, 1, std::max(2, len - 1 - rng.i(2)));
    }
    if (p.thD) { flame(sx + 7, sy - 1, 0, -1, 2 + rng.i(3)); }
    if (p.thL || p.thR) {
        if (p.face == 0) flame(sx - 1, sy + 5, -1, 0, 2 + rng.i(4));
        else flame(sx + 16, sy + 5, 1, 0, 2 + rng.i(4));
    }
}

static int passFrame(const Passenger& q) {
    if (q.state == PS_EXIT) return 1 + ((int)(q.anim * 6) & 1);
    if (q.state == PS_BEAM) return 0;
    bool walking = false;
    for (int i = 0; i < MAXP; i++)
        if (P[i].joined && P[i].alive && P[i].landed && P[i].landPad == q.pad && P[i].pass < 0) walking = true;
    if (walking || std::fabs(q.homeX - q.relx) > 0.5f) return 1 + ((int)(q.anim * 6) & 1);
    if (q.anim < 2.0f) return 3 + ((int)(q.anim * 6) & 1);
    return 0;
}
// sprite character at (x,y) for this passenger and pose; '.' = transparent
static char passGlyph(const Passenger& q, int frame, int x, int y) {
    bool side = frame == 1 || frame == 2;
    int sx = (side && q.dir < 0) ? 5 - x : x;
    if (y < 4) { char h = PHEAD[q.style][side ? 1 : 0][y][sx]; if (h != '.') return h; }
    return PBODY[q.gender][frame][y][sx];
}
static uint32_t passColor(const Passenger& q, char c) {
    switch (c) {
        case 'H': return q.hair;  case 'S': case 'L': return q.skin;  case 'E': return rgb(24, 16, 16);
        case 'C': return q.top;   case 'P': case 'D': return q.bottom; case 'B': return rgb(30, 24, 20);
        default: return 0;
    }
}
static void drawPassenger(const Passenger& q, int sx, int sy) {
    int frame = passFrame(q);
    float vis = q.state == PS_BEAM ? std::clamp((q.beamT - 0.15f * q.beamDur) / (0.65f * q.beamDur), 0.f, 1.f) : 1.f;
    if (q.state == PS_BEAM) {
        // teleport beam around the standing passenger
        float pulse = 0.35f + 0.25f * std::sin(tglobal * 40);
        float fade = std::min(1.f, q.beamT / 0.3f);
        for (int y = -6; y < 10; y++)
            for (int x = -1; x < 7; x++) {
                float edge = (x == -1 || x == 6) ? 0.5f : 1.f;
                pblend(sx + x, sy + y, rgb(160, 240, 255), pulse * edge * fade * (y < 0 ? (y + 6) / 6.f : 1.f));
            }
        for (int k = 0; k < 6; k++) {
            int px = sx + (int)(h01(k, (int)(tglobal * 30), 11) * 7) - 1;
            int py = sy + 9 - (int)(std::fmod(tglobal * 40 + k * 7, 16.f));
            pset(px, py, rgb(255, 255, 255));
        }
    }
    for (int y = 0; y < 10; y++)
        for (int x = 0; x < 6; x++) {
            char c = passGlyph(q, frame, x, y);
            if (c == '.') continue;
            if (q.state == PS_BEAM) {
                if (bayer(x, y + (int)(tglobal * 20)) > vis) continue;
                pset(sx + x, sy + y, mix(passColor(q, c), rgb(200, 250, 255), 1 - vis));
            } else pset(sx + x, sy + y, passColor(q, c));
        }
    if (q.up && q.state == PS_WAIT && ((int)(tglobal * 3) & 1)) drawTinyOutlinedC(sx + 3, sy - 7, "UP", rgb(120, 255, 120));
}

// Flying taxis hit standing passengers (pixel perfect against the drawn sprites). The taxi
// bounces off, the passenger yells "Ouch!" and beams out immediately, and the driver is fined.
// Parked taxis and taxis that took off less than 0.5 s ago are ignored, so taking off
// next to a passenger is safe but landing on top of one is not. Delivered passengers
// (walking to the edge or beaming out) can never be hit.
static const int PASSENGER_HIT_PENALTY = 75;
static void passengerCollisions() {
    for (auto& q : PASS) {
        if (q.state != PS_WAIT) continue;
        const PadE& pd = LV.pads[q.pad];
        int qx = pd.x + (int)q.relx, qy = pd.y - 10;
        int frame = passFrame(q);
        for (int i = 0; i < MAXP; i++) {
            Player& p = P[i];
            if (!p.joined || !p.alive) continue;
            if (p.landed ? p.landedT > 0.1f : p.airT < 0.5f) continue;   // touching down on someone still counts
            int tx = (int)std::lround(p.x), ty = (int)std::lround(p.y);
            if (qx + 6 <= tx || qx >= tx + 16 || qy + 10 <= ty || qy >= ty + 10) continue;
            const uint8_t* m = taxiMask[p.face][p.gearRows()];
            bool hit = false;
            for (int y = 0; y < 10 && !hit; y++)
                for (int x = 0; x < 6; x++) {
                    if (passGlyph(q, frame, x, y) == '.') continue;
                    int u = qx + x - tx, v = qy + y - ty;
                    if (u >= 0 && u < 16 && v >= 0 && v < 10 && m[v * 16 + u]) { hit = true; break; }
                }
            if (!hit) continue;
            audio.say(q.voice, VC_OUCH, panOf(qx + 3));     // "Ouch!" plays right away
            audio.play(SFX_BUMP, 0.8f, panOf(qx + 3));
            if (getenv("BOTDBG")) printf("  PASSENGER HIT by p%d at %d,%d t=%.1f\n", i, qx, qy, LV.time);
            int fine = (int)std::min<long>(PASSENGER_HIT_PENALTY, p.score);
            p.score -= fine;
            addText(qx + 3.f, qy - 4.f, "OUCH! -$" + std::to_string(PASSENGER_HIT_PENALTY), rgb(255, 90, 90));
            // bounce the taxi off the passenger along the axis of contact
            float dx = p.cx() - (qx + 3.f), dy = p.cy() - (qy + 5.f);
            if (p.landed) { p.landed = false; p.landPad = p.landPlat = -1; p.y -= 1; dy = -1; dx = 0; }   // touched down on them
            if (std::fabs(dx) / 11.f > std::fabs(dy) / 10.f)
                p.vx = (dx > 0 ? 1.f : -1.f) * std::max(std::fabs(p.vx) * 0.6f, 30.f);
            else
                p.vy = (dy > 0 ? 1.f : -1.f) * std::max(std::fabs(p.vy) * 0.6f, 28.f);
            // the passenger beams out on the spot (can't be hit again while beaming)
            if (q.up) upSpawnT = 2.0f;
            q.state = PS_BEAM; q.beamT = q.beamDur = 0.5f;
            audio.play(SFX_TELEPORT, 0.35f, panOf(qx + 3));
            spawnT = std::max(spawnT, 2.0f);
            break;
        }
    }
}

static void drawWorld() {
    Level& L = LV;
    // static layer
    for (int y = 0; y < WH; y++) memcpy(&fb[(y + MAPY) * SW], &L.img[y * WW], WW * 4);
    const Theme& th = *L.th;
    // wind streaks
    for (int ty = 0; ty < MH; ty++)
        for (int tx = 0; tx < MW; tx++) {
            float wx = L.windX[ty][tx], wy = L.windY[ty][tx];
            if (wx == 0 && wy == 0) continue;
            for (int k = 0; k < 2; k++) {
                float ph = h01(tx, ty, k) * 8 + L.time * (std::fabs(wx) + std::fabs(wy)) * 0.5f;
                int off = ((int)ph) & 7;
                int lane = (int)(h01(tx, ty, k + 9) * 8);
                for (int s = 0; s < 3; s++) {
                    int px, py;
                    if (wx != 0) { px = tx * 8 + ((wx > 0 ? off - s : 7 - off + s) & 7); py = ty * 8 + lane; }
                    else { px = tx * 8 + lane; py = ty * 8 + ((wy > 0 ? off - s : 7 - off + s) & 7); }
                    pblend(px, py + MAPY, th.accent, 0.35f - s * 0.1f);
                }
            }
        }
    // teleporters
    if (L.hasTp) {
        for (int w = 0; w < 2; w++) {
            SDL_Rect r = w ? L.tpB : L.tpA;
            float cx = r.x + r.w / 2.f, cy = r.y + r.h / 2.f + MAPY;
            uint32_t c1 = w ? rgb(80, 220, 255) : rgb(255, 120, 230);
            for (int ring = 0; ring < 4; ring++) {
                float rad = 3 + ring * 2.8f + std::fmod(L.time * 6, 2.8f);
                for (int k = 0; k < 18; k++) {
                    float a = k / 18.f * 6.2832f + L.time * (ring % 2 ? 2.f : -2.f);
                    pblend((int)(cx + std::cos(a) * rad), (int)(cy + std::sin(a) * rad * 0.9f), c1, 0.75f - ring * 0.15f);
                }
            }
            pset((int)cx, (int)cy, rgb(255, 255, 255));
        }
    }
    // doors, gate, lasers
    for (int ty = 0; ty < MH; ty++)
        for (int tx = 0; tx < MW; tx++) {
            char c = L.tile[ty][tx];
            int bx = tx * 8, by = ty * 8 + MAPY;
            if (c == 'D' || c == 'E') {
                uint32_t col = c == 'D' ? rgb(255, 60, 60) : rgb(60, 200, 255);
                if (doorSolid(c, L)) {
                    for (int py = 0; py < 8; py++)
                        for (int px = 0; px < 8; px++) {
                            float n = h01(bx + px, by + py + (int)(L.time * 20), 3);
                            uint32_t cc = mix(shade(col, 0.35f), col, n);
                            if (((px + py + (int)(L.time * 12)) & 3) == 0) cc = mix(cc, rgb(255, 255, 255), 0.5f);
                            pset(bx + px, by + py, cc);
                        }
                } else {
                    for (int px = 0; px < 8; px += 2) { pblend(bx + px, by, col, 0.5f); pblend(bx + px, by + 7, col, 0.5f); }
                }
            } else if (c == 'G') {
                float a = L.gateAnim;
                // gate: two hazard-striped blast doors sliding apart
                int gx0 = 0, gx1 = 0;
                for (int x = 0; x < MW; x++) if (L.tile[0][x] == 'G') { if (!gx1) gx0 = x * 8; gx1 = x * 8 + 8; }
                int half = (gx1 - gx0) / 2;
                int open = (int)(half * a);
                for (int py = 0; py < 8; py++)
                    for (int px = 0; px < 8; px++) {
                        int x = bx + px;
                        bool leftDoor = x < gx0 + half - open, rightDoor = x >= gx0 + half + open;
                        if (!leftDoor && !rightDoor) continue;
                        uint32_t cc = (((x + py) / 3) & 1) ? rgb(250, 200, 30) : rgb(30, 30, 30);
                        if (py == 0) cc = rgb(200, 200, 210);
                        if (py == 7) cc = rgb(60, 60, 70);
                        pset(x, by + py, cc);
                    }
                if (a >= 1.f && ((int)(L.time * 4) & 1)) { pset(gx0 - 1, MAPY + 3, rgb(80, 255, 80)); pset(gx1, MAPY + 3, rgb(80, 255, 80)); }
            } else if (c == 'L' || c == 'l') {
                bool on = c == 'L' ? L.laserA : L.laserB;
                float lt = std::fmod(L.time, 4.f);
                float until = c == 'L' ? (lt < 2 ? 0 : 4 - lt) : (lt >= 2 ? 0 : 2 - lt);
                bool warn = !on && until < 0.5f;
                bool vert = L.T(tx, ty - 1) == c || L.T(tx, ty + 1) == c;
                for (int k = 0; k < 8; k++) {
                    int px = vert ? bx : bx + k, py = vert ? by + k : by;
                    auto put = [&](int a, uint32_t col, float alpha) {
                        if (vert) pblend(px + a, py, col, alpha); else pblend(px, py + a, col, alpha);
                    };
                    if (on) {
                        float fl = 0.8f + 0.2f * std::sin(L.time * 60 + k + ty * 8);
                        put(2, rgb(255, 40, 40), 0.6f * fl); put(5, rgb(255, 40, 40), 0.6f * fl);
                        put(3, rgb(255, 230, 230), 1.f); put(4, rgb(255, 160, 160), 1.f);
                        put(1, rgb(255, 0, 0), 0.2f); put(6, rgb(255, 0, 0), 0.2f);
                    } else if (warn && ((int)(L.time * 20) & 1)) put(3, rgb(255, 80, 80), 0.5f);
                }
                // emitters where the beam meets a wall
                for (int e = 0; e < 2; e++) {
                    int nx = vert ? tx : tx + (e ? 1 : -1), ny = vert ? ty + (e ? 1 : -1) : ty;
                    if (!isWall(L.T(nx, ny)) && L.T(nx, ny) != '=') continue;
                    for (int a = 1; a < 7; a++)
                        for (int b = 0; b < 2; b++) {
                            int px = vert ? bx + a : (e ? bx + 7 - b : bx + b);
                            int py = vert ? (e ? by + 7 - b : by + b) : by + a;
                            pset(px, py, b == 0 ? rgb(90, 90, 104) : (on ? rgb(255, 120, 120) : rgb(140, 140, 156)));
                        }
                }
            }
        }
    // floor switches: a coloured button on a flat plate. The colour shows which doors are
    // closed (red = red doors, cyan = cyan doors); it is pushed flat while a taxi sits on it.
    for (auto& s : L.switches) {
        uint32_t col = L.switchOn ? rgb(60, 200, 255) : rgb(255, 60, 60);
        bool pressed = false;
        for (auto& p : P) if (p.joined && p.alive && switchPressedBy(p, s)) pressed = true;
        int sy = s.y + MAPY;
        for (int px = 0; px < 8; px++) { pset(s.x + px, sy + 6, rgb(170, 170, 184)); pset(s.x + px, sy + 7, rgb(70, 70, 84)); }
        int top = pressed ? 5 : 3;
        for (int py = top; py < 6; py++)
            for (int px = 2; px < 6; px++) pset(s.x + px, sy + py, py == top ? mix(col, rgb(255, 255, 255), 0.5f) : col);
        if (!pressed && ((int)(L.time * 2) & 1)) { pblend(s.x + 3, sy + 1, col, 0.6f); pblend(s.x + 4, sy + 1, col, 0.6f); }
    }
    // moving platforms
    for (auto& p : L.plats) {
        int sx = p.x, sy = p.y + MAPY;
        if (p.pad >= 0) {
            const PadE& pd = L.pads[p.pad];
            paintPad(sx, sy, p.w, pd.id, pd.id == 'F', [&](int x, int y, uint32_t c) { pset(x, y, c); });
        } else {
            const uint32_t* m = th.metal;
            for (int py = 0; py < 8; py++)
                for (int px = 0; px < p.w; px++) {
                    uint32_t c;
                    if (py == 0) c = m[4]; else if (py == 7) c = m[0]; else if (py == 1 || py == 6) c = m[2];
                    else c = (((px + py) / 3) & 1) ? rgb(250, 200, 30) : rgb(30, 30, 30);
                    if (px == 0 || px == p.w - 1) c = m[1];
                    pset(sx + px, sy + py, c);
                }
        }
        // hover jets underneath
        for (int e = 0; e < 2; e++) {
            int jx = sx + (e ? p.w - 4 : 2);
            int len = 1 + rng.i(3);
            for (int k = 0; k < len; k++) pset(jx + rng.i(2), sy + 8 + k, k == 0 ? rgb(200, 240, 255) : rgb(80, 160, 255));
        }
    }
    // pad lamps: waiting passenger -> white blink, destination -> carrier colour
    for (int k = 0; k < (int)L.pads.size(); k++) {
        const PadE& pd = L.pads[k];
        uint32_t lamp = 0;
        for (auto& q : PASS) if (q.state == PS_WAIT && q.pad == k && ((int)(tglobal * 4) & 1)) lamp = rgb(255, 255, 255);
        for (int i = 0; i < MAXP; i++)
            if (P[i].joined && P[i].alive && P[i].pass >= 0 && !PASS[P[i].pass].up && padByChar(PASS[P[i].pass].dest) == k && ((int)(tglobal * 5 + i) & 1))
                lamp = PCOL[i].main;
        if (pd.id == 'F') {
            for (int i = 0; i < MAXP; i++) if (P[i].alive && P[i].landed && P[i].landPad == k && P[i].fuel < FUEL_MAX && ((int)(tglobal * 8) & 1)) lamp = rgb(255, 80, 40);
        }
        if (lamp) {
            int y = pd.y + MAPY - 1;
            for (int e = 0; e < 2; e++) {
                int x = e ? pd.x + pd.w - 3 : pd.x + 1;
                pset(x, y, lamp); pset(x + 1, y, lamp); pblend(x, y - 1, lamp, 0.4f); pblend(x + 1, y - 1, lamp, 0.4f);
            }
        }
    }
    // mines
    for (auto& m : L.mines) {
        int mx = (int)std::lround(m.x) - 4, my = (int)std::lround(m.y) - 4 + MAPY;
        bool blink = ((int)(L.time * 6)) & 1;
        for (int j = 0; j < 9; j++)
            for (int i = 0; i < 9; i++) {
                char c = MINE_SPR[j][i];
                uint32_t col;
                switch (c) {
                    case 'X': col = rgb(220, 220, 230); break; case 'o': col = rgb(70, 60, 80); break;
                    case 'O': col = rgb(120, 110, 130); break; case 'R': col = blink ? rgb(255, 40, 40) : rgb(120, 0, 0); break;
                    default: continue;
                }
                pset(mx + i, my + j, col);
            }
        if (blink) for (int k = 0; k < 4; k++) pblend(mx + 4 + (k % 2 ? 1 : -1), my + 4 + (k / 2 ? 1 : -1), rgb(255, 60, 60), 0.4f);
    }
    // passengers
    for (auto& q : PASS) {
        if (q.state == PS_WAIT) {
            const PadE& pd = L.pads[q.pad];
            drawPassenger(q, pd.x + (int)q.relx, pd.y - 10 + MAPY);
        } else if (q.state == PS_EXIT || q.state == PS_BEAM) {
            const PadE& pd = L.pads[q.pad];
            drawPassenger(q, pd.x + (int)q.relx, pd.y - 10 + MAPY);
        }
    }
    // taxis
    for (int i = 0; i < MAXP; i++) {
        Player& p = P[i];
        if (!p.joined || !p.alive) continue;
        if (p.invuln > 0 && ((int)(tglobal * 15) & 1)) continue;
        int jx = 0, jy = 0;
        if (p.roughT > 0) { jx = rng.i(3) - 1; jy = rng.i(2) - (p.roughT > 0.25f ? 1 : 0); }   // visual shake only
        blitTaxi(i, (int)std::lround(p.x) + jx, (int)std::lround(p.y) + MAPY + jy);
        // little player marker
        if (p.invuln > 0 || gstate == GS_INTRO) drawTinyOutlinedC((int)p.cx(), (int)p.y + MAPY - 7, "P" + std::to_string(i + 1), PCOL[i].light);
    }
    // particles
    for (auto& p : parts) {
        float a = std::clamp(p.life / p.max * 1.5f, 0.f, 1.f);
        int x = (int)p.x, y = (int)p.y + MAPY;
        if (y < MAPY) continue;
        if (p.size == 1) pblend(x, y, p.c, a);
        else { pblend(x, y, p.c, a); pblend(x + 1, y, p.c, a); pblend(x, y + 1, p.c, a); pblend(x + 1, y + 1, p.c, a); }
    }
    // floating texts ("$47", "PAD 3", "CRASH!", ...) in the small 3x5 pixel font with a dark outline
    for (auto& t : ftexts) {
        if (t.life < 0.4f && ((int)(t.life * 20) & 1)) continue;
        int y = std::clamp((int)t.y + MAPY, MAPY + 1, MAPY + WH - 6);
        drawTinyOutlinedC((int)t.x, y, t.s, t.c);
    }
}

static void drawHud() {
    // status bar (same look as before, now below the playfield)
    const int Y = HUDY;
    for (int y = 0; y < HUDH; y++) {
        uint32_t c = mix(rgb(30, 30, 46), rgb(10, 10, 18), y / 15.f);
        for (int x = 0; x < SW; x++) fb[(Y + y) * SW + x] = c;
    }
    rectf(0, Y + HUDH - 1, SW, 1, rgb(90, 90, 120));
    for (int i = 0; i < MAXP; i++) {
        int x0 = i * 64;
        const Player& p = P[i];
        rectf(x0, Y, 1, HUDH - 1, i ? rgb(60, 60, 80) : rgb(30, 30, 46));
        if (!p.joined) {
            drawText(x0 + 4, Y + 4, "PRESS " JOIN_LABEL, rgb(90, 90, 110), 0);
            continue;
        }
        drawText(x0 + 4, Y, hudMoneyText(p), p.alive && p.pass >= 0 ? PCOL[i].light : PCOL[i].main);
        if (p.exited) { drawText(x0 + 4, Y + 8, "EXITED", rgb(120, 255, 120), 0); continue; }
        if (p.lives <= 0 && !p.alive) { drawText(x0 + 4, Y + 8, "OUT", rgb(150, 60, 60), 0); continue; }
        // lives: tiny taxis
        int lv = std::min(p.lives, 3);
        for (int k = 0; k < lv; k++) {
            int lx = x0 + 3 + k * 7;
            rectf(lx + 1, Y + 10, 4, 1, PCOL[i].light); rectf(lx, Y + 11, 6, 2, PCOL[i].main); pset(lx + 1, Y + 13, rgb(80, 80, 90)); pset(lx + 4, Y + 13, rgb(80, 80, 90));
        }
        if (p.lives > 3) tinyText(x0 + 24, Y + 9, "+", [&](int x, int y) { pset(x, y, PCOL[i].light); });
        // destination indicator
        if (p.alive && p.pass >= 0) {
            char d[3] = {PASS[p.pass].up ? '+' : PASS[p.pass].dest, 0, 0};
            if (PASS[p.pass].up) { tinyText(x0 + 33, Y + 9, "+", [&](int x, int y) { pset(x, y, rgb(120, 255, 120)); }); }
            else tinyText(x0 + 33, Y + 9, d, [&](int x, int y) { pset(x, y, ((int)(tglobal * 4) & 1) ? rgb(255, 255, 255) : PCOL[i].main); });
        }
        // fuel bar
        int fw = 22, fx = x0 + 39;
        rectf(fx - 1, Y + 9, fw + 2, 5, rgb(10, 10, 14));
        int fill = (int)std::ceil(fw * p.fuel / FUEL_MAX);
        uint32_t fc = p.fuel > 50 ? rgb(80, 230, 80) : (p.fuel > 20 ? rgb(240, 210, 40) : rgb(240, 50, 40));
        if (p.fuel <= 20 && ((int)(tglobal * 6) & 1)) fc = rgb(120, 20, 20);
        rectf(fx, Y + 10, fill, 3, fc);
        rectf(fx, Y + 10, fill, 1, mix(fc, rgb(255, 255, 255), 0.4f));
    }
    // level name / fares line, below the status bar
    int by = HUDY + HUDH;
    rectf(0, by, SW, SH - by, rgb(10, 10, 18));
    char buf[64];
    snprintf(buf, sizeof buf, "%02d %s", curLevel + 1, LV.def ? LV.def->name.c_str() : "");
    drawText(2, by, buf, rgb(200, 200, 230), 0);
    std::string right;
    uint32_t rc = rgb(255, 210, 60);
    if (LV.upDone) { right = ((int)(tglobal * 3) & 1) ? "EVERYONE OUT!" : ""; rc = rgb(120, 255, 120); }
    else if (LV.quotaDone) { right = ((int)(tglobal * 3) & 1) ? "UP PLEASE!" : ""; rc = rgb(120, 255, 120); }
    else { snprintf(buf, sizeof buf, "FARES %d/%d", LV.delivered, LV.def ? LV.def->quota : 0); right = buf; }
    drawText(SW - textW(right) - 2, by, right, rc, 0);
}

static void banner(int y, int h) {
    rectb(20, y, SW - 40, h, rgb(0, 0, 10), 0.75f);
    rectline(20, y, SW - 40, h, rgb(120, 120, 170));
    rectline(21, y + 1, SW - 42, h - 2, rgb(40, 40, 70));
}

static void drawStandings(int y0) {
    std::vector<int> ord;
    for (int i = 0; i < MAXP; i++) if (P[i].joined) ord.push_back(i);
    std::sort(ord.begin(), ord.end(), [](int a, int b) { return P[a].score > P[b].score; });
    int y = y0;
    for (size_t r = 0; r < ord.size(); r++) {
        int i = ord[r];
        char buf[48]; snprintf(buf, sizeof buf, "%zu. P%d  $%-7ld %2d FARES", r + 1, i + 1, P[i].score, P[i].fares);
        drawText(40, y, buf, PCOL[i].main);
        y += 11;
    }
}

#if defined(__SWITCH__)
#define START_LABEL "+"
#else
#define START_LABEL "START"
#endif
static void drawTitle() {
    for (int y = 0; y < SH; y++) memcpy(&fb[y * SW], &titleBg[y * SW], SW * 4);
    // scrolling foreground stars
    for (int k = 0; k < 40; k++) {
        float sp = 20 + (k % 5) * 18;
        int x = (int)(SW - std::fmod(tglobal * sp + h01(k, 0, 5) * SW, (float)SW));
        int y = (int)(h01(k, 1, 5) * SH);
        pblend(x, y, rgb(255, 255, 255), 0.4f + (k % 5) * 0.12f);
    }
    // flying taxis
    for (int i = 0; i < MAXP; i++) {
        float t = std::fmod(tglobal * (0.12f + i * 0.023f) + i * 0.37f, 1.f);
        int dir = i % 2;
        int x = dir ? (int)(SW + 20 - t * (SW + 40)) : (int)(-20 + t * (SW + 40));
        int y = 108 + i * 8 + (int)(std::sin(tglobal * 2 + i) * 4);
        Player save = P[i];
        P[i].face = dir; P[i].gearExt = 0; P[i].thU = (int)(tglobal * 3 + i) % 3 == 0; P[i].thL = dir == 1; P[i].thR = dir == 0; P[i].thD = false; P[i].fuel = 50;
        blitTaxi(i, x, y);
        P[i] = save;
    }
    drawFancyText(SW / 2, 18, "SPACE TAXI", 24, rgb(255, 250, 170), rgb(255, 170, 20), rgb(210, 40, 10), rgb(40, 0, 30));
    drawFancyText(SW / 2, 48, "MAYHEM", 16, rgb(170, 250, 255), rgb(60, 200, 255), rgb(30, 90, 220), rgb(0, 10, 40));
    drawTextC(SW / 2, 74, "HEY TAXI! PAD 1 PLEASE!", ((int)(tglobal * 2) & 1) ? rgb(255, 255, 255) : rgb(160, 160, 190));
    // player slots
    for (int i = 0; i < MAXP; i++) {
        int x0 = 8 + i * 62, y0 = 160;
        rectb(x0, y0, 58, 34, rgb(0, 0, 0), 0.6f);
        rectline(x0, y0, 58, 34, P[i].joined ? PCOL[i].main : rgb(60, 60, 80));
        drawTextC(x0 + 29, y0 + 3, "P" + std::to_string(i + 1), P[i].joined ? PCOL[i].main : rgb(80, 80, 100), 0);
        if (P[i].joined) {
            Player save = P[i];
            P[i].face = 0; P[i].gearExt = 2; P[i].thU = P[i].thD = P[i].thL = P[i].thR = false; P[i].fuel = 50;
            blitTaxi(i, x0 + 21, y0 + 13);
            P[i] = save;
            const char* src = P[i].src == SRC_PAD ? "PAD" : (P[i].src == SRC_KEYA ? "KEYS" : "WASD");
            tinyText(x0 + 29 - (int)strlen(src) * 2, y0 + 27, src, [&](int x, int y) { pset(x, y, rgb(200, 200, 220)); });
        } else if ((int)(tglobal * 2 + i * 0.3f) & 1) drawTextC(x0 + 29, y0 + 17, "JOIN", rgb(120, 120, 150), 0);
    }
    char buf[40]; snprintf(buf, sizeof buf, "< START LEVEL %02d >", startLevel_ + 1);
    drawTextC(SW / 2, 200, buf, rgb(255, 210, 60));
    drawTextC(SW / 2, 212, numJoined() ? "PRESS " START_LABEL " TO PLAY" : "PRESS " JOIN_LABEL " TO JOIN", ((int)(tglobal * 3) & 1) ? rgb(255, 255, 255) : rgb(120, 255, 120));
    drawTextC(SW / 2, 226, "DPAD THRUST  " GEAR_LABEL " GEAR", rgb(140, 140, 170), 0);
#if defined(__SWITCH__)
    drawTextC(SW / 2, 88, "SINGLE JOY-CONS: HOLD SIDEWAYS", rgb(110, 110, 140), 0);
#elif defined(__vita__)
    drawTextC(SW / 2, 88, "PS TV: UP TO 4 CONTROLLERS", rgb(110, 110, 140), 0);
#else
    drawTextC(SW / 2, 88, "KEYS: ARROWS+SPACE  WASD+SHIFT", rgb(110, 110, 140), 0);
#endif
}

static void render() {
    if (gstate == GS_TITLE) { drawTitle(); return; }
    drawWorld();
    drawHud();
    if (gstate == GS_INTRO) {
        banner(70, 64);
        char buf[32]; snprintf(buf, sizeof buf, "LEVEL %02d", curLevel + 1);
        drawFancyText(SW / 2, 78, buf, 16, rgb(255, 250, 170), rgb(255, 170, 20), rgb(210, 40, 10), rgb(40, 0, 30));
        drawTextC(SW / 2, 100, LV.def->name, rgb(170, 230, 255));
        snprintf(buf, sizeof buf, "DELIVER %d FARES", LV.def->quota);
        drawTextC(SW / 2, 114, buf, rgb(255, 255, 255));
        if (LV.def->gravity < 0.9f) drawTextC(SW / 2, 124, "LOW GRAVITY", rgb(120, 255, 120));
        else if (LV.def->gravity > 1.2f) drawTextC(SW / 2, 124, "HIGH GRAVITY", rgb(255, 120, 120));
        else if (stateT > 1.6f) drawTextC(SW / 2, 124, "GET READY!", ((int)(tglobal * 6) & 1) ? rgb(255, 255, 255) : rgb(255, 210, 60));
    } else if (gstate == GS_DONE) {
        banner(50, 30 + numJoined() * 11 + 10);
        drawFancyText(SW / 2, 58, "LEVEL COMPLETE!", 8, rgb(200, 255, 200), rgb(80, 230, 80), rgb(20, 120, 40), rgb(0, 30, 0));
        drawStandings(76);
    } else if (gstate == GS_OVER || gstate == GS_WIN) {
        banner(40, 160);
        if (gstate == GS_OVER) drawFancyText(SW / 2, 50, "GAME OVER", 16, rgb(255, 200, 200), rgb(255, 60, 40), rgb(140, 0, 0), rgb(30, 0, 0));
        else drawFancyText(SW / 2, 50, "ALL FARES DONE!", 16, rgb(255, 250, 170), rgb(255, 170, 20), rgb(210, 40, 10), rgb(40, 0, 30));
        drawStandings(80);
        if (stateT > 2.f) drawTextC(SW / 2, 184, "PRESS " START_LABEL, ((int)(tglobal * 3) & 1) ? rgb(255, 255, 255) : rgb(150, 150, 170));
    } else if (gstate == GS_PAUSE) {
        banner(90, 50);
        drawFancyText(SW / 2, 98, "PAUSED", 16, rgb(255, 255, 255), rgb(170, 200, 255), rgb(80, 100, 200), rgb(0, 0, 40));
        drawTextC(SW / 2, 122, START_LABEL " RESUME   " QUIT_LABEL " QUIT", rgb(200, 200, 220));
    }
}

// ------------------------------------------------------------------ input
static std::vector<SDL_GameController*> controllers;
static const Uint8* keys = nullptr;

// Opens a game controller by device index unless it is already open. SDL sends
// CONTROLLERDEVICEADDED for controllers present at startup as well, so this guards
// against duplicates. On Switch, devkitPro's SDL2 maps every player slot (Pro
// Controller, Joy-Con pair, handheld, single sideways Joy-Con) to the standard buttons.
static void openController(int deviceIndex) {
    if (!SDL_IsGameController(deviceIndex)) return;
    SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(deviceIndex);
    for (auto* gc : controllers)
        if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gc)) == id) return;
    if (SDL_GameController* gc = SDL_GameControllerOpen(deviceIndex)) controllers.push_back(gc);
}

static Input readSrc(int src, SDL_JoystickID jid) {
    Input in;
    if (src == SRC_PAD) {
        for (auto* gc : controllers) {
            if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gc)) != jid) continue;
            const int DZ = 14000;
            int ax = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX), ay = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY);
            in.u = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_UP) || ay < -DZ;
            in.d = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_DOWN) || ay > DZ;
            in.l = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_LEFT) || ax < -DZ;
            in.r = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || ax > DZ;
            auto btn = [&](SDL_GameControllerButton b) { return SDL_GameControllerGetButton(gc, b) != 0; };
            bool trig = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000 ||
                        SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000;
            in.a = btn(SDL_CONTROLLER_BUTTON_A) || btn(SDL_CONTROLLER_BUTTON_X) ||
                   btn(SDL_CONTROLLER_BUTTON_LEFTSHOULDER) || btn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) || trig;
            in.b = btn(SDL_CONTROLLER_BUTTON_B) || btn(SDL_CONTROLLER_BUTTON_Y);
            in.start = btn(SDL_CONTROLLER_BUTTON_START);
            in.back = btn(SDL_CONTROLLER_BUTTON_BACK);
#ifdef __SWITCH__
            // a single Joy-Con has only one of +/-, so either one pauses on Switch
            in.start = in.start || in.back;
            in.back = false;
#endif
        }
    } else if (src == SRC_KEYA && keys) {
        in.u = keys[SDL_SCANCODE_UP]; in.d = keys[SDL_SCANCODE_DOWN]; in.l = keys[SDL_SCANCODE_LEFT]; in.r = keys[SDL_SCANCODE_RIGHT];
        in.a = keys[SDL_SCANCODE_SPACE] || keys[SDL_SCANCODE_RCTRL]; in.b = keys[SDL_SCANCODE_RSHIFT];
        in.start = keys[SDL_SCANCODE_RETURN]; in.back = keys[SDL_SCANCODE_ESCAPE];
    } else if (src == SRC_KEYB && keys) {
        in.u = keys[SDL_SCANCODE_W]; in.d = keys[SDL_SCANCODE_S]; in.l = keys[SDL_SCANCODE_A]; in.r = keys[SDL_SCANCODE_D];
        in.a = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_LCTRL]; in.b = keys[SDL_SCANCODE_Q];
        in.start = keys[SDL_SCANCODE_RETURN]; in.back = keys[SDL_SCANCODE_ESCAPE];
    }
    return in;
}

// unjoined sources ("lobby") for join detection
struct Lobby { int src; SDL_JoystickID jid; Input prev, cur; };
static std::vector<Lobby> lobby;

static bool srcTaken(int src, SDL_JoystickID jid) {
    for (auto& p : P) if (p.joined && p.src == src && (src != SRC_PAD || p.jid == jid)) return true;
    return false;
}
static int joinPlayer(int src, SDL_JoystickID jid) {
    for (int i = 0; i < MAXP; i++)
        if (!P[i].joined) {
            P[i] = Player();
            P[i].joined = true; P[i].src = src; P[i].jid = jid;
            audio.play(SFX_JOIN);
            return i;
        }
    return -1;
}

static bool botMode = false;
// Autopilot used only by the headless test harness (--bot). It BFS-plans a path on
// the tile grid (taxi = 2x2 tiles) and follows the waypoints with a PD controller.
// Used to verify that every level can be completed.
static bool botFree(int tx, int ty) {
    for (int y = ty - 1; y < ty + 2; y++)
        for (int x = tx - 1; x < tx + 3; x++) {
            if (y < 0) { if (x < 0 || x >= MW) return false; continue; }
            char c = LV.T(x, y);
            if (isWall(c) || isPadC(c) || c == '=' || c == 'X' || doorSolid(c, LV)) return false;
            if (c == 'G' && LV.gateAnim < 1.f) return false;
        }
    return true;
}
static std::vector<SDL_Point> botPath; static int botPathI = 0; static float botReplan = 0; static int botGoalX = -99, botGoalY = -99;
static void botPlan(int sx, int sy, int gx, int gy) {
    botPath.clear(); botPathI = 0;
    const int OY = 3, HH = MH + OY;   // allow rows above the map (for the exit)
    std::vector<int> prev(MW * HH, -2);
    auto id = [&](int x, int y) { return (y + OY) * MW + x; };
    if (sx < 0 || sx >= MW - 1 || sy < -OY || sy >= MH) return;
    std::vector<SDL_Point> q{{sx, sy}}; prev[id(sx, sy)] = -1;
    for (size_t h = 0; h < q.size(); h++) {
        SDL_Point c = q[h];
        if (c.x == gx && c.y == gy) break;
        const int D[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (auto& d : D) {
            int nx = c.x + d[0], ny = c.y + d[1];
            if (nx < 0 || nx >= MW - 1 || ny < -OY || ny >= MH - 1) continue;
            if (prev[id(nx, ny)] != -2 || !botFree(nx, ny)) continue;
            prev[id(nx, ny)] = id(c.x, c.y); q.push_back({nx, ny});
        }
    }
    if (prev[id(gx, gy)] == -2) return;
    for (int k = id(gx, gy); k != -1; k = prev[k]) botPath.push_back({k % MW, k / MW - OY});
    std::reverse(botPath.begin(), botPath.end());
}
static Input botInput(int i) {
    Player& p = P[i];
    Input in;
    if (!p.alive) { botPath.clear(); return in; }
    int gx = -99, gy = -99;
    auto padGoal = [&](int k) { const PadE& pd = LV.pads[k]; gx = (pd.x + pd.w / 2 - 8) / 8; gy = pd.y / 8 - 2; };
    if (p.pass >= 0) {
        if (PASS[p.pass].up) { if (LV.gateAnim < 1.f) return in; for (int x = 0; x < MW; x++) if (LV.tile[0][x] == 'G') { gx = x + 1; break; } gy = -3; }
        else padGoal(padByChar(PASS[p.pass].dest));
    } else {
        float bd = 1e9f; int bk = -1;
        for (auto& q : PASS) if (q.state == PS_WAIT) {
            const PadE& pd = LV.pads[q.pad];
            float d = std::fabs(pd.x - p.x) + std::fabs(pd.y - p.y);
            if (d < bd) { bd = d; bk = q.pad; }
        }
        if (p.score >= FUEL_PRICE && (p.fuel < 40 || (p.landed && p.landPad >= 0 && LV.pads[p.landPad].id == 'F' && p.fuel < 98))) bk = padByChar('F');
        if (bk < 0) return in;
        padGoal(bk);
    }
    if (p.landed) {
        int k = p.landPad;
        if (k >= 0 && gx == (LV.pads[k].x + LV.pads[k].w / 2 - 8) / 8 && gy == LV.pads[k].y / 8 - 2) return in;  // wait to board
        in.u = true; botPath.clear(); return in;
    }
    botReplan -= 1 / 60.f;
    if (gx != botGoalX || gy != botGoalY || botPath.empty() || botReplan <= 0) {
        botGoalX = gx; botGoalY = gy; botReplan = 1.0f;
        botPlan((int)std::lround(p.x / 8), (int)std::floor(p.y / 8), gx, gy >= 0 && botFree(gx, gy - 2) ? gy - 2 : gy);
    }
    int hoverY = gy >= 0 && botFree(gx, gy - 2) ? gy - 2 : gy;   // approach pads from above, then drop vertically
    if (botPath.empty()) { in.u = p.vy > 0; return in; }
    while (botPathI < (int)botPath.size() - 1) {
        SDL_Point w = botPath[botPathI];
        if (std::fabs(w.x * 8 - p.x) < 5 && std::fabs(w.y * 8 - p.y) < 5) botPathI++; else break;
    }
    int look = botPathI;
    while (look < (int)botPath.size() - 1 && look < botPathI + 3) look++;
    SDL_Point w = botPath[look];
    bool last = look == (int)botPath.size() - 1;
    float tx = w.x * 8.f, ty = w.y * 8.f;
    if (last && gy >= 0) ty = (p.gear && std::fabs(tx - p.x) <= 3) ? gy * 8.f + 14.f : hoverY * 8.f;
    float wantVx = std::clamp((tx - p.x) * 1.6f, -48.f, 48.f);
    float wantVy = std::clamp((ty - p.y) * 1.6f, -45.f, last ? 22.f : 34.f);
    // gear: up to steer sideways, down only for the final vertical descent onto the pad
    bool wantGear = last && gy >= 0 && std::fabs(tx - p.x) <= 1.5f && std::fabs(p.vx) < 6;
    if (last && gy >= 0 && (std::fabs(tx - p.x) > 1.5f || p.gearRows() < 2)) wantVy = std::min(wantVy, 0.f);
    // descending onto a moving pad: limit speed relative to the platform
    if (last && gy >= 0)
        for (auto& pd : LV.pads)
            if (pd.plat >= 0 && (pd.x + pd.w / 2 - 8) / 8 == gx && pd.y / 8 - 2 == gy)
                wantVy = std::min(wantVy, LV.plats[pd.plat].vy + 16.f);
    in.u = p.vy > wantVy + 1;
    in.d = p.vy < wantVy - 20;
    in.r = p.vx < wantVx - 2; in.l = p.vx > wantVx + 2;
    if (p.gear != wantGear) in.a = !p.prev.a;
    return in;
}

static void pollInputs() {
    keys = SDL_GetKeyboardState(nullptr);
    for (auto& p : P) { p.prev = p.cur; p.cur = p.joined ? readSrc(p.src, p.jid) : Input(); }
    if (autoplay) {
        for (int i = 0; i < MAXP; i++) {
            Player& p = P[i];
            if (!p.joined) continue;
            Input in;
            uint32_t h = hash2((int)(tglobal * 2.5f), i, 77);
            in.u = (h & 3) != 0 && p.cy() > 40; in.l = (h >> 3) % 4 == 0; in.r = (h >> 5) % 4 == 0; in.d = false;
            in.a = (hash2((int)(tglobal * 7), i, 3) % 40) == 0;
            p.cur = in;
        }
    }
    if (botMode && P[0].joined) P[0].cur = botInput(0);
    // lobby
    std::vector<Lobby> nl;
    auto addL = [&](int src, SDL_JoystickID jid) {
        Lobby l{src, jid, {}, {}};
        for (auto& o : lobby) if (o.src == src && o.jid == jid) l.prev = o.cur;
        l.cur = readSrc(src, jid);
        nl.push_back(l);
    };
    for (auto* gc : controllers) addL(SRC_PAD, SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gc)));
    addL(SRC_KEYA, -1); addL(SRC_KEYB, -1);
    lobby = nl;
}
static bool lobbyPressed(const Lobby& l, bool Input::*b) { return l.cur.*b && !(l.prev.*b); }

// ------------------------------------------------------------------ update
static void update(float dt) {
    tglobal += dt;
    stateT += dt;
    switch (gstate) {
        case GS_TITLE: {
            for (auto& l : lobby) {
                bool taken = srcTaken(l.src, l.jid);
                bool joinKey = l.src == SRC_PAD ? lobbyPressed(l, &Input::a) : lobbyPressed(l, &Input::a);
                if (!taken && joinKey) joinPlayer(l.src, l.jid);
                if (taken && l.src == SRC_PAD && lobbyPressed(l, &Input::b)) {
                    for (auto& p : P) if (p.joined && p.src == l.src && p.jid == l.jid) { p.joined = false; audio.play(SFX_BLIP); }
                }
                if (lobbyPressed(l, &Input::l)) { startLevel_ = (startLevel_ + NLEVELS - 1) % NLEVELS; audio.play(SFX_BLIP, 0.5f); }
                if (lobbyPressed(l, &Input::r)) { startLevel_ = (startLevel_ + 1) % NLEVELS; audio.play(SFX_BLIP, 0.5f); }
                if (lobbyPressed(l, &Input::start)) {
                    if (!taken && (l.src == SRC_PAD)) joinPlayer(l.src, l.jid);
                    if (numJoined() == 0) joinPlayer(l.src, l.jid);
                    newGame();
                    return;
                }
            }
            break;
        }
        case GS_INTRO:
            for (int i = 0; i < MAXP; i++) audio.thrust(i, 0, 0);
            updateParticles(dt);
            if (stateT > 2.6f) { gstate = GS_PLAY; stateT = 0; }
            break;
        case GS_PLAY: {
            for (auto& p : P) if (p.joined && p.pressed(&Input::start)) { gstate = GS_PAUSE; audio.silenceThrust(); audio.play(SFX_BLIP); return; }
            // drop-in join
            for (auto& l : lobby)
                if (!srcTaken(l.src, l.jid) && lobbyPressed(l, &Input::a)) {
                    int i = joinPlayer(l.src, l.jid);
                    if (i >= 0) { P[i].deadT = 0.8f; P[i].alive = false; }
                }
            updateWorld(dt);
            for (int i = 0; i < MAXP; i++) {
                Player& p = P[i];
                if (!p.joined) continue;
                if (p.alive) updateTaxi(i, dt);
                else if (p.lives > 0 && !p.exited) { p.deadT -= dt; if (p.deadT <= 0) respawn(i); }
                if (gstate != GS_PLAY) return;
                if (p.score >= p.nextLife) { p.lives = std::min(9, p.lives + 1); p.nextLife += EXTRA_LIFE_EVERY; audio.play(SFX_EXTRALIFE); addText(p.cx(), p.y - 10, "1UP!", rgb(255, 255, 255)); }
            }
            taxiCollisions();
            updatePassengers(dt);
            passengerCollisions();
            updateParticles(dt);
            checkLevelExit();
            if (gstate != GS_PLAY) return;
            bool anyAlive = false;
            for (auto& p : P) if (p.joined && (p.alive || p.lives > 0)) anyAlive = true;
            if (!anyAlive && numJoined() > 0) { gstate = GS_OVER; stateT = 0; audio.silenceThrust(); audio.play(SFX_GAMEOVER); }
            break;
        }
        case GS_PAUSE:
            for (auto& p : P) {
                if (!p.joined) continue;
                if (p.pressed(&Input::start)) { gstate = GS_PLAY; audio.play(SFX_BLIP); }
                if (p.pressed(&Input::back) || p.pressed(&Input::b)) { gstate = GS_TITLE; audio.play(SFX_BLIP); }
            }
            break;
        case GS_DONE:
            updateParticles(dt);
            if (stateT > 4.5f) {
                if (curLevel + 1 >= NLEVELS) { gstate = GS_WIN; stateT = 0; audio.play(SFX_EXTRALIFE); }
                else startLevel(curLevel + 1);
            }
            break;
        case GS_OVER:
        case GS_WIN:
            updateParticles(dt);
            if (stateT > 2.f)
                for (auto& l : lobby)
                    if (lobbyPressed(l, &Input::start) || lobbyPressed(l, &Input::a)) { gstate = GS_TITLE; stateT = 0; audio.play(SFX_BLIP); }
            break;
    }
}

// ------------------------------------------------------------------ main
static void saveShot(const char* path) {
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(fb, SW, SH, 32, SW * 4, SDL_PIXELFORMAT_ARGB8888);
    SDL_SaveBMP(s, path);
    SDL_FreeSurface(s);
}

// Verifies that collision masks are exactly the drawn pixels and that taxi/taxi and
// taxi/level collisions are pixel perfect (not bounding boxes). Ground truth comes from
// actually rendering the sprites with blitTaxi() into the framebuffer.
static std::vector<uint8_t> renderTaxiPixels(int i) {
    const uint32_t KEY = 0xFF123456u;
    for (auto& px : fb) px = KEY;
    Player save = P[i];
    P[i].thU = P[i].thD = P[i].thL = P[i].thR = false;
    blitTaxi(i, 20, 20);
    P[i] = save;
    std::vector<uint8_t> m(160, 0);
    for (int y = 0; y < 10; y++) for (int x = 0; x < 16; x++) m[y * 16 + x] = fb[(20 + y) * SW + 20 + x] != KEY;
    for (int y = 0; y < SH; y++) for (int x = 0; x < SW; x++)   // nothing drawn outside the 16x10 box
        if ((x < 20 || x >= 36 || y < 20 || y >= 30) && fb[y * SW + x] != KEY) m.push_back(1);
    return m;
}
// macOS .dmg window background: 320x220 art for a 640x420 pt window (+20 pt spare at the
// bottom), saved 4x as macos/dmg-background.png at 144 dpi so it is sharp on Retina screens.
// Finder places the 128 pt app and Applications icons centred at (160,236) and (480,236) pt,
// i.e. (80,118) and (240,118) here; the arrow sits between them. The lower part is light so
// Finder's black icon labels stay readable.
static void renderDmgBackground() {
    const int W = 320, H = 220;   // 10 extra art rows (20 pt) of margin below the 640x420 pt window
    const uint32_t stops[5] = {rgb(8, 6, 30), rgb(40, 18, 70), rgb(140, 60, 120), rgb(236, 150, 110), rgb(250, 230, 206)};
    const float at[5] = {0.f, 0.27f, 0.46f, 0.63f, 0.76f};
    Rng r(4711);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float t = (float)y / (H - 1) + (bayer(x, y) - 0.5f) * 0.035f;
            uint32_t c = stops[4];
            for (int k = 0; k < 4; k++)
                if (t < at[k + 1]) { c = mix(stops[k], stops[k + 1], (t - at[k]) / (at[k + 1] - at[k])); break; }
            fb[y * SW + x] = c;
        }
    for (int i = 0; i < 90; i++) {                       // stars, fading out toward the horizon
        int x = r.i(W), y = (int)(std::pow(r.f(), 1.8f) * 95);
        float b = (1.f - y / 95.f) * (0.4f + 0.6f * r.f());
        fb[y * SW + x] = mix(fb[y * SW + x], rgb(255, 255, 255), b);
        if (b > 0.85f && x > 0 && x < W - 1) {
            fb[y * SW + x - 1] = mix(fb[y * SW + x - 1], rgb(255, 255, 255), 0.4f);
            fb[y * SW + x + 1] = mix(fb[y * SW + x + 1], rgb(255, 255, 255), 0.4f);
        }
    }
    drawFancyText(W / 2, 12, "SPACE TAXI", 24, rgb(255, 250, 170), rgb(255, 170, 20), rgb(210, 40, 10), rgb(40, 0, 30));
    drawFancyText(W / 2, 42, "MAYHEM", 16, rgb(170, 250, 255), rgb(60, 200, 255), rgb(30, 90, 220), rgb(0, 10, 40));
    // big arrow from the app icon to the Applications icon, in the logo's colours
    const int ax0 = 124, ax1 = 200, ay = 118, shaft = 5, headLen = 18, headH = 13;
    auto inArrow = [&](int x, int y) {
        if (x >= ax0 && x < ax1 - headLen && std::abs(y - ay) <= shaft) return true;
        if (x >= ax1 - headLen && x <= ax1) { int half = (ax1 - x) * headH / headLen; return std::abs(y - ay) <= half; }
        return false;
    };
    for (int y = ay - headH - 3; y <= ay + headH + 4; y++)
        for (int x = ax0 - 3; x <= ax1 + 4; x++) {
            bool in = inArrow(x, y), near = false;
            for (int dy = -1; dy <= 1 && !near; dy++) for (int dx = -1; dx <= 1; dx++) if (inArrow(x + dx, y + dy)) { near = true; break; }
            if (!in && inArrow(x - 2, y - 2)) fb[y * SW + x] = mix(fb[y * SW + x], rgb(40, 0, 30), 0.45f);   // drop shadow
            if (near && !in) fb[y * SW + x] = rgb(40, 0, 30);                                              // outline
            if (in) {
                float t = (float)(y - (ay - headH)) / (2 * headH) + (bayer(x, y) - 0.5f) * 0.15f;
                uint32_t c = t < 0.5f ? mix(rgb(255, 250, 170), rgb(255, 170, 20), t * 2) : mix(rgb(255, 170, 20), rgb(210, 40, 10), (t - 0.5f) * 2);
                fb[y * SW + x] = c;
            }
        }
    // a little taxi zooming along above the arrow
    P[0] = Player(); P[0].face = 0; P[0].gearExt = 0; P[0].gear = false; P[0].fuel = 50; P[0].thR = true; tglobal = 0.25f;
    rng = Rng(5);
    blitTaxi(0, 150, 94);
    for (int k = 0; k < 3; k++) for (int x = 132 + k * 2; x < 146; x += 4) fb[(97 + k * 2) * SW + x] = mix(fb[(97 + k * 2) * SW + x], rgb(255, 255, 255), 0.6f);
    drawTextC(W / 2, 192, "DRAG THE TAXI INTO APPLICATIONS", rgb(70, 30, 60), 0);
}

// Staged "typical game" scene on Girder City, used for the README screenshot.
static void renderScene() {
    for (int i = 0; i < 4; i++) { P[i] = Player(); P[i].joined = true; P[i].src = SRC_PAD; }
    startLevel(3); gstate = GS_PLAY; stateT = 10; LV.time = 7.3f; tglobal = 7.3f;
    rng = Rng(777);
    auto padIdx = [](char c) { return padByChar(c); };
    struct T { float x, y; int face; float gear; bool u, side; long score; int lives; float fuel; };
    const T t[4] = {
        {150, 92, 0, 0, true, true, 1240, 3, 72},     // P1 carrying a fare, flying right
        {0, 0, 1, 2, false, false, 860, 4, 45},       // P2 parked on pad 2
        {196, 52, 1, 0, false, true, 1475, 2, 88},    // P3 cruising left, gear up
        {72, 86, 0, 2, true, false, 390, 5, 18},      // P4 lowering onto pad 4, low fuel
    };
    for (int i = 0; i < 4; i++) {
        Player& p = P[i];
        p.alive = true; p.landed = false; p.face = t[i].face; p.gearExt = t[i].gear; p.gear = t[i].gear > 0;
        p.x = t[i].x; p.y = t[i].y; p.score = t[i].score; p.lives = t[i].lives; p.fuel = t[i].fuel; p.invuln = 0;
        p.thU = t[i].u; p.thL = t[i].side && p.face == 1; p.thR = t[i].side && p.face == 0;
    }
    const PadE& p2 = LV.pads[padIdx('2')];
    P[1].x = p2.x + 8.f; P[1].y = p2.y - 10.f; P[1].landed = true; P[1].landPad = padIdx('2');
    for (auto& q : PASS) q = Passenger();
    auto mk = [&](int k, char pad, int st, int g, int skin, int hair, int top, int bot, float relx, int state, float anim, float dir) {
        Passenger& q = PASS[k];
        q.state = state; q.pad = padIdx(pad); q.style = st; q.gender = g; q.skin = SKIN_TONES[skin]; q.hair = HAIR_COLS[hair];
        q.top = TOP_COLS[top]; q.bottom = g ? SKIRT_COLS[bot] : PANTS_COLS[bot]; q.relx = relx; q.anim = anim; q.dir = dir;
    };
    mk(0, '2', 4, 1, 1, 3, 1, 0, 6, PS_WAIT, 0.1f, 1);          // walking to the parked red taxi
    mk(1, '5', 1, 0, 4, 0, 2, 0, 0, PS_WAIT, 0.15f, 1);          // waving on pad 5
    mk(2, '6', 5, 1, 3, 4, 7, 3, 26, PS_WAIT, 3.f, -1);          // standing on pad 6
    mk(3, '3', 0, 0, 0, 2, 5, 1, 1, PS_BEAM, 0, 1); PASS[3].beamT = 0.5f;   // delivered, beaming out
    mk(4, '7', 2, 0, 2, 1, 0, 2, 0, PS_RIDE, 0, 1); PASS[4].dest = '7'; PASS[4].taxi = 0; P[0].pass = 4; P[0].tip = 18;
    LV.delivered = 3;
    rebuildMasks();
    // exhaust trails
    for (int i = 0; i < 4; i++) {
        Player& p = P[i];
        for (int k = 0; k < 14; k++) {
            float a = k / 14.f;
            if (p.thU) addPart(p.cx() + rng.r(-1.5f, 1.5f), p.y + 11 + a * 12, 0, 0, 0.3f * (1 - a) + 0.05f, mix(rgb(255, 230, 120), rgb(255, 80, 0), a), 0);
            if (p.thL || p.thR) {
                float ex = p.face == 0 ? p.x - 3 - a * 14 : p.x + 18 + a * 14;
                addPart(ex, p.y + 5 + rng.r(-1, 1), 0, 0, 0.25f * (1 - a) + 0.05f, mix(rgb(255, 230, 120), rgb(255, 80, 0), a), 0);
            }
        }
    }
    const PadE& p3 = LV.pads[padIdx('3')];
    ftexts.push_back({p3.x + 6.f, p3.y - 16.f, 1.2f, "$47", PCOL[2].light});
    render();
}

static int runSelfTest() {
    int fails = 0;
    startLevel(0); gstate = GS_PLAY;
    // 1) masks == drawn pixels for every facing and gear state
    for (int f = 0; f < 2; f++)
        for (int g = 0; g < 3; g++) {
            P[0].face = f; P[0].gearExt = (float)g;
            std::vector<uint8_t> drawn = renderTaxiPixels(0);
            bool ok = drawn.size() == 160 && memcmp(drawn.data(), taxiMask[f][g], 160) == 0;
            int n = 0; for (int k = 0; k < 160; k++) n += taxiMask[f][g][k];
            printf("mask face=%d gear=%d: %3d of 160 box pixels solid -> %s\n", f, g, n, ok ? "matches drawing" : "MISMATCH");
            if (!ok) fails++;
        }
    // 2) taxi vs taxi at every offset where the bounding boxes overlap
    int boxOnly = 0, pixelHits = 0, wrong = 0;
    for (int fa = 0; fa < 2; fa++) for (int fb_ = 0; fb_ < 2; fb_++) for (int ga = 0; ga < 3; ga++) for (int gb = 0; gb < 3; gb++)
    for (int dy = -9; dy <= 9; dy++) for (int dx = -15; dx <= 15; dx++) {
        for (int k = 0; k < 2; k++) { P[k] = Player(); P[k].joined = true; P[k].alive = true; P[k].lives = 9; }
        P[0].face = fa; P[0].gearExt = (float)ga; P[1].face = fb_; P[1].gearExt = (float)gb;
        std::vector<uint8_t> A = renderTaxiPixels(0), B = renderTaxiPixels(1);
        bool truth = false;
        for (int y = 0; y < 10; y++) for (int x = 0; x < 16; x++) {
            int u = x - dx, v = y - dy;
            if (A[y * 16 + x] && u >= 0 && u < 16 && v >= 0 && v < 10 && B[v * 16 + u]) truth = true;
        }
        P[0].x = 100; P[0].y = 100; P[1].x = 100.f + dx; P[1].y = 100.f + dy;
        taxiCollisions();
        bool hit = !P[0].alive && !P[1].alive;
        if (hit != truth) wrong++;
        if (truth) pixelHits++; else boxOnly++;
    }
    printf("taxi/taxi: %d overlapping-box placements, %d real pixel contacts, %d box-only near misses, %d wrong results\n",
           boxOnly + pixelHits, pixelHits, boxOnly, wrong);
    if (wrong) fails++;
    // 3) taxi vs level: box overlaps solid pixels but taxi pixels do not -> must survive
    int nearMiss = 0, badLevel = 0;
    for (int y = 1; y < WH - 11; y++) for (int x = 1; x < WW - 17; x++) {
        bool boxHit = false;
        for (int j = 0; j < 10 && !boxHit; j++) for (int i = 0; i < 16; i++) if (LV.solid[(y + j) * WW + x + i]) { boxHit = true; break; }
        if (!boxHit) continue;
        bool body = hitMask(LV.solid, taxiMask[0][2], 16, 10, x, y, false);
        bool truth = false;
        for (int j = 0; j < 10; j++) for (int i = 0; i < 16; i++) if (taxiMask[0][2][j * 16 + i] && LV.solid[(y + j) * WW + x + i]) truth = true;
        if (body != truth) badLevel++;
        if (!truth) nearMiss++;
    }
    printf("taxi/level (level 1): %d positions where the box touches a wall but no taxi pixel does (no collision), %d wrong\n", nearMiss, badLevel);
    if (badLevel) fails++;
    // 4) gate: solid exactly where the sliding doors are drawn, at several opening stages
    int gateBad = 0;
    for (float a : {0.f, 0.3f, 0.6f, 1.f}) {
        LV.gateAnim = a; rebuildMasks(); drawWorld();
        for (int x = 0; x < WW; x++) for (int y = 0; y < 8; y++) {
            if (LV.tile[0][x / 8] != 'G') continue;
            bool drawnDoor = fb[(y + MAPY) * SW + x] != LV.img[y * WW + x];
            if (drawnDoor != (LV.solid[y * WW + x] != 0)) gateBad++;
        }
    }
    printf("gate: %d pixels where mask and drawn doors disagree (over 4 opening stages)\n", gateBad);
    if (gateBad) fails++;
    // 5) passenger collision shape == drawn pixels (every gender, hair style, pose, facing)
    {
        for (int k = 0; k < MAXP; k++) P[k].landed = false;
        const uint32_t KEY = 0xFF123456u;
        int bad = 0, cases = 0;
        for (int g = 0; g < 2; g++) for (int st = 0; st < 7; st++) for (int pose = 0; pose < 3; pose++) for (int d = 0; d < 2; d++) for (int ph = 0; ph < 2; ph++) {
            Passenger q; q.gender = g; q.style = st; q.dir = d ? 1.f : -1.f; q.skin = SKIN_TONES[0]; q.hair = HAIR_COLS[3];
            q.top = TOP_COLS[0]; q.bottom = PANTS_COLS[0]; q.pad = 0;
            if (pose == 0) { q.state = PS_WAIT; q.anim = 3.f; }                 // standing
            else if (pose == 1) { q.state = PS_WAIT; q.anim = ph ? 0.2f : 0.f; } // waving
            else { q.state = PS_EXIT; q.anim = ph ? 0.2f : 0.f; }                // walking
            for (auto& px : fb) px = KEY;
            drawPassenger(q, 40, 40);
            int fr = passFrame(q);
            for (int y = 0; y < 10; y++) for (int x = 0; x < 6; x++)
                if ((fb[(40 + y) * SW + 40 + x] != KEY) != (passGlyph(q, fr, x, y) != '.')) bad++;
            cases++;
        }
        printf("passenger masks: %d sprite variants checked, %d mismatching pixels\n", cases, bad);
        if (bad) fails++;
    }
    // 6) taxi vs standing passenger: every overlapping placement, truth from rendered pixels
    {
        startLevel(0); gstate = GS_PLAY;
        const uint32_t KEY = 0xFF123456u;
        int placements = 0, contacts = 0, wrong = 0, fineBad = 0, bounceBad = 0;
        for (int st = 0; st < 7; st++) for (int f = 0; f < 2; f++) for (int gr = 0; gr < 3; gr += 2)
        for (int dy = -9; dy <= 9; dy++) for (int dx = -15; dx <= 5; dx++) {
            for (auto& q : PASS) q = Passenger();
            Passenger& q = PASS[0];
            q.state = PS_WAIT; q.pad = 0; q.relx = 8; q.anim = 3.f; q.gender = st >= 4; q.style = st; q.dir = 1;
            q.skin = SKIN_TONES[2]; q.hair = HAIR_COLS[1]; q.top = TOP_COLS[2]; q.bottom = SKIRT_COLS[1];
            for (int k = 0; k < MAXP; k++) { P[k] = Player(); }
            P[0].joined = P[0].alive = true; P[0].landed = false; P[0].airT = 9; P[0].face = f; P[0].gearExt = (float)gr; P[0].score = 500;
            const PadE& pd = LV.pads[0];
            int qx = pd.x + 8, qy = pd.y - 10;
            P[0].x = (float)(qx + dx); P[0].y = (float)(qy + dy);
            float cdx = P[0].cx() - (qx + 3.f), cdy = P[0].cy() - (qy + 5.f);
            P[0].vx = cdx > 0 ? -40.f : 40.f; P[0].vy = cdy > 0 ? -30.f : 30.f;     // flying into them
            std::vector<uint8_t> T = renderTaxiPixels(0);
            for (auto& px : fb) px = KEY;
            drawPassenger(q, 40, 40);
            bool truth = false;
            for (int y = 0; y < 10; y++) for (int x = 0; x < 6; x++) {
                if (fb[(40 + y) * SW + 40 + x] == KEY) continue;
                int u = x - dx, v = y - dy;
                if (u >= 0 && u < 16 && v >= 0 && v < 10 && T[v * 16 + u]) truth = true;
            }
            passengerCollisions();
            bool hit = PASS[0].state != PS_WAIT;
            bool away = (P[0].vx * cdx > 0 && std::fabs(P[0].vx) >= 24) || (P[0].vy * cdy > 0 && std::fabs(P[0].vy) >= 24) ||
                        (cdx == 0 && cdy == 0 && P[0].vy <= -24);      // dead centre: bounce up
            if (hit && (PASS[0].state != PS_BEAM || !away)) bounceBad++;
            if (hit != truth) wrong++;
            if (hit && P[0].score != 500 - PASSENGER_HIT_PENALTY) fineBad++;
            placements++; if (truth) contacts++;
        }
        printf("taxi/passenger: %d box-overlapping placements, %d pixel contacts, %d wrong, %d wrong fines, %d missing bounce/beam-out\n", placements, contacts, wrong, fineBad, bounceBad);
        if (wrong || fineBad || bounceBad) fails++;
        // 7) delivered passengers: never hittable, stay on the pad, then beam away
        int exitBad = 0;
        for (int start = 1; start < LV.pads[0].w - 7; start += 3) {
            for (auto& q : PASS) q = Passenger();
            Passenger& q = PASS[0];
            const PadE& pd = LV.pads[0];
            q.state = PS_EXIT; q.pad = 0; q.relx = (float)start; q.exitX = start < pd.w / 2 ? 1.f : (float)(pd.w - 7);
            q.dir = q.exitX > q.relx ? 1.f : -1.f; q.style = 4; q.gender = 1;
            P[0] = Player(); P[0].joined = P[0].alive = true; P[0].airT = 9; P[0].gearExt = 2;
            bool beamed = false;
            for (int t = 0; t < 600 && q.state != PS_NONE; t++) {
                P[0].x = pd.x + q.relx - 5; P[0].y = (float)(pd.y - 12);      // flying taxi sitting right on top of them
                updatePassengers(1 / 60.f);
                if (q.state == PS_BEAM) beamed = true;
                passengerCollisions();
                if (q.state != PS_NONE && (q.relx < 1 || q.relx > pd.w - 7)) exitBad++;
            }
            if (!beamed || q.state != PS_NONE) exitBad++;
        }
        printf("delivered passengers: walk to pad edge + beam out without being hittable -> %s\n", exitBad ? "FAIL" : "ok");
        if (exitBad) fails++;
    }
    // 8) passenger walks back to their spot when the taxi leaves before boarding
    {
        startLevel(0); gstate = GS_PLAY;
        for (auto& q : PASS) q = Passenger();
        for (int k = 0; k < MAXP; k++) P[k] = Player();
        Passenger& q = PASS[0];
        q.state = PS_WAIT; q.pad = 0; q.relx = q.homeX = 0; q.heyT = 0; q.waveT = 99; q.style = 0;
        const PadE& pd = LV.pads[0];
        P[0].joined = P[0].alive = true; P[0].landed = true; P[0].landPad = 0; P[0].landedT = 5;
        P[0].x = pd.x + 8.f; P[0].y = pd.y - 10.f; P[0].face = 0; P[0].gearExt = 2;
        spawnT = 999;
        for (int t = 0; t < 20; t++) updatePassengers(1 / 60.f);            // starts walking over
        float walked = q.relx;
        P[0].landed = false; P[0].landPad = -1; P[0].y -= 40; P[0].airT = 9;  // taxi takes off
        for (int t = 0; t < 240; t++) { updatePassengers(1 / 60.f); passengerCollisions(); }
        bool ok = walked > 1.5f && q.state == PS_WAIT && q.relx == q.homeX && passFrame(q) != 1 && passFrame(q) != 2;
        printf("taxi leaves early: passenger walked %.1f px toward it, then back to the spot -> %s\n", walked, ok ? "ok" : "FAIL");
        if (!ok) fails++;
    }
    // 9) + 10) one "Hey, taxi!" per passenger; up to 3 waiting at once on different pads, spaced in time
    {
        startLevel(0); gstate = GS_PLAY;
        for (auto& q : PASS) q = Passenger();
        for (int k = 0; k < MAXP; k++) P[k] = Player();
        P[0].joined = true; P[0].alive = false; P[0].lives = 0;       // nobody picks anyone up
        heyCount = 0; spawnT = 1.f;
        int maxWaiting = 0, spawns = 0, prevWaiting = 0; float lastSpawn = -99, minGap = 99; bool distinct = true;
        for (int t = 0; t < 60 * 60; t++) {
            updatePassengers(1 / 60.f);
            int w = 0; std::vector<int> pads;
            for (auto& q : PASS) if (q.state == PS_WAIT) { w++; pads.push_back(q.pad); }
            std::sort(pads.begin(), pads.end());
            if (std::adjacent_find(pads.begin(), pads.end()) != pads.end()) distinct = false;
            if (w > prevWaiting) { spawns++; float now = t / 60.f; minGap = std::min(minGap, now - lastSpawn); lastSpawn = now; }
            prevWaiting = w; maxWaiting = std::max(maxWaiting, w);
        }
        bool ok = maxWaiting == 3 && spawns == 3 && heyCount == 3 && distinct && minGap >= 13.f;
        printf("spawning: max %d waiting at once on different pads, %d spawned %.1fs+ apart, %d \"Hey, taxi!\" calls in 60s -> %s\n",
               maxWaiting, spawns, minGap, heyCount, ok ? "ok" : "FAIL");
        if (!ok) fails++;
    }
    // 11) fuel economy
    {
        for (int k = 0; k < MAXP; k++) P[k] = Player();
        P[0].joined = true; P[0].lives = 5;
        startLevel(0); gstate = GS_PLAY;
        P[0].fuel = 37; P[0].score = 200;
        startLevel(1); gstate = GS_PLAY;
        bool carry = P[0].alive && P[0].fuel == 37;
        // crash: repair fee + full tank
        killPlayer(0, nullptr);
        bool crash = P[0].score == 200 - CRASH_FEE && P[0].fuel == FUEL_MAX && P[0].lives == 4;
        // refuel on the FUEL pad: $1 per unit, stops when broke
        int fpad = padByChar('F');
        Slot fs{fpad, LV.pads[fpad].x + 8};
        placeOnSlot(P[0], fs); P[0].cur = P[0].prev = Input();
        P[0].fuel = 40; P[0].score = 30;
        rebuildMasks();
        for (int t = 0; t < 180; t++) updateTaxi(0, 1 / 60.f);
        bool pay = P[0].alive && P[0].score == 0 && std::fabs(P[0].fuel - 70) < 0.01f && P[0].noMoneyShown;
        // stranded with an empty tank on a normal pad -> self-destructs after 4 s
        int npad = padByChar('1');
        Slot ns{npad, LV.pads[npad].x + 8};
        placeOnSlot(P[0], ns); P[0].fuel = 0; P[0].score = 0; int livesBefore = P[0].lives;
        int t = 0;
        for (; t < 600 && P[0].alive; t++) updateTaxi(0, 1 / 60.f);
        bool strand = !P[0].alive && P[0].lives == livesBefore - 1 && P[0].fuel == FUEL_MAX && t >= 239 && t <= 242;
        printf("fuel: carried to next level %s, crash fee + full tank %s, $1/unit and stops at $0 %s, stranded self-destruct after %.1fs %s\n",
               carry ? "ok" : "FAIL", crash ? "ok" : "FAIL", pay ? "ok" : "FAIL", t / 60.f, strand ? "ok" : "FAIL");
        if (!(carry && crash && pay && strand)) fails++;
    }
    // 12) landing on the wrong pad with a passenger repeats "Pad X, please!"
    {
        for (int k = 0; k < MAXP; k++) P[k] = Player();
        P[0].joined = true;
        startLevel(0); gstate = GS_PLAY;
        for (auto& q : PASS) q = Passenger();
        PASS[0].state = PS_RIDE; PASS[0].dest = '2'; PASS[0].taxi = 0;
        auto landOn = [&](char c) { int k = padByChar(c); placeOnSlot(P[0], Slot{k, LV.pads[k].x + 8}); P[0].pass = 0; onLanded(0); };
        padReminderCount = 0;
        landOn('1'); int afterWrong = padReminderCount;
        landOn('F'); int afterFuel = padReminderCount;
        landOn('2'); bool delivered = PASS[0].state == PS_EXIT && padReminderCount == afterFuel;
        bool ok = afterWrong == 1 && afterFuel == 1 && delivered;
        printf("wrong pad reminder: wrong pad -> %d, fuel pad -> +%d, right pad delivers -> %s\n", afterWrong, afterFuel - afterWrong, ok ? "ok" : "FAIL");
        if (!ok) fails++;
    }
    // 13) landing needs both feet fully on the pad: sweep the taxi across a pad edge, 1 px at a time
    {
        int ok = 0, bad = 0, landedN = 0, crashedN = 0;
        for (int face = 0; face < 2; face++)
            for (char padc : {'1', '2', '3', '4'}) {
                for (int k = 0; k < MAXP; k++) P[k] = Player();
                P[0].joined = true; P[0].lives = 99;
                startLevel(0); gstate = GS_PLAY;
                for (auto& q : PASS) q = Passenger();
                spawnT = 999;
                int pk = padByChar(padc);
                const PadE pd = LV.pads[pk];
                for (int x = pd.x - 15; x <= pd.x + pd.w - 1; x++) {
                    // expected: do the feet touch the pad top at all, and are all of them supported?
                    int touch = 0, total = 0;
                    for (int c = 0; c < 16; c++) if (gearOnly[face][2][9 * 16 + c]) { total++; if (x + c >= pd.x && x + c < pd.x + pd.w) touch++; }
                    if (touch == 0) continue;
                    bool expectLand = touch == total;
                    Player& p = P[0];
                    p = Player(); p.joined = true; p.alive = true; p.lives = 99; p.face = face; p.gear = true; p.gearExt = 2;
                    p.x = (float)x; p.y = pd.y - 10 - 6.f; p.vx = 0; p.vy = 15; p.airT = 9; p.fuel = 50;
                    p.cur = p.prev = Input();
                    for (int t = 0; t < 120 && p.alive && !p.landed; t++) { rebuildMasks(); updateTaxi(0, 1 / 60.f); }
                    bool landed = p.alive && p.landed;
                    if (landed == expectLand) ok++; else bad++;
                    if (landed) landedN++; else crashedN++;
                }
            }
        printf("landing footprint: %d touchdowns across pad edges, %d safe (both feet fully on), %d crashed (a foot pixel off), %d wrong\n",
               ok + bad, landedN, crashedN, bad);
        if (bad) fails++;
    }
    // 14) rough landings: shake; with a passenger aboard no tip and $5 off the fare, never below 0
    {
        auto run = [&](float vy, bool withPass, char padc, int presetCut) -> std::pair<bool, long> {
            for (int k = 0; k < MAXP; k++) P[k] = Player();
            P[0].joined = true; P[0].lives = 99;
            startLevel(0); gstate = GS_PLAY;
            for (auto& q : PASS) q = Passenger();
            spawnT = 999;
            const PadE pd = LV.pads[padByChar(padc)];
            Player& p = P[0];
            p.alive = true; p.landed = false; p.landPad = p.landPlat = -1; p.roughT = 0;
            p.face = 0; p.gear = true; p.gearExt = 2; p.fuel = 50; p.airT = 9; p.score = 0;
            p.x = pd.x + 8.f; p.y = pd.y - 10 - 3.f; p.vx = 0; p.vy = vy; p.cur = p.prev = Input();
            if (withPass) { PASS[0].state = PS_RIDE; PASS[0].dest = '2'; PASS[0].taxi = 0; p.pass = 0; p.tip = 20; PASS[0].fareCut = presetCut; }
            bool shook = false;
            for (int t = 0; t < 120 && p.alive && !p.landed; t++) { rebuildMasks(); updateTaxi(0, 1 / 60.f); }
            shook = p.roughT > 0;
            return {p.alive && p.landed && shook, p.score};
        };
        auto gentle = run(15, true, '2', 0);   // normal delivery: fare + tip
        auto rough = run(40, true, '2', 0);    // rough delivery: fare - 5, no tip
        auto roughEmpty = run(40, false, '1', 0);
        auto floor0 = run(40, true, '2', 8);   // earlier cuts already: fare can't go below 0
        bool ok = !gentle.first && gentle.second == FARE_BASE + 20 && rough.first && rough.second == FARE_BASE - ROUGH_FARE_CUT &&
                  roughEmpty.first && roughEmpty.second == 0 && floor0.second == 0;
        printf("rough landing: gentle pays $%ld, rough pays $%ld (no tip, -$%d), shakes %s, never below $0 %s -> %s\n",
               gentle.second, rough.second, ROUGH_FARE_CUT, rough.first && roughEmpty.first ? "yes" : "NO", floor0.second == 0 ? "yes" : "NO", ok ? "ok" : "FAIL");
        if (!ok) fails++;
    }
    // 15) top bar shows "+$fare" counting down while a passenger rides, then the score again
    {
        for (int k = 0; k < MAXP; k++) P[k] = Player();
        P[0].joined = true; P[0].lives = 9;
        startLevel(0); gstate = GS_PLAY;
        for (auto& q : PASS) q = Passenger();
        spawnT = 999;
        Player& p = P[0];
        p.score = 123;
        std::string idle = hudMoneyText(p);
        PASS[0].state = PS_RIDE; PASS[0].dest = '2'; PASS[0].taxi = 0; p.pass = 0; p.tip = TIP_START;
        std::string start = hudMoneyText(p);
        for (int t = 0; t < 60 * 5; t++) updatePassengers(1 / 60.f);           // 5 s of riding
        std::string later = hudMoneyText(p);
        PASS[0].noTip = true; PASS[0].fareCut += ROUGH_FARE_CUT;               // rough landing
        std::string afterRough = hudMoneyText(p);
        int shown = currentFare(p);
        long before = p.score;
        int k2 = padByChar('2'); placeOnSlot(p, Slot{k2, LV.pads[k2].x + 8}); p.pass = 0; onLanded(0);
        std::string delivered = hudMoneyText(p);
        bool ok = idle == "$123" && start == "+$" + std::to_string(FARE_BASE + (int)TIP_START) &&
                  later == "+$" + std::to_string(FARE_BASE + (int)(TIP_START - 5 * TIP_DECAY)) &&
                  afterRough == "+$" + std::to_string(FARE_BASE - ROUGH_FARE_CUT) &&
                  p.score - before == shown && delivered == "$" + std::to_string(p.score);
        printf("HUD fare: %s -> %s -> %s (5s later) -> %s (rough) -> paid %d -> %s -> %s\n",
               idle.c_str(), start.c_str(), later.c_str(), afterRough.c_str(), shown, delivered.c_str(), ok ? "ok" : "FAIL");
        if (!ok) fails++;
    }
    // 16) exit order: up-passenger carrier first, then everyone else, then the level is done
    {
        for (int k = 0; k < MAXP; k++) P[k] = Player();
        P[0].joined = P[1].joined = true;
        startLevel(0); gstate = GS_PLAY;
        for (auto& q : PASS) q = Passenger();
        spawnT = 999; upSpawnT = 999;
        LV.quotaDone = true; LV.gateAnim = 1.f; rebuildMasks();
        int gx = 0; for (int x = 0; x < MW; x++) if (LV.tile[0][x] == 'G') { gx = x * 8 + 8; break; }   // gate centre-ish
        auto flyUp = [&](int i, int frames) {
            Player& p = P[i];
            p.alive = true; p.landed = false; p.gear = false; p.gearExt = 0; p.fuel = 50; p.airT = 9; p.invuln = 0;
            p.x = (float)gx; p.y = 12; p.vx = 0; p.vy = -20; p.cur = Input(); p.cur.u = true; p.prev = p.cur;
            for (int t = 0; t < frames && p.alive; t++) { updateTaxi(i, 1 / 60.f); checkLevelExit(); }
        };
        P[0].alive = false; P[0].x = 30; P[0].y = 30;
        flyUp(1, 90);                                                    // non-carrier tries first
        bool blocked = P[1].alive && !P[1].exited && P[1].y >= 0;
        P[1].alive = false; P[1].x = 260; P[1].y = 150;                   // park P2 away from the gate
        PASS[0].state = PS_RIDE; PASS[0].up = true; PASS[0].taxi = 0; P[0].pass = 0;
        flyUp(0, 120);                                                   // carrier exits
        P[1].lives = 3;
        bool carrierOut = P[0].exited && LV.upDone && gstate == GS_PLAY;   // P2 still inside -> not done
        flyUp(1, 120);                                                   // now P2 can leave
        bool done = P[1].exited && gstate == GS_DONE;
        printf("exit order: others blocked before carrier %s, carrier out but level waits %s, all out -> level done %s\n",
               blocked ? "ok" : "FAIL", carrierOut ? "ok" : "FAIL", done ? "ok" : "FAIL");
        if (!(blocked && carrierOut && done)) fails++;
    }
    // 17) life stealing in multiplayer
    {
        for (int k = 0; k < MAXP; k++) P[k] = Player();
        for (int k = 0; k < 3; k++) P[k].joined = true;
        startLevel(0); gstate = GS_PLAY;
        P[0].lives = 1; P[1].lives = 3; P[2].lives = 2;
        auto die = [&](int i) { P[i].alive = true; killPlayer(i, nullptr); };
        std::string trace;
        auto snap = [&]() { trace += "[" + std::to_string(P[0].lives) + std::to_string(P[1].lives) + std::to_string(P[2].lives) + "]"; };
        snap();
        die(0); snap();   // takes from P2 (3 lives)   -> 1 2 2
        die(0); snap();   // tie 2/2 -> first (P2)      -> 1 1 2
        die(0); snap();   // only P3 can give           -> 1 1 1
        die(0); snap();   // everyone on last life      -> 0 1 1 (P1 out)
        bool multi = trace == "[132][122][112][111][011]";
        // single player: no stealing
        for (int k = 0; k < MAXP; k++) P[k] = Player();
        P[0].joined = true; startLevel(0); gstate = GS_PLAY; P[0].lives = 1;
        die(0);
        bool single = P[0].lives == 0;
        printf("life stealing: %s %s, single player not affected %s\n", trace.c_str(), multi ? "ok" : "FAIL", single ? "ok" : "FAIL");
        if (!(multi && single)) fails++;
    }
    // 18) floor switches: landing on one toggles the doors without crashing
    {
        int good = 0, tries = 0;
        for (int L = 0; L < NLEVELS; L++) {
            if (LEVELDEFS[L].map.empty()) continue;
            for (int k = 0; k < MAXP; k++) P[k] = Player();
            P[0].joined = true; P[0].lives = 99;
            startLevel(L); gstate = GS_PLAY;
            for (auto& q : PASS) q = Passenger();
            spawnT = 999;
            for (size_t si = 0; si < LV.switches.size(); si++) {
                SDL_Point sw = LV.switches[si];
                bool before = LV.switchOn;
                LV.switchCd = 0;
                Player& p = P[0];
                p = Player(); p.joined = true; p.alive = true; p.lives = 99; p.face = 0; p.gear = true; p.gearExt = 2; p.fuel = 50; p.airT = 9;
                p.x = sw.x - 4.f; p.y = sw.y + 8 - 10 - 6.f; p.vx = 0; p.vy = 15; p.cur = p.prev = Input();
                for (int t = 0; t < 120 && p.alive && !p.landed; t++) { rebuildMasks(); updateTaxi(0, 1 / 60.f); }
                bool ok = p.alive && p.landed && LV.switchOn != before;
                tries++; if (ok) good++;
                if (getenv("SWDBG")) printf("  L%d switch %zu at %d,%d: alive=%d landed=%d toggled=%d\n", L + 1, si, sw.x, sw.y, p.alive, p.landed, LV.switchOn != before);
            }
        }
        printf("floor switches: %d of %d switches toggled by a centred, gentle landing\n", good, tries);
        if (good != tries) fails++;
    }
    printf(fails ? "SELFTEST FAILED (%d)\n" : "SELFTEST PASSED\n", fails);
    return fails ? 1 : 0;
}

int main(int argc, char** argv) {
    // debug / test modes
    int shotLevel = -1, shotFrames = 240, shotPlayers = 5; const char* shotPath = nullptr; bool titleShot = false, iconShot = false, selfTest = false, sceneShot = false, dmgShot = false, liveShot = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--shot") && i + 2 < argc) { shotLevel = atoi(argv[i + 1]) - 1; shotPath = argv[i + 2]; i += 2; headless = true; }
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) shotFrames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--players") && i + 1 < argc) shotPlayers = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--autoplay")) autoplay = true;
        else if (!strcmp(argv[i], "--bot")) botMode = true;
        else if (!strcmp(argv[i], "--selftest")) { selfTest = true; headless = true; }
        else if (!strcmp(argv[i], "--scene-shot") && i + 1 < argc) { sceneShot = true; shotPath = argv[++i]; headless = true; }
        else if (!strcmp(argv[i], "--dmg-shot") && i + 1 < argc) { dmgShot = true; shotPath = argv[++i]; headless = true; }
        else if (!strcmp(argv[i], "--livearea-shot") && i + 1 < argc) { liveShot = true; shotPath = argv[++i]; headless = true; }
        else if (!strcmp(argv[i], "--title-shot") && i + 1 < argc) { titleShot = true; shotPath = argv[++i]; headless = true; }
        else if (!strcmp(argv[i], "--icon-shot") && i + 1 < argc) { iconShot = true; shotPath = argv[++i]; headless = true; }
    }
#ifdef __SWITCH__
    romfsInit();
#endif
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    if (TTF_Init() != 0) { fprintf(stderr, "TTF_Init: %s\n", TTF_GetError()); return 1; }
#if defined(__SWITCH__)
    assetDir = "romfs:/";
#elif defined(__vita__)
    assetDir = "app0:/assets/";          // packed into the .vpk
#else
    char* base = SDL_GetBasePath();
    std::string baseDir = base ? base : "./";
    SDL_free(base);
    assetDir = baseDir + "assets/";
    {
        SDL_RWops* t = SDL_RWFromFile((assetDir + "PressStart2P-Regular.ttf").c_str(), "rb");
        if (!t) assetDir = "assets/"; else SDL_RWclose(t);
    }
#endif
    std::string fontPath = assetDir + "PressStart2P-Regular.ttf";
    font8 = TTF_OpenFont(fontPath.c_str(), 8);
    font16 = TTF_OpenFont(fontPath.c_str(), 16);
    font24 = TTF_OpenFont(fontPath.c_str(), 24);
    if (!font8) fprintf(stderr, "warning: font not found (%s)\n", fontPath.c_str());
#if !defined(__SWITCH__) && !defined(__vita__)
    SDL_GameControllerAddMappingsFromFile((assetDir + "gamecontrollerdb.txt").c_str());
#endif

    buildLevels(LEVELDEFS);
    buildTaxiMasks();
    buildSubMasks();
    titleBg.resize(SW * SH);
    genBackground(THEMES[0], titleBg.data(), SW, SH, 4242);

    if (headless) {
        if (selfTest) return runSelfTest();
        if (sceneShot) { renderScene(); saveShot(shotPath); return 0; }
        if (dmgShot) { renderDmgBackground(); saveShot(shotPath); return 0; }
        if (liveShot) {
            // PS Vita LiveArea background art: 420x250, scaled 2x to 840x500 by the build tools
            std::vector<uint32_t> bg(420 * 250);
            genBackground(THEMES[0], bg.data(), 420, 250, 77);
            SDL_Surface* sfc = SDL_CreateRGBSurfaceWithFormatFrom(bg.data(), 420, 250, 32, 420 * 4, SDL_PIXELFORMAT_ARGB8888);
            SDL_SaveBMP(sfc, shotPath); SDL_FreeSurface(sfc);
            return 0;
        }
        if (iconShot) {
            // 64x64 icon scene (no text) in the top-left of the framebuffer, scaled 4x to 256x256
            // for the Switch Homebrew Menu: a taxi flying in toward a pad where a passenger waves.
            // Sprites are drawn at double size so they read well as a small icon.
            const int N = 64;
            std::vector<uint32_t> bg(N * N);
            genBackground(THEMES[0], bg.data(), N, N, 7);
            std::vector<uint32_t> icon(bg);
            auto put = [&](int x, int y, uint32_t c) { if (x >= 0 && y >= 0 && x < N && y < N) icon[y * N + x] = c; };
            // draw a sprite into the scratch area of fb, then copy it into the icon at 2x
            const uint32_t KEY = 0xFF123456u;
            auto sprite2x = [&](auto draw, int w, int h, int dx, int dy) {
                for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) fb[(100 + y) * SW + 100 + x] = KEY;
                draw(100, 100);
                for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
                    uint32_t c = fb[(100 + y) * SW + 100 + x];
                    if (c == KEY) continue;
                    for (int k = 0; k < 4; k++) put(dx + x * 2 + (k & 1), dy + y * 2 + (k >> 1), c);
                }
            };
            const uint32_t* m = THEMES[0].metal;
            for (int y = 52; y < N; y++)          // metal ledge under the pad
                for (int x = 26; x < N; x++) {
                    int lx = (x - 26) & 15, ly = (y - 52) & 15;
                    uint32_t c = m[2];
                    if (lx == 0 || ly == 0) c = m[3];
                    if (lx == 15 || ly == 15) c = m[1];
                    if (x == 26 || y == 52) c = m[4];
                    if ((lx == 3 || lx == 12) && ly == 4) c = m[4];
                    if (x == 26 && y == 52) continue;
                    put(x, y, c);
                }
            for (int y = 44; y < 52; y++)          // landing pad plate (no label)
                for (int x = 28; x < N; x++) {
                    uint32_t c = y == 44 ? rgb(232, 232, 214) : (y == 51 ? rgb(24, 24, 30) : (y == 45 ? rgb(78, 82, 100) : rgb(46, 50, 64)));
                    if (x <= 29) c = rgb(124, 124, 140);
                    if (y >= 46 && y <= 49 && x > 29 && (((x + y) / 3) & 1)) c = rgb(250, 200, 30);
                    put(x, y, c);
                }
            for (int k = 0; k < 2; k++) for (int j = 0; j < 2; j++) { put(31 + k, 42 + j, rgb(255, 255, 255)); put(60 + k, 42 + j, rgb(255, 255, 255)); }
            Passenger q; q.state = PS_WAIT; q.anim = 0.1f; q.gender = 1; q.style = 4; q.dir = -1;
            q.skin = SKIN_TONES[2]; q.hair = HAIR_COLS[4]; q.top = TOP_COLS[5]; q.bottom = SKIRT_COLS[0];
            sprite2x([&](int x, int y) { drawPassenger(q, x, y); }, 6, 10, 46, 24);
            P[0] = Player(); P[0].face = 0; P[0].gearExt = 2; P[0].gear = true; P[0].fuel = 50; P[0].thU = false; tglobal = 0.25f;
            sprite2x([&](int x, int y) { blitTaxi(0, x, y); }, 16, 10, 2, 6);
            // main engine flame under the chassis (sprite row 7, columns 7-8 -> icon 2x)
            const uint32_t fl[5] = {rgb(255, 255, 230), rgb(255, 230, 120), rgb(255, 190, 50), rgb(255, 120, 20), rgb(210, 50, 10)};
            for (int k = 0; k < 5; k++)
                for (int j = 0; j < 2; j++) {
                    put(16 + 0, 20 + k * 2 + j, fl[k]); put(17, 20 + k * 2 + j, fl[k]);
                    if (k < 4) { put(18, 20 + k * 2 + j, fl[k + 1]); put(19, 20 + k * 2 + j, fl[k + 1]); }
                }
            for (int k = 0; k < 4; k++) { put(15 + (k & 1) * 4, 31 + k * 2, mix(icon[(31 + k * 2) * N + 15], rgb(255, 140, 40), 0.6f - k * 0.12f)); }
            for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) fb[y * SW + x] = icon[y * N + x];
            saveShot(shotPath); return 0;
        }
        if (titleShot) {
            for (int i = 0; i < 3; i++) { P[i].joined = true; P[i].src = SRC_KEYA + (i % 2); }
            tglobal = 1.2f;
            drawTitle(); saveShot(shotPath); return 0;
        }
        shotPlayers = std::clamp(shotPlayers, 1, MAXP);
        for (int i = 0; i < shotPlayers; i++) { P[i] = Player(); P[i].joined = true; P[i].src = SRC_NONE; }
        int levels0 = shotLevel, levels1 = shotLevel;
        if (shotLevel < 0) { levels0 = 0; levels1 = NLEVELS - 1; }
        int deaths = 0, delivered = 0;
        for (int L = levels0; L <= levels1; L++) {
            for (auto& p : P) { p.lives = 99; p.score = 0; }
            startLevel_ = L;
            startLevel(L);
            gstate = GS_PLAY;
            for (int f = 0; f < shotFrames; f++) {
                pollInputs();
                update(1.f / 60.f);
                if (botMode && getenv("BOTDBG") && f % 60 == 0) {
                    int w = 0; for (auto& q : PASS) if (q.state == PS_WAIT) w++;
                    printf("  t=%3d x=%6.1f y=%6.1f vx=%6.1f vy=%6.1f land=%d pad=%d pass=%d alive=%d fuel=%.0f wait=%d del=%d\n", f / 60, P[0].x, P[0].y, P[0].vx, P[0].vy, P[0].landed, P[0].landPad, P[0].pass, P[0].alive, P[0].fuel, w, LV.delivered);
                }
                if (gstate != GS_PLAY) { printf("  level state %d after %.1fs (upCarrier=%d)\n", (int)gstate, f / 60.f, upCarrier); break; }
            }
            int d = 0; for (auto& p : P) if (p.joined) d += 99 - p.lives;
            deaths += d; delivered += LV.delivered;
            render();
            if (shotLevel >= 0) saveShot(shotPath);
            printf("level %2d %-16s ok  deaths=%d delivered=%d parts=%zu money=$%ld fuel=%.0f\n", L + 1, LV.def->name.c_str(), d, LV.delivered, parts.size(), P[0].score, P[0].fuel);
        }
        printf("total deaths %d delivered %d\n", deaths, delivered);
        return 0;
    }

    audio.init(assetDir);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
#if defined(__SWITCH__)
    SDL_Window* win = SDL_CreateWindow("Space Taxi Mayhem", 0, 0, 1280, 720, 0);   // always fullscreen 720p
#elif defined(__vita__)
    SDL_Window* win = SDL_CreateWindow("Space Taxi Mayhem", 0, 0, 960, 544, 0);    // Vita screen
#else
    SDL_Window* win = SDL_CreateWindow("Space Taxi Mayhem", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SW * 3, SH * 3, SDL_WINDOW_RESIZABLE);
#endif
    if (!win) { fprintf(stderr, "window: %s\n", SDL_GetError()); return 1; }
#if defined(__vita__)
    // use SDL's native GXM renderer (precompiled shaders) rather than OpenGL ES via vitaGL,
    // which would need the runtime shader compiler (libshacccg.suprx) on the console
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "VITA gxm");
#endif
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
    SDL_RenderSetLogicalSize(ren, SW, SH);
#if defined(__vita__) || defined(SHARP_BILINEAR)
    // fill the Vita's 544 px height with the 4:3 image (725x544, black bars left/right)
    SDL_RenderSetIntegerScale(ren, SDL_FALSE);
#else
    SDL_RenderSetIntegerScale(ren, SDL_TRUE);    // crisp pixels: 3x = 960x720 on Switch
#endif
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SW, SH);
#if defined(__vita__) || defined(SHARP_BILINEAR)
    // "sharp bilinear": the frame is first scaled an exact 2x with nearest-neighbour into
    // a 640x480 render target (crisp square pixels), and only that is smoothed (bilinear)
    // for the last 1.13x up to 725x544, so just the pixel edges are slightly soft.
    SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);
    SDL_Texture* tex2x = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, SW * 2, SH * 2);
    if (tex2x) SDL_SetTextureScaleMode(tex2x, SDL_ScaleModeLinear);
    else SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);   // no render targets: plain bilinear
#endif
    for (int i = 0; i < SDL_NumJoysticks(); i++) openController(i);

    bool running = true;
    Uint64 last = SDL_GetPerformanceCounter();
    double acc = 0;
    const double DT = 1.0 / 60.0;
    bool fullscreen = false;
    while (running) {
#ifdef __SWITCH__
        if (!appletMainLoop()) break;      // HOME menu -> close
#endif
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            else if (e.type == SDL_CONTROLLERDEVICEADDED) {
                openController(e.cdevice.which);
            } else if (e.type == SDL_CONTROLLERDEVICEREMOVED) {
                for (size_t i = 0; i < controllers.size(); i++)
                    if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controllers[i])) == e.cdevice.which) {
                        SDL_GameControllerClose(controllers[i]); controllers.erase(controllers.begin() + i); break;
                    }
            }
            if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                if (e.key.keysym.sym == SDLK_F11 || (e.key.keysym.sym == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT))) {
                    fullscreen = !fullscreen;
                    SDL_SetWindowFullscreen(win, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                }
                if (e.key.keysym.sym == SDLK_ESCAPE && gstate == GS_TITLE) running = false;
                if (e.key.keysym.sym == SDLK_ESCAPE && gstate == GS_PLAY) { gstate = GS_PAUSE; audio.silenceThrust(); }
            }
        }
        Uint64 now = SDL_GetPerformanceCounter();
        acc += (double)(now - last) / SDL_GetPerformanceFrequency();
        last = now;
        if (acc > 0.25) acc = 0.25;
        while (acc >= DT) { pollInputs(); update((float)DT); acc -= DT; }
        render();
        SDL_UpdateTexture(tex, nullptr, fb, SW * 4);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_Rect dst{0, 0, SW, SH};
        if (shakeT > 0 && gstate != GS_TITLE) { dst.x = rng.i(5) - 2; dst.y = rng.i(5) - 2; }
#if defined(__vita__) || defined(SHARP_BILINEAR)
        if (tex2x) {
            SDL_SetRenderTarget(ren, tex2x);
            SDL_RenderCopy(ren, tex, nullptr, nullptr);      // exact 2x, nearest
            SDL_SetRenderTarget(ren, nullptr);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
            SDL_RenderClear(ren);
            SDL_RenderCopy(ren, tex2x, nullptr, &dst);      // remaining 1.13x, bilinear
        } else
#endif
        SDL_RenderCopy(ren, tex, nullptr, &dst);
        SDL_RenderPresent(ren);
    }
    audio.shutdown();
    for (auto* gc : controllers) SDL_GameControllerClose(gc);
#if defined(__vita__) || defined(SHARP_BILINEAR)
    if (tex2x) SDL_DestroyTexture(tex2x);
#endif
    SDL_DestroyTexture(tex); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win);
    TTF_Quit(); SDL_Quit();
#ifdef __SWITCH__
    romfsExit();
#endif
    return 0;
}
