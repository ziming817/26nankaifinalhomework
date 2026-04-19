/**
 * Pixel Escape — Win32 + GDI，无第三方库（避免 CMake 拉取 raylib 需网络）
 */

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>

#include <gdiplus.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr int kScreenW = 960;
constexpr int kScreenH = 540;

constexpr float GRAVITY = 1800.0f;
constexpr float MOVE_SPEED = 260.0f;
constexpr float JUMP_VELOCITY = -620.0f;
constexpr int TILE = 32;

struct Vec2 {
    float x = 0, y = 0;
};

enum class BodyShape { Normal, Tall, Short, Fat, Thin };

struct RectF {
    float x, y, w, h;
};

inline bool AabbOverlap(const RectF& a, const RectF& b) {
    return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}

void BodySize(BodyShape s, float& w, float& h) {
    switch (s) {
        case BodyShape::Tall:
            w = 22;
            h = 52;
            break;
        case BodyShape::Short:
            w = 22;
            h = 18;
            break;
        case BodyShape::Fat:
            w = 42;
            h = 28;
            break;
        case BodyShape::Thin:
            w = 14;
            h = 36;
            break;
        case BodyShape::Normal:
        default:
            w = 24;
            h = 32;
            break;
    }
}

enum class CellType : int {
    Empty = 0,
    Ground = 1,
    Spike = 2,
    PitMarker = 3,
    ButtonTall = 4,
    ButtonShort = 5,
    ButtonFat = 6,
    ButtonThin = 7,
    DoorReal = 8,
    DoorFake = 9,
};

struct Level {
    int cols = 0;
    int rows = 0;
    std::vector<CellType> grid;
    Vec2 spawnTile{1, 1};
};

CellType CharToCell(char c) {
    switch (c) {
        case '#':
            return CellType::Ground;
        case '^':
            return CellType::Spike;
        case '.':
            return CellType::PitMarker;
        case 'T':
            return CellType::ButtonTall;
        case 'S':
            return CellType::ButtonShort;
        case 'F':
            return CellType::ButtonFat;
        case 'I':
            return CellType::ButtonThin;
        case 'R':
            return CellType::DoorReal;
        case 'X':
            return CellType::DoorFake;
        case 'P':
            return CellType::Empty;
        case ' ':
            return CellType::Empty;
        default:
            return CellType::Empty;
    }
}

Level MakeLevelFromString(const char* ascii) {
    Level L;
    std::vector<std::string> lines;
    const char* p = ascii;
    std::string cur;
    while (*p) {
        if (*p == '\n') {
            if (!cur.empty()) {
                lines.push_back(cur);
                cur.clear();
            }
        } else if (*p != '\r') {
            cur.push_back(*p);
        }
        ++p;
    }
    if (!cur.empty()) lines.push_back(cur);
    L.rows = static_cast<int>(lines.size());
    L.cols = 0;
    for (auto& ln : lines) L.cols = (std::max)(L.cols, static_cast<int>(ln.size()));
    L.grid.assign(L.cols * L.rows, CellType::Empty);
    for (int y = 0; y < L.rows; ++y) {
        const std::string& ln = lines[y];
        for (int x = 0; x < L.cols; ++x) {
            char ch = (x < static_cast<int>(ln.size())) ? ln[x] : ' ';
            if (ch == 'P') {
                L.spawnTile = {(float)x, (float)y};
                L.grid[y * L.cols + x] = CellType::Empty;
            } else {
                L.grid[y * L.cols + x] = CharToCell(ch);
            }
        }
    }
    return L;
}

// 单层平地：左为真门 R（出生居中时镜头外左侧），中间出生 P，右可见尖刺 ^，再右为假门 X；无跳台仅平地
const char* kLevel1 = R"(
####################################################################
#..................................................................#
#..................................................................#
#..................................................................#
#.................................P................................#
#R#########################################^############X###########
#..................................................................#
#..................................................................#
#..................................................................#
#..................................................................#
#..................................................................#
#..................................................................#
####################################################################
)";

const char* kLevel2 = R"(
##############################################################################
#............................................................................#
#..P.........................................................................#
#..###.......................................................................#
#......^...^.................................................................#
#......#####...............................X.................................#
#.....................................##########.............................#
#..S..............F..........................................................#
#..####...........###..................................R.....................#
#.......^........................^...........................................#
#.......###########..............###.........................................#
#............................................................................#
#............................................................................#
##############################################################################
)";

const char* kLevel3 = R"(
################################################################################################
#..............................................................................................#
#..............................................................................................#
#..P...........^..^............................................................................#
#..###.........####............................................................................#
#.......^..................I...................................................................#
#.......###............^...###..............X..................................................#
#..................^...........^............###......................R.......................#
#..T...............###.........###....................................###......................#
#..###.........................................................................................#
#..............^...........^............^......................................................#
#..............###.........###..........###....................................................#
#..F...........................................................................................#
#..###.........................................................................................#
#..............................................................................................#
#..............................................................................................#
################################################################################################
)";

enum class GameState { Playing, SettingsOverlay, Won, Lost };

struct Game {
    int currentLevelIndex = 0;
    Level levels[3];
    GameState state = GameState::Playing;

    Vec2 playerPos{0, 0};
    Vec2 playerVel{0, 0};
    BodyShape body = BodyShape::Normal;
    bool facingRight = true;
    bool onGround = false;
};

RectF PlayerHitbox(const Game& g) {
    float w, h;
    BodySize(g.body, w, h);
    return {g.playerPos.x - w * 0.5f, g.playerPos.y - h, w, h};
}

void BuildSolidsAndHazards(const Level& L, std::vector<RectF>& solids, std::vector<RectF>& spikes,
                           std::vector<RectF>& doorReal, std::vector<RectF>& doorFake,
                           std::vector<RectF>& btnTall, std::vector<RectF>& btnShort,
                           std::vector<RectF>& btnFat, std::vector<RectF>& btnThin) {
    solids.clear();
    spikes.clear();
    doorReal.clear();
    doorFake.clear();
    btnTall.clear();
    btnShort.clear();
    btnFat.clear();
    btnThin.clear();

    for (int y = 0; y < L.rows; ++y) {
        for (int x = 0; x < L.cols; ++x) {
            CellType c = L.grid[y * L.cols + x];
            float px = x * TILE;
            float py = y * TILE;
            RectF cell{px, py, (float)TILE, (float)TILE};
            switch (c) {
                case CellType::Ground:
                    solids.push_back(cell);
                    break;
                case CellType::Spike:
                    // 碰撞与三角形主体大致一致，避免整格矩形比可见尖刺“大一圈”
                    spikes.push_back(
                        {px + TILE * 0.05f, py + TILE * 0.12f, TILE * 0.9f, TILE * 0.88f});
                    break;
                case CellType::DoorReal:
                    doorReal.push_back(cell);
                    break;
                case CellType::DoorFake:
                    doorFake.push_back(cell);
                    break;
                case CellType::ButtonTall:
                    btnTall.push_back(cell);
                    break;
                case CellType::ButtonShort:
                    btnShort.push_back(cell);
                    break;
                case CellType::ButtonFat:
                    btnFat.push_back(cell);
                    break;
                case CellType::ButtonThin:
                    btnThin.push_back(cell);
                    break;
                case CellType::PitMarker:
                case CellType::Empty:
                default:
                    break;
            }
        }
    }
}

bool IsOverButton(const RectF& player, const RectF& btn) {
    RectF feet{player.x + player.w * 0.2f, player.y + player.h - 2.0f, player.w * 0.6f, 4.0f};
    return AabbOverlap(feet, btn);
}

void TryApplyButtons(Game& g, const Level& L) {
    std::vector<RectF> solids, spikes, dr, df, bt, bs, bf, bi;
    BuildSolidsAndHazards(L, solids, spikes, dr, df, bt, bs, bf, bi);
    RectF hb = PlayerHitbox(g);

    auto tryOne = [&](const std::vector<RectF>& buttons, BodyShape shape) {
        for (const auto& b : buttons) {
            if (IsOverButton(hb, b)) {
                g.body = shape;
                return true;
            }
        }
        return false;
    };
    if (tryOne(bt, BodyShape::Tall)) return;
    if (tryOne(bs, BodyShape::Short)) return;
    if (tryOne(bf, BodyShape::Fat)) return;
    if (tryOne(bi, BodyShape::Thin)) return;
}

void PhysicsStep(Game& g, float dt, const Level& L, bool keyA, bool keyD, bool keySpaceEdge) {
    std::vector<RectF> solids, spikes, doorReal, doorFake, bt, bs, bf, bi;
    BuildSolidsAndHazards(L, solids, spikes, doorReal, doorFake, bt, bs, bf, bi);

    if (keyA) {
        g.playerVel.x = -MOVE_SPEED;
        g.facingRight = false;
    } else if (keyD) {
        g.playerVel.x = MOVE_SPEED;
        g.facingRight = true;
    } else {
        g.playerVel.x = 0;
    }

    if (g.onGround && keySpaceEdge) {
        g.playerVel.y = JUMP_VELOCITY;
        g.onGround = false;
    }

    g.playerVel.y += GRAVITY * dt;

    g.playerPos.x += g.playerVel.x * dt;
    for (int pass = 0; pass < 3; ++pass) {
        RectF hb = PlayerHitbox(g);
        for (const auto& s : solids) {
            if (AabbOverlap(hb, s)) {
                float half = hb.w * 0.5f;
                float penLeft = (hb.x + hb.w) - s.x;
                float penRight = (s.x + s.w) - hb.x;
                if (penLeft > 0 && penRight > 0) {
                    if (penLeft < penRight)
                        g.playerPos.x = s.x - half - 0.02f;
                    else
                        g.playerPos.x = s.x + s.w + half + 0.02f;
                }
            }
        }
    }

    g.playerPos.y += g.playerVel.y * dt;
    g.onGround = false;
    for (int pass = 0; pass < 3; ++pass) {
        RectF hb = PlayerHitbox(g);
        for (const auto& s : solids) {
            if (AabbOverlap(hb, s)) {
                if (g.playerVel.y > 0) {
                    g.playerPos.y = s.y - 0.01f;
                    g.playerVel.y = 0;
                    g.onGround = true;
                } else if (g.playerVel.y < 0) {
                    g.playerPos.y = s.y + s.h + hb.h + 0.01f;
                    g.playerVel.y = 0;
                }
            }
        }
    }

    TryApplyButtons(g, L);

    RectF hb = PlayerHitbox(g);
    for (const auto& sp : spikes) {
        if (AabbOverlap(hb, sp)) {
            g.state = GameState::Lost;
            return;
        }
    }

    for (const auto& d : doorReal) {
        if (AabbOverlap(hb, d)) {
            g.state = GameState::Won;
            return;
        }
    }
    for (const auto& d : doorFake) {
        if (AabbOverlap(hb, d)) {
            g.state = GameState::Lost;
            return;
        }
    }

    float levelH = L.rows * TILE;
    float levelW = L.cols * TILE;
    if (g.playerPos.y > levelH + 100 || g.playerPos.x < -200 || g.playerPos.x > levelW + 200) {
        g.state = GameState::Lost;
    }
}

// P 正下方第一格 # 的顶面高度（脚底 y）；避免 P 列下方是空点导致出生在平台下方空档里
float FeetYOnGroundBelow(const Level& L, int gx, int py) {
    if (gx < 0 || gx >= L.cols) return (float)((py + 1) * TILE);
    for (int y = py + 1; y < L.rows; ++y) {
        if (L.grid[y * L.cols + gx] == CellType::Ground) return (float)(y * TILE);
    }
    return (float)((py + 1) * TILE);
}

void ResetLevel(Game& g) {
    g.levels[g.currentLevelIndex] =
        g.currentLevelIndex == 0 ? MakeLevelFromString(kLevel1)
        : g.currentLevelIndex == 1 ? MakeLevelFromString(kLevel2)
                                   : MakeLevelFromString(kLevel3);
    const Level& L = g.levels[g.currentLevelIndex];
    int gx = (int)floorf(L.spawnTile.x + 0.5f);
    int py = (int)floorf(L.spawnTile.y + 0.5f);
    g.playerPos = {L.spawnTile.x * TILE + TILE * 0.5f, FeetYOnGroundBelow(L, gx, py)};
    g.playerVel = {0, 0};
    g.body = BodyShape::Normal;
    g.state = GameState::Playing;
}

// ---------- GDI 绘制（世界坐标 -> 屏幕，摄像机跟随玩家） ----------

void WorldToScreen(float wx, float wy, float cx, float cy, int* sx, int* sy) {
    *sx = (int)floorf(wx - cx + kScreenW * 0.5f);
    *sy = (int)floorf(wy - cy + kScreenH * 0.55f);
}

void FillWorldRect(HDC hdc, float wx, float wy, float ww, float wh, float cx, float cy, COLORREF col) {
    int x0, y0, x1, y1;
    WorldToScreen(wx, wy, cx, cy, &x0, &y0);
    WorldToScreen(wx + ww, wy + wh, cx, cy, &x1, &y1);
    RECT r{(std::min)(x0, x1), (std::min)(y0, y1), (std::max)(x0, x1), (std::max)(y0, y1)};
    HBRUSH br = CreateSolidBrush(col);
    FillRect(hdc, &r, br);
    DeleteObject(br);
}

void FrameWorldRect(HDC hdc, float wx, float wy, float ww, float wh, float cx, float cy, COLORREF col) {
    int x0, y0, x1, y1;
    WorldToScreen(wx, wy, cx, cy, &x0, &y0);
    WorldToScreen(wx + ww, wy + wh, cx, cy, &x1, &y1);
    RECT r{(std::min)(x0, x1), (std::min)(y0, y1), (std::max)(x0, x1), (std::max)(y0, y1)};
    HBRUSH br = CreateSolidBrush(col);
    FrameRect(hdc, &r, br);
    DeleteObject(br);
}

void FillWorldTriangle(HDC hdc, float ax, float ay, float bx, float by, float cxp, float cyp, float cx,
                       float cy, COLORREF col) {
    int sx0, sy0, sx1, sy1, sx2, sy2;
    WorldToScreen(ax, ay, cx, cy, &sx0, &sy0);
    WorldToScreen(bx, by, cx, cy, &sx1, &sy1);
    WorldToScreen(cxp, cyp, cx, cy, &sx2, &sy2);
    POINT pts[3] = {{sx0, sy0}, {sx1, sy1}, {sx2, sy2}};
    HPEN np = CreatePen(PS_NULL, 0, 0);
    HPEN op = (HPEN)SelectObject(hdc, np);
    HBRUSH br = CreateSolidBrush(col);
    HBRUSH ob = (HBRUSH)SelectObject(hdc, br);
    Polygon(hdc, pts, 3);
    SelectObject(hdc, ob);
    SelectObject(hdc, op);
    DeleteObject(br);
    DeleteObject(np);
}

void OutlineWorldTriangle(HDC hdc, float ax, float ay, float bx, float by, float cxp, float cyp, float cx,
                          float cy, COLORREF outline) {
    int sx0, sy0, sx1, sy1, sx2, sy2;
    WorldToScreen(ax, ay, cx, cy, &sx0, &sy0);
    WorldToScreen(bx, by, cx, cy, &sx1, &sy1);
    WorldToScreen(cxp, cyp, cx, cy, &sx2, &sy2);
    POINT pts[4] = {{sx0, sy0}, {sx1, sy1}, {sx2, sy2}, {sx0, sy0}};
    HPEN pen = CreatePen(PS_SOLID, 2, outline);
    HPEN old = (HPEN)SelectObject(hdc, pen);
    Polyline(hdc, pts, 4);
    SelectObject(hdc, old);
    DeleteObject(pen);
}

void FillWorldCircle(HDC hdc, float wx, float wy, float radius, float cx, float cy, COLORREF col) {
    int sx, sy;
    WorldToScreen(wx - radius, wy - radius, cx, cy, &sx, &sy);
    int sx2, sy2;
    WorldToScreen(wx + radius, wy + radius, cx, cy, &sx2, &sy2);
    HBRUSH br = CreateSolidBrush(col);
    HBRUSH old = (HBRUSH)SelectObject(hdc, br);
    Ellipse(hdc, sx, sy, sx2, sy2);
    SelectObject(hdc, old);
    DeleteObject(br);
}

void DrawUtf8(HDC hdc, int x, int y, int heightPx, COLORREF c, const char* utf8) {
    int nw = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (nw <= 0) return;
    std::wstring w(nw, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w.data(), nw);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, c);
    HFONT f = CreateFontW(-heightPx, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                          CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    HFONT old = (HFONT)SelectObject(hdc, f);
    TextOutW(hdc, x, y, w.c_str(), (int)wcslen(w.c_str()));
    SelectObject(hdc, old);
    DeleteObject(f);
}

void DrawPixelDude(HDC hdc, float footX, float footY, BodyShape shape, bool facingRight, float cx, float cy,
                   COLORREF shirt) {
    (void)facingRight;
    float w, h;
    BodySize(shape, w, h);
    float ox = footX - w * 0.5f;
    float oy = footY - h;

    auto block = [&](float bx, float by, float bw, float bh, COLORREF c) {
        FillWorldRect(hdc, ox + bx, oy + by, bw, bh, cx, cy, c);
    };

    COLORREF skin = RGB(235, 205, 185);
    COLORREF dark = RGB((GetRValue(shirt) * 3) / 4, (GetGValue(shirt) * 3) / 4, (GetBValue(shirt) * 3) / 4);

    switch (shape) {
        case BodyShape::Tall:
            block(w * 0.35f, 0, w * 0.3f, h * 0.18f, skin);
            block(w * 0.25f, h * 0.18f, w * 0.5f, h * 0.55f, shirt);
            block(w * 0.3f, h * 0.73f, w * 0.18f, h * 0.27f, dark);
            block(w * 0.52f, h * 0.73f, w * 0.18f, h * 0.27f, dark);
            break;
        case BodyShape::Short:
            block(w * 0.25f, 0, w * 0.5f, h * 0.35f, skin);
            block(w * 0.15f, h * 0.35f, w * 0.7f, h * 0.4f, shirt);
            block(w * 0.2f, h * 0.75f, w * 0.22f, h * 0.25f, dark);
            block(w * 0.55f, h * 0.75f, w * 0.22f, h * 0.25f, dark);
            break;
        case BodyShape::Fat:
            block(w * 0.35f, 0, w * 0.3f, h * 0.25f, skin);
            block(w * 0.1f, h * 0.25f, w * 0.8f, h * 0.45f, shirt);
            block(w * 0.15f, h * 0.7f, w * 0.25f, h * 0.3f, dark);
            block(w * 0.55f, h * 0.7f, w * 0.25f, h * 0.3f, dark);
            break;
        case BodyShape::Thin:
            block(w * 0.35f, 0, w * 0.3f, h * 0.22f, skin);
            block(w * 0.38f, h * 0.22f, w * 0.24f, h * 0.5f, shirt);
            block(w * 0.32f, h * 0.72f, w * 0.16f, h * 0.28f, dark);
            block(w * 0.52f, h * 0.72f, w * 0.16f, h * 0.28f, dark);
            break;
        case BodyShape::Normal:
        default:
            block(w * 0.3f, 0, w * 0.4f, h * 0.22f, skin);
            block(w * 0.22f, h * 0.22f, w * 0.56f, h * 0.45f, shirt);
            block(w * 0.28f, h * 0.67f, w * 0.2f, h * 0.33f, dark);
            block(w * 0.52f, h * 0.67f, w * 0.2f, h * 0.33f, dark);
            break;
    }
}

void DrawLevel(HDC hdc, const Level& L, float cx, float cy) {
    // 先画地形/门/按钮，尖刺最后画，避免被下方平台像素盖住
    for (int pass = 0; pass < 2; ++pass) {
        for (int y = 0; y < L.rows; ++y) {
            for (int x = 0; x < L.cols; ++x) {
                CellType c = L.grid[y * L.cols + x];
                if (pass == 0 && c == CellType::Spike) continue;
                if (pass == 1 && c != CellType::Spike) continue;

                float px = x * TILE;
                float py = y * TILE;
                switch (c) {
                case CellType::Ground:
                    FillWorldRect(hdc, px, py, (float)TILE, (float)TILE, cx, cy, RGB(55, 60, 70));
                    FrameWorldRect(hdc, px, py, (float)TILE, (float)TILE, cx, cy, RGB(90, 95, 105));
                    break;
                case CellType::Spike:
                    FillWorldTriangle(hdc, px + TILE * 0.5f, py, px + TILE, py + TILE, px, py + TILE, cx, cy,
                                      RGB(255, 85, 95));
                    OutlineWorldTriangle(hdc, px + TILE * 0.5f, py, px + TILE, py + TILE, px, py + TILE, cx, cy,
                                         RGB(255, 255, 220));
                    break;
                case CellType::DoorReal:
                    FillWorldRect(hdc, px + 4, py, TILE - 8, TILE, cx, cy, RGB(70, 180, 90));
                    FillWorldRect(hdc, px + 10, py + 8, TILE - 20, TILE - 16, cx, cy, RGB(40, 90, 50));
                    FillWorldCircle(hdc, px + TILE - 12, py + TILE * 0.5f, 3.0f, cx, cy, RGB(240, 220, 80));
                    break;
                case CellType::DoorFake:
                    FillWorldRect(hdc, px + 4, py, TILE - 8, TILE, cx, cy, RGB(70, 180, 90));
                    FillWorldRect(hdc, px + 10, py + 8, TILE - 20, TILE - 16, cx, cy, RGB(40, 90, 50));
                    FillWorldCircle(hdc, px + TILE - 12, py + TILE * 0.5f, 3.0f, cx, cy, RGB(240, 220, 80));
                    break;
                case CellType::ButtonTall: {
                    FillWorldRect(hdc, px + 2, py + TILE - 10, TILE - 4, 8, cx, cy, RGB(120, 140, 200));
                    int tx, ty;
                    WorldToScreen(px + 6, py + 4, cx, cy, &tx, &ty);
                    DrawUtf8(hdc, tx, ty, 14, RGB(200, 200, 200), u8"高");
                    break;
                }
                case CellType::ButtonShort: {
                    FillWorldRect(hdc, px + 2, py + TILE - 10, TILE - 4, 8, cx, cy, RGB(200, 160, 120));
                    int tx, ty;
                    WorldToScreen(px + 6, py + 4, cx, cy, &tx, &ty);
                    DrawUtf8(hdc, tx, ty, 14, RGB(200, 200, 200), u8"矮");
                    break;
                }
                case CellType::ButtonFat: {
                    FillWorldRect(hdc, px + 2, py + TILE - 10, TILE - 4, 8, cx, cy, RGB(200, 120, 160));
                    int tx, ty;
                    WorldToScreen(px + 6, py + 4, cx, cy, &tx, &ty);
                    DrawUtf8(hdc, tx, ty, 14, RGB(200, 200, 200), u8"胖");
                    break;
                }
                case CellType::ButtonThin: {
                    FillWorldRect(hdc, px + 2, py + TILE - 10, TILE - 4, 8, cx, cy, RGB(160, 200, 160));
                    int tx, ty;
                    WorldToScreen(px + 6, py + 4, cx, cy, &tx, &ty);
                    DrawUtf8(hdc, tx, ty, 14, RGB(200, 200, 200), u8"瘦");
                    break;
                }
                default:
                    break;
                }
            }
        }
    }
}

void DrawUi(HDC hdc, const Game& g) {
    const char* shapeName = u8"普通";
    switch (g.body) {
        case BodyShape::Tall:
            shapeName = u8"高";
            break;
        case BodyShape::Short:
            shapeName = u8"矮";
            break;
        case BodyShape::Fat:
            shapeName = u8"胖";
            break;
        case BodyShape::Thin:
            shapeName = u8"瘦";
            break;
        default:
            break;
    }
    char buf[256];
    snprintf(buf, sizeof(buf), u8"第 %d / 3 关   体型：%s   A/D 移动  空格 跳跃  ESC 设置", g.currentLevelIndex + 1,
             shapeName);
    DrawUtf8(hdc, 12, 10, 18, RGB(245, 245, 245), buf);
}

void DrawSettingsOverlay(HDC hdc) {
    int boxW = 420, boxH = 260;
    int bx = (kScreenW - boxW) / 2, by = (kScreenH - boxH) / 2;
    RECT box{bx, by, bx + boxW, by + boxH};
    HBRUSH bg = CreateSolidBrush(RGB(35, 38, 48));
    FillRect(hdc, &box, bg);
    DeleteObject(bg);
    HBRUSH fr = CreateSolidBrush(RGB(128, 128, 128));
    FrameRect(hdc, &box, fr);
    DeleteObject(fr);

    DrawUtf8(hdc, bx + 24, by + 20, 22, RGB(255, 255, 255), u8"设置 / 暂停");
    DrawUtf8(hdc, bx + 24, by + 70, 18, RGB(210, 210, 210), u8"[Enter] 继续游戏");
    DrawUtf8(hdc, bx + 24, by + 100, 18, RGB(210, 210, 210), u8"[R] 重新游玩本关");
    DrawUtf8(hdc, bx + 24, by + 130, 18, RGB(210, 210, 210), u8"[Q] 退出游戏");
}

Game gGame;
HWND gHwnd = nullptr;

constexpr int kEndDlgClientW = 500;
constexpr int kEndDlgClientH = 540;

struct EndDlgInit {
    const wchar_t* windowTitle = nullptr;
    const wchar_t* pngRelative = nullptr;
    const wchar_t* lineText = nullptr;
    DWORD lineArgb = 0;
    DWORD fillArgb = 0;
};

struct EndDlgData {
    Gdiplus::Bitmap* bmp = nullptr;
    wchar_t lineText[64]{};
    DWORD lineArgb = 0;
    DWORD fillArgb = 0;
};

void TrimExeToDir(wchar_t* path) {
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash)
        *(slash + 1) = L'\0';
    else
        path[0] = L'\0';
}

LRESULT CALLBACK EndDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            auto* ini = reinterpret_cast<const EndDlgInit*>(cs->lpCreateParams);
            auto* d = new EndDlgData();
            if (ini) {
                d->lineArgb = ini->lineArgb;
                d->fillArgb = ini->fillArgb;
                if (ini->lineText)
                    wcsncpy_s(d->lineText, ini->lineText, _TRUNCATE);
                wchar_t path[MAX_PATH];
                if (ini->pngRelative && GetModuleFileNameW(nullptr, path, MAX_PATH) > 0) {
                    TrimExeToDir(path);
                    wcscat_s(path, ini->pngRelative);
                    d->bmp = Gdiplus::Bitmap::FromFile(path);
                    if (d->bmp && d->bmp->GetLastStatus() != Gdiplus::Ok) {
                        delete d->bmp;
                        d->bmp = nullptr;
                    }
                }
            }
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(d));

            RECT cr{};
            GetClientRect(hwnd, &cr);
            HINSTANCE hi = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
            CreateWindowW(L"BUTTON", L"确定", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                          (cr.right - 140) / 2, cr.bottom - 48, 140, 36, hwnd, (HMENU)(INT_PTR)1, hi, nullptr);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == 1 && HIWORD(wParam) == BN_CLICKED)
                DestroyWindow(hwnd);
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_RETURN || wParam == VK_ESCAPE)
                DestroyWindow(hwnd);
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY: {
            auto* d = reinterpret_cast<EndDlgData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (d) {
                delete d->bmp;
                delete d;
            }
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT cr{};
            GetClientRect(hwnd, &cr);
            Gdiplus::Graphics g(hdc);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            auto* d = reinterpret_cast<EndDlgData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            Gdiplus::SolidBrush bg(d ? Gdiplus::Color(d->fillArgb) : Gdiplus::Color(255, 250, 250, 252));
            g.FillRectangle(&bg, (INT)cr.left, (INT)cr.top, (INT)(cr.right - cr.left), (INT)(cr.bottom - cr.top));

            const int margin = 20;
            int imgTop = margin;
            int imgMaxW = cr.right - margin * 2;
            int imgMaxH = 380;
            if (d && d->bmp) {
                int iw = (int)d->bmp->GetWidth();
                int ih = (int)d->bmp->GetHeight();
                float sx = (float)imgMaxW / (float)iw;
                float sy = (float)imgMaxH / (float)ih;
                float sc = (sx < sy) ? sx : sy;
                int dw = (int)(iw * sc);
                int dh = (int)(ih * sc);
                int dx = margin + (imgMaxW - dw) / 2;
                int dy = imgTop;
                g.DrawImage(d->bmp, dx, dy, dw, dh);
            } else {
                Gdiplus::SolidBrush ph(Gdiplus::Color(255, 220, 220, 228));
                g.FillRectangle(&ph, margin, imgTop, imgMaxW, imgMaxH);
            }

            Gdiplus::Font fontPrimary(L"Microsoft YaHei UI", 26.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::Font fontFallback(L"Arial", 24.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            const Gdiplus::Font* font =
                (fontPrimary.GetLastStatus() == Gdiplus::Ok) ? &fontPrimary : &fontFallback;
            Gdiplus::StringFormat sf;
            sf.SetAlignment(Gdiplus::StringAlignmentCenter);
            Gdiplus::SolidBrush tb(d ? Gdiplus::Color(d->lineArgb) : Gdiplus::Color(255, 45, 45, 55));
            int textY = imgTop + imgMaxH + 8;
            Gdiplus::RectF tr((Gdiplus::REAL)margin, (Gdiplus::REAL)textY, (Gdiplus::REAL)(cr.right - 2 * margin),
                              40.0f);
            if (d && d->lineText[0])
                g.DrawString(d->lineText, -1, font, tr, &sf, &tb);

            EndPaint(hwnd, &ps);
            return 0;
        }
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void RunFailModal(HWND hDlg, HWND owner) {
    KillTimer(owner, 1);
    EnableWindow(owner, FALSE);
    ShowWindow(hDlg, SW_SHOW);
    UpdateWindow(hDlg);
    SetForegroundWindow(hDlg);

    while (IsWindow(hDlg)) {
        MSG m{};
        int gm = GetMessageW(&m, nullptr, 0, 0);
        if (gm == 0) {
            PostQuitMessage((int)m.wParam);
            break;
        }
        if (gm < 0)
            break;
        HWND th = m.hwnd;
        if (th == hDlg || (th && IsChild(hDlg, th))) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        } else {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }

    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    SetTimer(owner, 1, 16, nullptr);
}

void ShowEndTauntDialog(HWND owner, const EndDlgInit& init) {
    static bool registered = false;
    HINSTANCE hi = (HINSTANCE)GetWindowLongPtrW(owner, GWLP_HINSTANCE);
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = EndDlgProc;
        wc.hInstance = hi;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = L"PixelEndTauntDlg";
        RegisterClassW(&wc);
        registered = true;
    }

    RECT rc{0, 0, kEndDlgClientW, kEndDlgClientH};
    AdjustWindowRectEx(&rc, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    int ww = rc.right - rc.left;
    int wh = rc.bottom - rc.top;

    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    HMONITOR mon = MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST);
    GetMonitorInfoW(mon, &mi);
    int x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - ww) / 2;
    int y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - wh) / 2;

    EndDlgInit initCopy = init;
    HWND hDlg =
        CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE, L"PixelEndTauntDlg",
                        init.windowTitle ? init.windowTitle : L"Pixel Escape", WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y,
                        ww, wh, owner, nullptr, hi, reinterpret_cast<LPVOID>(&initCopy));
    if (!hDlg)
        return;
    RunFailModal(hDlg, owner);
}

void ShowSuccessTauntDialog(HWND owner) {
    EndDlgInit init{};
    init.windowTitle = L"Pixel Escape — 夯爆了";
    init.pngRelative = L"assets\\win_taunt.png";
    init.lineText = L"夯爆了";
    init.lineArgb = Gdiplus::Color(255, 20, 130, 55).GetValue();
    init.fillArgb = Gdiplus::Color(255, 242, 255, 248).GetValue();
    ShowEndTauntDialog(owner, init);
}

void ShowFailureTauntDialog(HWND owner) {
    EndDlgInit init{};
    init.windowTitle = L"Pixel Escape — 拉完了";
    init.pngRelative = L"assets\\fail_taunt.png";
    init.lineText = L"拉完了";
    init.lineArgb = Gdiplus::Color(255, 55, 45, 50).GetValue();
    init.fillArgb = Gdiplus::Color(255, 250, 250, 252).GetValue();
    ShowEndTauntDialog(owner, init);
}

void GameFrame() {
    static LARGE_INTEGER freq{};
    static LARGE_INTEGER prev{};
    static bool timeInit = false;
    static bool prevEsc = false, prevEnter = false, prevR = false, prevN = false, prevQ = false, prevSpace = false;

    if (!timeInit) {
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&prev);
        timeInit = true;
    }
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    float dt = (float)(now.QuadPart - prev.QuadPart) / (float)freq.QuadPart;
    prev = now;
    if (dt > 0.033f) dt = 0.033f;
    if (dt < 0.001f) dt = 0.001f;

    bool keyA = (GetAsyncKeyState('A') & 0x8000) != 0;
    bool keyD = (GetAsyncKeyState('D') & 0x8000) != 0;
    bool spaceDown = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
    bool keySpaceEdge = spaceDown && !prevSpace;
    bool esc = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    bool enter = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
    bool r = (GetAsyncKeyState('R') & 0x8000) != 0;
    bool n = (GetAsyncKeyState('N') & 0x8000) != 0;
    bool q = (GetAsyncKeyState('Q') & 0x8000) != 0;

    if (gGame.state == GameState::Playing) {
        if (esc && !prevEsc) gGame.state = GameState::SettingsOverlay;
        PhysicsStep(gGame, dt, gGame.levels[gGame.currentLevelIndex], keyA, keyD, keySpaceEdge);
    } else if (gGame.state == GameState::SettingsOverlay) {
        if ((esc && !prevEsc) || (enter && !prevEnter)) gGame.state = GameState::Playing;
        if (r && !prevR) ResetLevel(gGame);
        if (q && !prevQ) PostQuitMessage(0);
    } else if (gGame.state == GameState::Won) {
        if (n && !prevN && gGame.currentLevelIndex < 2) {
            gGame.currentLevelIndex++;
            ResetLevel(gGame);
        }
        if (r && !prevR) ResetLevel(gGame);
    } else if (gGame.state == GameState::Lost) {
        if (r && !prevR) ResetLevel(gGame);
    }

    prevEsc = esc;
    prevEnter = enter;
    prevR = r;
    prevN = n;
    prevQ = q;
    prevSpace = spaceDown;

    HDC hdcScr = GetDC(gHwnd);
    HDC hdcMem = CreateCompatibleDC(hdcScr);
    HBITMAP bmp = CreateCompatibleBitmap(hdcScr, kScreenW, kScreenH);
    HBITMAP oldBmp = (HBITMAP)SelectObject(hdcMem, bmp);

    RECT bg{0, 0, kScreenW, kScreenH};
    HBRUSH bgb = CreateSolidBrush(RGB(28, 30, 38));
    FillRect(hdcMem, &bg, bgb);
    DeleteObject(bgb);

    float cx = gGame.playerPos.x;
    float cy = gGame.playerPos.y;
    DrawLevel(hdcMem, gGame.levels[gGame.currentLevelIndex], cx, cy);
    DrawPixelDude(hdcMem, gGame.playerPos.x, gGame.playerPos.y, gGame.body, gGame.facingRight, cx, cy,
                   RGB(90, 140, 220));

    DrawUi(hdcMem, gGame);

    if (gGame.state == GameState::SettingsOverlay) DrawSettingsOverlay(hdcMem);

    BitBlt(hdcScr, 0, 0, kScreenW, kScreenH, hdcMem, 0, 0, SRCCOPY);

    SelectObject(hdcMem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(hdcMem);
    ReleaseDC(gHwnd, hdcScr);

    // 不得在 WM_TIMER 内、持有时钟 DC 时同步 MessageBox（嵌套消息循环易崩溃）；释放 DC 后 PostMessage 到主循环再弹窗
    static GameState prevEndMsgState = GameState::Playing;
    if (gHwnd && gGame.state != prevEndMsgState) {
        if (gGame.state == GameState::Won)
            PostMessageW(gHwnd, WM_APP, 1, 0);
        else if (gGame.state == GameState::Lost)
            PostMessageW(gHwnd, WM_APP, 2, 0);
        prevEndMsgState = gGame.state;
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_DESTROY:
            KillTimer(hwnd, 1);
            PostQuitMessage(0);
            return 0;
        case WM_TIMER:
            if (wParam == 1) GameFrame();
            return 0;
        case WM_APP:
            if (wParam == 1) {
                ShowSuccessTauntDialog(hwnd);
                ResetLevel(gGame);
            } else if (wParam == 2) {
                ShowFailureTauntDialog(hwnd);
                ResetLevel(gGame);
            }
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    Gdiplus::GdiplusStartupInput gdpsi{};
    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdpsi, nullptr);

    gGame.levels[0] = MakeLevelFromString(kLevel1);
    gGame.levels[1] = MakeLevelFromString(kLevel2);
    gGame.levels[2] = MakeLevelFromString(kLevel3);
    ResetLevel(gGame);

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"PixelEscapeWnd";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassW(&wc);

    RECT wr{0, 0, kScreenW, kScreenH};
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX, FALSE);
    int ww = wr.right - wr.left, wh = wr.bottom - wr.top;

    gHwnd = CreateWindowExW(0, L"PixelEscapeWnd", L"Pixel Escape — 像素闯关", WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
                            CW_USEDEFAULT, CW_USEDEFAULT, ww, wh, nullptr, nullptr, hInst, nullptr);
    ShowWindow(gHwnd, SW_SHOW);
    UpdateWindow(gHwnd);
    GameFrame();

    SetTimer(gHwnd, 1, 16, nullptr);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    Gdiplus::GdiplusShutdown(gdiplusToken);
    return (int)msg.wParam;
}
