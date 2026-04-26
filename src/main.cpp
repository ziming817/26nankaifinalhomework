/**
 * 《我才是奶龙》— Win32 + GDI，无第三方游戏库。
 *
 * 【游戏设计概要】
 * 1) 三关结构：主菜单可任选关卡；第 1、2 关为平台跳跃；第 3 关为固定场地 Boss 战。
 * 2) 第 1 关：平地向右推进，学习移动、跳跃、尖刺即死、真门 R 胜利、假门 X 即败。
 * 3) 第 2 关（解谜+机关）：先踩地面陷阱格 Z 触发“封门墙”，将玩家与真门隔在两侧；
 *    再踩与尖刺同形的解锁格 *（视觉上为尖刺，但判定为“奶龙光线”解锁，不伤血）获得横向奶龙光线；
 *    长按左键蓄力发射光线，清除封门地形后从真门 R 离开。
 * 4) 第 3 关：与“黑暗奶龙”Boss 对战，拾取场地道具获得瞄准奶龙弹，与 Boss 弹幕互伤，击败后通关并弹出祝贺图。
 * 5) 技术要点：离屏位图每帧重绘、胜负通过 WM_APP 延后弹窗避免在渲染 DC 内嵌套 MessageBox；F11 切换无边框全屏，StretchBlt 将 960×540 拉至整屏，鼠标坐标映射回逻辑分辨率。
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
#include <cstdlib>
#include <string>
#include <vector>

namespace {

// —— 显示与物理常数：逻辑分辨率 960×540、重力、移动与跳跃、格子像素边长 ——
constexpr int kScreenW = 960;
constexpr int kScreenH = 540;

constexpr float GRAVITY = 1800.0f;
constexpr float MOVE_SPEED = 260.0f;
constexpr float JUMP_VELOCITY = -480.0f;
constexpr int TILE = 32;
constexpr float kPlayerW = 24.0f;
constexpr float kPlayerH = 32.0f;

struct Vec2 {
    float x = 0, y = 0;
};

struct RectF {
    float x, y, w, h;
};
inline bool AabbOverlap(const RectF& a, const RectF& b) {
    return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}

inline void PlayerBodySize(float& w, float& h) {
    w = kPlayerW;
    h = kPlayerH;
}
enum class CellType : int {
    Empty = 0,
    Ground = 1,
    Spike = 2,
    PitMarker = 3,
    DoorReal = 4,
    DoorFake = 5,
    ButtonTrap = 6,
    NailongUnlockSpike = 7,
};

struct Level {
    int cols = 0;
    int rows = 0;
    std::vector<CellType> grid;
    Vec2 spawnTile{1, 1};
};

// 将 ASCII 地图字符转为 CellType；未知字符当空地
CellType CharToCell(char c) {
    switch (c) {
        case '#':
            return CellType::Ground;
        case '^':
            return CellType::Spike;
        case '.':
            return CellType::PitMarker;
        case 'R':
            return CellType::DoorReal;
        case 'X':
            return CellType::DoorFake;
        case 'Z':
            return CellType::ButtonTrap;
        case 'N':
        case '*':
            return CellType::NailongUnlockSpike;
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
// 第 1 关地图：R=真门（过关），X=假门（失败），^=即死尖刺，P=出生。
const char* kLevel1 = R"(
####################################################################
#..................................................................#
#..................................................................#
#..................................................................#
#.................................P................................#
#R####^####^####^####^#####################^############X###########
#..................................................................#
#..................................................................#
#..................................................................#
#..................................................................#
#..................................................................#
#..................................................................#
####################################################################
)";

// 第 2 关：# 可站立地面；Z=陷阱格（踩上生成封门墙）；*=奶龙激光格（先踩过 Z 再踩 * 解锁“奶龙光线”）；R=真门。…… 为外场空地。
const char* kLevel2 = R"(
##############################
#............................#
#............................#
#............................#
#............................#
#............................#
#............................#
#....P.......................#
#..#####Z##*##R###########...#
##############################
)";

// 第 3 关：封闭 Boss 战，P 为出生点；实际 Boss、掉落物。
const char* kLevel3 = R"(
##################################
#                                #
#                                #
#                                #
#                                #
#                                #
#                                #
#                                #
#                                #
#                                #
#  P                             #
##################################
)";
enum class GameState { MainMenu, Playing, SettingsOverlay, Won, Lost };
// —— 全局局内数据：当前关卡、玩家运动、
struct Game {
    int currentLevelIndex = 0;
    Level levels[3];
    GameState state = GameState::MainMenu;

    bool mainMenuPickLevel = false;
    int mainMenuSel = 0;

    Vec2 playerPos{0, 0};
    Vec2 playerVel{0, 0};
    bool facingRight = true;
    bool onGround = false;
    int level2DoorCellX = -1;
    int level2DoorCellY = -1;
    std::vector<CellType> level2GridNoWall;
    bool level2WallRaised = false;
    bool level2NailongUnlocked = false;
    bool level2WallDestroyed = false;
    float level2BeamAnimT = 0.0f;
    float level2LmbHoldT = 0.0f;
    bool level2LmbFiredThisHold = false;

    float level3BossX = 0.0f;
    float level3BossY = 0.0f;
    float level3BossVelX = 0.0f;
    float level3BossNextTurnT = 0.0f;
    int level3PlayerHp = 100;
    int level3BossHp = 100;
    float level3ContactDmgCd = 0.0f;
    bool level3BeamArmed = false;
    bool level3PickupVisible = true;
    float level3PickX = 0.0f;
    float level3PickY = 0.0f;
    float level3AimX = 0.0f;
    float level3AimY = 0.0f;
    float level3BossVelY = 0.0f;
    float level3BossShootCd = 0.0f;

    struct Level3Bullet {
        float x, y, vx, vy;
        float ttl;  // Boss 攻击，玩家攻击 .
    };
    std::vector<Level3Bullet> level3PlayerBullets;
    std::vector<Level3Bullet> level3EnemyBullets;
    struct CollisionCache {
        std::vector<RectF> solids, spikes, doorReal, doorFake, btnTrap, btnNailong;
    } col;
};

RectF PlayerHitbox(const Game& g) {
    float w, h;
    PlayerBodySize(w, h);
    return {g.playerPos.x - w * 0.5f, g.playerPos.y - h, w, h};
}

// 致命尖刺、真/假门、陷阱与奶龙光线解锁区
void BuildSolidsAndHazards(const Level& L, std::vector<RectF>& solids, std::vector<RectF>& spikes,
                           std::vector<RectF>& doorReal, std::vector<RectF>& doorFake, std::vector<RectF>& btnTrap,
                           std::vector<RectF>& btnNailong) {
    solids.clear();
    spikes.clear();
    doorReal.clear();
    doorFake.clear();
    btnTrap.clear();
    btnNailong.clear();

    for (int y = 0; y < L.rows; ++y) {
        for (int x = 0; x < L.cols; ++x) {
            CellType c = L.grid[y * L.cols + x];
            float px = (float)x * (float)TILE;
            float py = (float)y * (float)TILE;
            RectF cell{px, py, (float)TILE, (float)TILE};
            switch (c) {
                case CellType::Ground:
                    solids.push_back(cell);
                    break;
                case CellType::Spike:
                    spikes.push_back(
                        {px + TILE * 0.05f, py + TILE * 0.12f, TILE * 0.9f, TILE * 0.88f});
                    break;
                case CellType::DoorReal:
                    doorReal.push_back(cell);
                    break;
                case CellType::DoorFake:
                    doorFake.push_back(cell);
                    break;
                case CellType::ButtonTrap:
                    btnTrap.push_back(cell);
                    break;
                case CellType::NailongUnlockSpike:
                    btnNailong.push_back(
                        {px + TILE * 0.05f, py + TILE * 0.12f, TILE * 0.9f, TILE * 0.88f});
                    break;
                case CellType::PitMarker:
                case CellType::Empty:
                default:
                    break;
            }
        }
    }
}

// —— 第 2 关：陷阱格封门、* 格解锁左键奶龙光线、发射后还原无墙地图 ——
// 用脚底小矩形与“按钮格”做重叠，判定踩中地面机关
bool IsOverButton(const RectF& player, const RectF& btn) {
    RectF feet{player.x + player.w * 0.2f, player.y + player.h - 2.0f, player.w * 0.6f, 4.0f};
    return AabbOverlap(feet, btn);
}

// 在真门格上方用砖块造“封门墙”，将玩家与门隔开
void ApplyLevel2EnclosingWall(Level& L, int rx, int ry) {
    if (rx < 0 || rx >= L.cols || ry < 0 || ry >= L.rows) return;
    const int WALL_H = 7;
    int topY = ry - WALL_H;
    if (topY < 0) topY = 0;
    for (int x = rx - 1; x <= rx + 1; ++x) {
        if (x >= 0 && x < L.cols)
            L.grid[topY * L.cols + x] = CellType::Ground;
    }
    for (int y = topY; y <= ry; ++y) {
        for (int dx : {-1, 1}) {
            int x = rx + dx;
            if (x >= 0 && x < L.cols)
                L.grid[y * L.cols + x] = CellType::Ground;
        }
    }
    for (int y = topY + 1; y < ry; ++y) {
        L.grid[y * L.cols + rx] = CellType::Ground;
    }
}

bool TryApplyLevel2TrapButton(Game& g, Level& L) {
    if (g.currentLevelIndex != 1 || g.level2WallRaised) return false;
    auto& c = g.col;
    BuildSolidsAndHazards(L, c.solids, c.spikes, c.doorReal, c.doorFake, c.btnTrap, c.btnNailong);
    RectF hb = PlayerHitbox(g);
    for (const auto& t : c.btnTrap) {
        if (IsOverButton(hb, t)) {
            if (g.level2DoorCellX >= 0)
                ApplyLevel2EnclosingWall(L, g.level2DoorCellX, g.level2DoorCellY);
            g.level2WallRaised = true;
            return true;
        }
    }
    return false;
}

// 封门后踩中 * / N 格，解锁第 2 关长按左键的横向奶龙光线
bool TryApplyLevel2NailongButton(Game& g, Level& L) {
    if (g.currentLevelIndex != 1 || g.level2NailongUnlocked) return false;
    if (!g.level2WallRaised) return false;
    auto& c = g.col;
    BuildSolidsAndHazards(L, c.solids, c.spikes, c.doorReal, c.doorFake, c.btnTrap, c.btnNailong);
    RectF hb = PlayerHitbox(g);
    for (const auto& b : c.btnNailong) {
        if (IsOverButton(hb, b)) {
            g.level2NailongUnlocked = true;
            return true;
        }
    }
    return false;
}

bool TryFireLevel2NailongBeam(Game& g, Level& L) {
    if (g.currentLevelIndex != 1 || !g.level2NailongUnlocked || g.level2WallDestroyed) return false;
    if (g.level2GridNoWall.size() != (size_t)(L.rows * L.cols)) return false;
    L.grid = g.level2GridNoWall;
    g.level2WallDestroyed = true;
    g.level2BeamAnimT = 0.28f;
    return true;
}

float FeetYOnGroundBelow(const Level& L, int gx, int py);

// —— 第 3 关：Boss/攻击/拾取与瞄准
constexpr float kL3BossW = 48.0f;
constexpr float kL3BossH = 50.0f;
constexpr int kL3PlayerBulletDmg = 25;
constexpr int kL3BossBulletDmg = 10;
constexpr float kL3BossBulletLifetime = 5.0f;
constexpr float kL3PlayerBulletSpeed = 880.0f;
constexpr float kL3BossBulletSpeed = 260.0f;
constexpr float kL3BossShootInterval = 0.62f;
constexpr float kL3PlayerBulletR = 5.0f;
constexpr float kL3BossBulletR = 4.0f;

RectF Level3BossHitbox(const Game& g) {
    return {g.level3BossX - kL3BossW * 0.5f, g.level3BossY - kL3BossH, kL3BossW, kL3BossH};
}

RectF Level3PickupRect(const Game& g) {
    return {g.level3PickX - 14.0f, g.level3PickY - 22.0f, 28.0f, 28.0f};
}

void RespawnLevel3Pickup(Game& g, const Level& L) {
    std::vector<std::pair<int, int>> cands;
    for (int y = 0; y < L.rows - 1; ++y) {
        for (int x = 0; x < L.cols; ++x) {
            CellType c = L.grid[y * L.cols + x];
            if (c != CellType::Empty && c != CellType::PitMarker) continue;
            if (L.grid[(y + 1) * L.cols + x] != CellType::Ground) continue;
            cands.push_back({x, y});
        }
    }
    if (cands.empty()) {
        g.level3PickX = (float)L.cols * TILE * 0.5f;
        g.level3PickY = (float)(L.rows - 2) * TILE + TILE * 0.3f;
    } else {
        for (int tryn = 0; tryn < 48; ++tryn) {
            const auto& p = cands[(size_t)(rand() % (int)cands.size())];
            g.level3PickX = (float)p.first * TILE + TILE * 0.5f;
            g.level3PickY = (float)p.second * TILE + TILE * 0.45f;
            if (!AabbOverlap(Level3PickupRect(g), Level3BossHitbox(g))) break;
        }
    }
    g.level3PickupVisible = true;
}

// 进入第 3 关时：双方满血、Boss 就位、在空地上随机刷新一个可拾取的奶龙子弹
void InitLevel3State(Game& g, const Level& L) {
    g.level3PlayerHp = 100;
    g.level3BossHp = 100;
    g.level3BossVelX = 0.0f;
    g.level3BossVelY = 0.0f;
    g.level3BossNextTurnT = 0.35f;
    g.level3BossShootCd = 1.0f;
    g.level3ContactDmgCd = 0.0f;
    g.level3BeamArmed = false;
    g.level3PlayerBullets.clear();
    g.level3EnemyBullets.clear();
    int gx = (int)floorf(L.spawnTile.x + 0.5f);
    int py = (int)floorf(L.spawnTile.y + 0.5f);
    float floorY = FeetYOnGroundBelow(L, gx, py);
    g.level3BossY = floorY - 96.0f;
    g.level3BossX = (float)(L.cols - 6) * TILE + 16.0f;
    RespawnLevel3Pickup(g, L);
}

static RectF Level3BulletRect(float cx, float cy, float r) {
    return {cx - r, cy - r, r * 2.0f, r * 2.0f};
}

// 第三关：Boss 随机游走、朝玩家方向攻击、玩家攻击与 Boss 攻击、地形和生命结算、拾取与瞄准发射
void Level3Update(Game& g, Level& L, float dt, const std::vector<RectF>& solids, bool lmbClick) {
    g.level3BossNextTurnT -= dt;
    if (g.level3BossNextTurnT <= 0.0f) {
        g.level3BossNextTurnT = 0.28f + (float)(rand() % 60) / 200.0f;
        int rx = rand() % 3;
        int ry = rand() % 3;
        g.level3BossVelX = (rx == 0) ? 0.0f : (rx == 1 ? -1.0f : 1.0f) * 190.0f;
        g.level3BossVelY = (ry == 0) ? 0.0f : (ry == 1 ? -1.0f : 1.0f) * 130.0f;
    }

    g.level3BossX += g.level3BossVelX * dt;
    for (int pass = 0; pass < 3; ++pass) {
        RectF bb = Level3BossHitbox(g);
        for (const auto& s : solids) {
            if (AabbOverlap(bb, s)) {
                float penL = (bb.x + bb.w) - s.x;
                float penR = (s.x + s.w) - bb.x;
                if (penL > 0 && penR > 0) {
                    if (penL < penR) {
                        g.level3BossX = s.x - kL3BossW * 0.5f - 0.02f;
                        g.level3BossVelX = 0.0f;
                    } else {
                        g.level3BossX = s.x + s.w + kL3BossW * 0.5f + 0.02f;
                        g.level3BossVelX = 0.0f;
                    }
                }
            }
        }
    }

    g.level3BossY += g.level3BossVelY * dt;
    for (int pass = 0; pass < 3; ++pass) {
        RectF bb = Level3BossHitbox(g);
        for (const auto& s : solids) {
            if (AabbOverlap(bb, s)) {
                float penB = (bb.y + bb.h) - s.y;
                float penT = (s.y + s.h) - bb.y;
                if (penB > 0 && penT > 0) {
                    if (penB < penT) {
                        g.level3BossY = s.y - 0.02f;
                        if (g.level3BossVelY > 0.0f) g.level3BossVelY = 0.0f;
                    } else {
                        g.level3BossY = s.y + s.h + kL3BossH + 0.02f;
                        if (g.level3BossVelY < 0.0f) g.level3BossVelY = 0.0f;
                    }
                }
            }
        }
    }

    float wmax = (float)L.cols * TILE;
    float minFeetY = TILE * 3.0f;
    float maxFeetY = (float)(L.rows - 1) * TILE - 12.0f;
    if (g.level3BossX < kL3BossW * 0.5f + 4.0f) {
        g.level3BossX = kL3BossW * 0.5f + 4.0f;
        g.level3BossVelX = 0.0f;
    }
    if (g.level3BossX > wmax - kL3BossW * 0.5f - 4.0f) {
        g.level3BossX = wmax - kL3BossW * 0.5f - 4.0f;
        g.level3BossVelX = 0.0f;
    }
    if (g.level3BossY < minFeetY) {
        g.level3BossY = minFeetY;
        g.level3BossVelY = (std::max)(0.0f, g.level3BossVelY);
    }
    if (g.level3BossY > maxFeetY) {
        g.level3BossY = maxFeetY;
        g.level3BossVelY = (std::min)(0.0f, g.level3BossVelY);
    }

    g.level3BossShootCd -= dt;
    if (g.level3BossShootCd <= 0.0f && g.level3BossHp > 0) {
        g.level3BossShootCd = kL3BossShootInterval;
        float bx = g.level3BossX;
        float by = g.level3BossY - kL3BossH * 0.55f;
        float pw, ph;
        PlayerBodySize(pw, ph);
        float tx = g.playerPos.x;
        float ty = g.playerPos.y - ph * 0.5f;
        float dx = tx - bx;
        float dy = ty - by;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 4.0f) {
            dx = -1.0f;
            dy = 0.0f;
            len = 1.0f;
        } else {
            dx /= len;
            dy /= len;
        }
        g.level3EnemyBullets.push_back(
            Game::Level3Bullet{bx, by, dx * kL3BossBulletSpeed, dy * kL3BossBulletSpeed, kL3BossBulletLifetime});
    }

    auto bulletHitsSolid = [&](const RectF& br) -> bool {
        for (const auto& s : solids) {
            if (AabbOverlap(br, s)) return true;
        }
        return false;
    };

    for (size_t i = 0; i < g.level3PlayerBullets.size();) {
        auto& pb = g.level3PlayerBullets[i];
        pb.x += pb.vx * dt;
        pb.y += pb.vy * dt;
        RectF br = Level3BulletRect(pb.x, pb.y, kL3PlayerBulletR);
        bool erase = false;
        if (bulletHitsSolid(br)) erase = true;
        if (AabbOverlap(br, Level3BossHitbox(g)) && g.level3BossHp > 0) {
            g.level3BossHp -= kL3PlayerBulletDmg;
            if (g.level3BossHp < 0) g.level3BossHp = 0;
            erase = true;
        }
        if (pb.x < -80.0f || pb.x > wmax + 80.0f || pb.y < -80.0f || pb.y > (float)L.rows * TILE + 80.0f) erase = true;
        if (erase) {
            g.level3PlayerBullets[i] = g.level3PlayerBullets.back();
            g.level3PlayerBullets.pop_back();
        } else {
            ++i;
        }
    }

    for (size_t i = 0; i < g.level3EnemyBullets.size();) {
        auto& b = g.level3EnemyBullets[i];
        b.x += b.vx * dt;
        b.y += b.vy * dt;
        if (b.ttl > 0.0f) {
            b.ttl -= dt;
            if (b.ttl <= 0.0f) b.ttl = 0.0f;
        }
        RectF br = Level3BulletRect(b.x, b.y, kL3BossBulletR);
        bool erase = false;
        if (b.ttl <= 0.0f) erase = true;
        if (bulletHitsSolid(br)) erase = true;
        if (AabbOverlap(br, PlayerHitbox(g))) {
            g.level3PlayerHp -= kL3BossBulletDmg;
            if (g.level3PlayerHp < 0) g.level3PlayerHp = 0;
            erase = true;
        }
        if (b.x < -80.0f || b.x > wmax + 80.0f || b.y < -80.0f || b.y > (float)L.rows * TILE + 80.0f) erase = true;
        if (erase) {
            g.level3EnemyBullets[i] = g.level3EnemyBullets.back();
            g.level3EnemyBullets.pop_back();
        } else {
            ++i;
        }
    }

    g.level3ContactDmgCd -= dt;
    if (AabbOverlap(PlayerHitbox(g), Level3BossHitbox(g)) && g.level3ContactDmgCd <= 0.0f) {
        g.level3PlayerHp -= 10;
        if (g.level3PlayerHp < 0) g.level3PlayerHp = 0;
        g.level3ContactDmgCd = 0.45f;
    }

    if (g.level3PickupVisible && !g.level3BeamArmed) {
        if (AabbOverlap(PlayerHitbox(g), Level3PickupRect(g))) {
            g.level3BeamArmed = true;
            g.level3PickupVisible = false;
        }
    }

    if (g.level3BeamArmed && lmbClick) {
        float pw, ph;
        PlayerBodySize(pw, ph);
        float ox = g.playerPos.x;
        float oy = g.playerPos.y - ph * 0.45f;
        float dx = g.level3AimX - ox;
        float dy = g.level3AimY - oy;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 8.0f) {
            dx = g.facingRight ? 1.0f : -1.0f;
            dy = 0.0f;
            len = 1.0f;
        } else {
            dx /= len;
            dy /= len;
        }
        g.level3PlayerBullets.push_back(
            Game::Level3Bullet{ox, oy, dx * kL3PlayerBulletSpeed, dy * kL3PlayerBulletSpeed, -1.0f});
        g.level3BeamArmed = false;
        RespawnLevel3Pickup(g, L);
    }
}

// 通关判定
void PhysicsStep(Game& g, float dt, bool keyA, bool keyD, bool keySpaceEdge, bool lmbDown, bool lmbClick) {
    Level& L = g.levels[g.currentLevelIndex];
    auto& c = g.col;
    BuildSolidsAndHazards(L, c.solids, c.spikes, c.doorReal, c.doorFake, c.btnTrap,
                          c.btnNailong);

    if (g.currentLevelIndex == 1 && g.level2BeamAnimT > 0.0f) {
        g.level2BeamAnimT -= dt;
        if (g.level2BeamAnimT < 0.0f) g.level2BeamAnimT = 0.0f;
    }
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
        for (const auto& s : c.solids) {
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
        for (const auto& s : c.solids) {
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

    if (TryApplyLevel2TrapButton(g, L)) {
        BuildSolidsAndHazards(L, c.solids, c.spikes, c.doorReal, c.doorFake, c.btnTrap,
                              c.btnNailong);
    }
    TryApplyLevel2NailongButton(g, L);
    if (g.currentLevelIndex == 1) {
        constexpr float kL2LmbMinHold = 0.22f;
        if (g.level2NailongUnlocked && !g.level2WallDestroyed) {
            if (lmbDown) {
                g.level2LmbHoldT += dt;
                if (g.level2LmbHoldT >= kL2LmbMinHold && !g.level2LmbFiredThisHold) {
                    if (TryFireLevel2NailongBeam(g, L)) {
                        g.level2LmbFiredThisHold = true;
                        BuildSolidsAndHazards(L, c.solids, c.spikes, c.doorReal, c.doorFake, c.btnTrap,
                                              c.btnNailong);
                    }
                }
            } else {
                g.level2LmbHoldT = 0.0f;
                g.level2LmbFiredThisHold = false;
            }
        } else {
            g.level2LmbHoldT = 0.0f;
            g.level2LmbFiredThisHold = false;
        }
    }

    if (g.currentLevelIndex == 2) Level3Update(g, L, dt, c.solids, lmbClick);

    RectF hb = PlayerHitbox(g);
    for (const auto& sp : c.spikes) {
        if (AabbOverlap(hb, sp)) {
            g.state = GameState::Lost;
            return;
        }
    }

    if (g.currentLevelIndex == 2) {
        if (g.level3BossHp <= 0) {
            g.state = GameState::Won;
            return;
        }
        if (g.level3PlayerHp <= 0) {
            g.state = GameState::Lost;
            return;
        }
    } else {
        for (const auto& d : c.doorReal) {
            if (AabbOverlap(hb, d)) {
                g.state = GameState::Won;
                return;
            }
        }
        for (const auto& d : c.doorFake) {
            if (AabbOverlap(hb, d)) {
                g.state = GameState::Lost;
                return;
            }
        }
    }

    float levelH = (float)L.rows * (float)TILE;
    float levelW = (float)L.cols * (float)TILE;
    if (g.playerPos.y > levelH + 100 || g.playerPos.x < -200 || g.playerPos.x > levelW + 200) {
        g.state = GameState::Lost;
    }
}

// 避免出生在地图外部。
float FeetYOnGroundBelow(const Level& L, int gx, int py) {
    if (gx < 0 || gx >= L.cols) return (float)((py + 1) * TILE);
    for (int y = py + 1; y < L.rows; ++y) {
        if (L.grid[y * L.cols + gx] == CellType::Ground) return (float)(y * TILE);
    }
    return (float)((py + 1) * TILE);
}

// 重开本关
void ResetLevel(Game& g) {
    g.level2DoorCellX = -1;
    g.level2DoorCellY = -1;
    g.level2GridNoWall.clear();
    g.level2WallRaised = false;
    g.level2NailongUnlocked = false;
    g.level2WallDestroyed = false;
    g.level2BeamAnimT = 0.0f;
    g.level2LmbHoldT = 0.0f;
    g.level2LmbFiredThisHold = false;
    g.levels[g.currentLevelIndex] =
        g.currentLevelIndex == 0 ? MakeLevelFromString(kLevel1)
        : g.currentLevelIndex == 1 ? MakeLevelFromString(kLevel2)
                                   : MakeLevelFromString(kLevel3);
    Level& L = g.levels[g.currentLevelIndex];
    if (g.currentLevelIndex == 1) {
        for (int y = 0; y < L.rows; ++y) {
            for (int x = 0; x < L.cols; ++x) {
                if (L.grid[y * L.cols + x] == CellType::DoorReal) {
                    g.level2DoorCellX = x;
                    g.level2DoorCellY = y;
                    break;
                }
            }
        }
        g.level2GridNoWall = L.grid;
    }
    if (g.currentLevelIndex == 2) InitLevel3State(g, L);
    int gx = (int)floorf(L.spawnTile.x + 0.5f);
    int py = (int)floorf(L.spawnTile.y + 0.5f);
    g.playerPos = {L.spawnTile.x * TILE + TILE * 0.5f, FeetYOnGroundBelow(L, gx, py)};
    g.playerVel = {0, 0};
    g.state = GameState::Playing;
}

// ---------- 摄像机跟随玩家 ----------

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

void DrawUtf8Centered(HDC hdc, int centerX, int y, int heightPx, COLORREF c, const char* utf8) {
    int nw = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (nw <= 0) return;
    std::wstring w(nw, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w.data(), nw);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, c);
    HFONT f = CreateFontW(-heightPx, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                          CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    HFONT old = (HFONT)SelectObject(hdc, f);
    SIZE sz{};
    GetTextExtentPoint32W(hdc, w.c_str(), (int)wcslen(w.c_str()), &sz);
    int tx = centerX - (int)sz.cx / 2;
    TextOutW(hdc, tx, y, w.c_str(), (int)wcslen(w.c_str()));
    SelectObject(hdc, old);
    DeleteObject(f);
}

// 绘制玩家，boss；玩家，boss的身高与贴图
void DrawPixelDude(HDC hdc, float footX, float footY, bool facingRight, float cx, float cy, COLORREF shirt,
                   int displayLevelIndex) {
    float w, h;
    PlayerBodySize(w, h);
    float ox = footX - w * 0.5f;
    float oy = footY - h;

    static Gdiplus::Bitmap* s_playerBmp = nullptr;
    static int s_playerLoad = 0;
    if (s_playerLoad == 0) {
        s_playerLoad = -1;
        wchar_t path[MAX_PATH];
        if (GetModuleFileNameW(nullptr, path, MAX_PATH) > 0) {
            wchar_t* slash = wcsrchr(path, L'\\');
            if (slash) *(slash + 1) = L'\0';
            wcscat_s(path, MAX_PATH, L"assets\\player_nailong.png");
            Gdiplus::Bitmap* b = Gdiplus::Bitmap::FromFile(path);
            if (b && b->GetLastStatus() == Gdiplus::Ok) {
                s_playerBmp = b;
                s_playerLoad = 1;
            } else {
                if (b) delete b;
            }
        }
    }

    if (s_playerLoad == 1 && s_playerBmp) {
        const float drawW = (displayLevelIndex == 2) ? w : kL3BossW;
        const float drawH = (displayLevelIndex == 2) ? h : kL3BossH;
        const float drawOx = footX - drawW * 0.5f;
        const float drawOy = footY - drawH;
        int sx0, sy0, sx1, sy1;
        WorldToScreen(drawOx, drawOy, cx, cy, &sx0, &sy0);
        WorldToScreen(drawOx + drawW, drawOy + drawH, cx, cy, &sx1, &sy1);
        int left = (std::min)(sx0, sx1);
        int top = (std::min)(sy0, sy1);
        int right = (std::max)(sx0, sx1);
        int bottom = (std::max)(sy0, sy1);
        int rw = right - left;
        int rh = bottom - top;
        if (rw > 0 && rh > 0) {
            Gdiplus::Graphics gfx(hdc);
            gfx.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            Gdiplus::ImageAttributes ia;
            ia.SetColorKey(Gdiplus::Color(255, 253, 253, 253), Gdiplus::Color(255, 255, 255, 255));
            const INT iw = (INT)s_playerBmp->GetWidth();
            const INT ih = (INT)s_playerBmp->GetHeight();
            if (!facingRight) {
                gfx.TranslateTransform((Gdiplus::REAL)(left + rw), (Gdiplus::REAL)top);
                gfx.ScaleTransform(-1.0f, 1.0f);
                gfx.DrawImage(s_playerBmp, Gdiplus::Rect(0, 0, rw, rh), 0, 0, iw, ih, Gdiplus::UnitPixel, &ia);
                gfx.ResetTransform();
            } else {
                gfx.DrawImage(s_playerBmp, Gdiplus::Rect(left, top, rw, rh), 0, 0, iw, ih, Gdiplus::UnitPixel,
                             &ia);
            }
            return;
        }
    }

    auto block = [&](float bx, float by, float bw, float bh, COLORREF c) {
        FillWorldRect(hdc, ox + bx, oy + by, bw, bh, cx, cy, c);
    };

    COLORREF skin = RGB(235, 205, 185);
    COLORREF dark = RGB((GetRValue(shirt) * 3) / 4, (GetGValue(shirt) * 3) / 4, (GetBValue(shirt) * 3) / 4);
    block(w * 0.3f, 0, w * 0.4f, h * 0.22f, skin);
    block(w * 0.22f, h * 0.22f, w * 0.56f, h * 0.45f, shirt);
    block(w * 0.28f, h * 0.67f, w * 0.2f, h * 0.33f, dark);
    block(w * 0.52f, h * 0.67f, w * 0.2f, h * 0.33f, dark);
}

// 尖刺与地面
void DrawLevel(HDC hdc, const Level& L, float cx, float cy) {
    for (int pass = 0; pass < 2; ++pass) {
        for (int y = 0; y < L.rows; ++y) {
            for (int x = 0; x < L.cols; ++x) {
                CellType c = L.grid[y * L.cols + x];
                if (pass == 0 && (c == CellType::Spike || c == CellType::NailongUnlockSpike)) continue;
                if (pass == 1 && c != CellType::Spike && c != CellType::NailongUnlockSpike) continue;

                float px = (float)x * (float)TILE;
                float py = (float)y * (float)TILE;
                switch (c) {
                case CellType::Ground:
                    FillWorldRect(hdc, px, py, (float)TILE, (float)TILE, cx, cy, RGB(55, 60, 70));
                    FrameWorldRect(hdc, px, py, (float)TILE, (float)TILE, cx, cy, RGB(90, 95, 105));
                    break;
                case CellType::Spike:
                case CellType::NailongUnlockSpike:
                    FillWorldTriangle(hdc, px + TILE * 0.5f, py, px + TILE, py + TILE, px, py + TILE, cx, cy,
                                      RGB(255, 85, 95));
                    OutlineWorldTriangle(hdc, px + TILE * 0.5f, py, px + TILE, py + TILE, px, py + TILE, cx, cy,
                                         RGB(255, 255, 220));
                    break;
                case CellType::DoorReal: {
                    FillWorldRect(hdc, px, py, (float)TILE, (float)TILE, cx, cy, RGB(55, 60, 70));
                    FrameWorldRect(hdc, px, py, (float)TILE, (float)TILE, cx, cy, RGB(90, 95, 105));
                    const float groundH = 8.0f;
                    const float doorTop = py + 1.0f;
                    const float doorH = (float)TILE - groundH - 1.0f;
                    FillWorldRect(hdc, px + 4, doorTop, TILE - 8, doorH, cx, cy, RGB(70, 180, 90));
                    FillWorldRect(hdc, px + 10, doorTop + 4.0f, TILE - 20, doorH - 8.0f, cx, cy, RGB(40, 90, 50));
                    FillWorldCircle(hdc, px + TILE - 12, doorTop + doorH * 0.5f, 3.0f, cx, cy, RGB(240, 220, 80));
                    break;
                }
                case CellType::DoorFake: {
                    FillWorldRect(hdc, px, py, (float)TILE, (float)TILE, cx, cy, RGB(55, 60, 70));
                    FrameWorldRect(hdc, px, py, (float)TILE, (float)TILE, cx, cy, RGB(90, 95, 105));
                    const float groundH = 8.0f;
                    const float doorTop = py + 1.0f;
                    const float doorH = (float)TILE - groundH - 1.0f;
                    FillWorldRect(hdc, px + 4, doorTop, TILE - 8, doorH, cx, cy, RGB(70, 180, 90));
                    FillWorldRect(hdc, px + 10, doorTop + 4.0f, TILE - 20, doorH - 8.0f, cx, cy, RGB(40, 90, 50));
                    FillWorldCircle(hdc, px + TILE - 12, doorTop + doorH * 0.5f, 3.0f, cx, cy, RGB(240, 220, 80));
                    break;
                }
                case CellType::ButtonTrap:
                    FillWorldRect(hdc, px, py, (float)TILE, (float)TILE, cx, cy, RGB(55, 60, 70));
                    FrameWorldRect(hdc, px, py, (float)TILE, (float)TILE, cx, cy, RGB(90, 95, 105));
                    break;
                default:
                    break;
                }
            }
        }
    }
}

void DrawNailongBeam(HDC hdc, const Game& g, float cx, float cy) {
    if (g.currentLevelIndex != 1 || g.level2BeamAnimT <= 0.0f) return;
    const Level& L = g.levels[1];
    float yc = g.playerPos.y - 14.0f;
    float x0 = g.playerPos.x + 14.0f;
    float x1 = (float)L.cols * TILE;
    if (x1 > x0 + 8.0f) {
        FillWorldRect(hdc, x0, yc, x1 - x0, 8.0f, cx, cy, RGB(255, 200, 90));
        FillWorldRect(hdc, x0, yc + 2, x1 - x0, 4.0f, cx, cy, RGB(255, 255, 220));
    }
}

void DrawWorldLine(HDC hdc, float x0, float y0, float x1, float y1, float cx, float cy, int penW, COLORREF col) {
    int sx0, sy0, sx1, sy1;
    WorldToScreen(x0, y0, cx, cy, &sx0, &sy0);
    WorldToScreen(x1, y1, cx, cy, &sx1, &sy1);
    HPEN pen = CreatePen(PS_SOLID, penW, col);
    HPEN old = (HPEN)SelectObject(hdc, pen);
    MoveToEx(hdc, sx0, sy0, nullptr);
    LineTo(hdc, sx1, sy1);
    SelectObject(hdc, old);
    DeleteObject(pen);
}

void DrawLevel3Boss(HDC hdc, const Game& g, float cx, float cy) {
    static Gdiplus::Bitmap* s_bossBmp = nullptr;
    static int s_bossLoad = 0;
    if (s_bossLoad == 0) {
        s_bossLoad = -1;
        wchar_t path[MAX_PATH];
        if (GetModuleFileNameW(nullptr, path, MAX_PATH) > 0) {
            wchar_t* slash = wcsrchr(path, L'\\');
            if (slash) *(slash + 1) = L'\0';
            wcscat_s(path, MAX_PATH, L"assets\\boss_dark.png");
            Gdiplus::Bitmap* b = Gdiplus::Bitmap::FromFile(path);
            if (b && b->GetLastStatus() == Gdiplus::Ok) {
                s_bossBmp = b;
                s_bossLoad = 1;
            } else {
                if (b) delete b;
            }
        }
    }
    RectF bb = Level3BossHitbox(g);
    if (s_bossLoad == 1 && s_bossBmp) {
        int sx0, sy0, sx1, sy1;
        WorldToScreen(bb.x, bb.y, cx, cy, &sx0, &sy0);
        WorldToScreen(bb.x + bb.w, bb.y + bb.h, cx, cy, &sx1, &sy1);
        int left = (std::min)(sx0, sx1);
        int top = (std::min)(sy0, sy1);
        int right = (std::max)(sx0, sx1);
        int bottom = (std::max)(sy0, sy1);
        if (right > left && bottom > top) {
            Gdiplus::Graphics gfx(hdc);
            gfx.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            Gdiplus::Rect dst(left, top, right - left, bottom - top);
            gfx.DrawImage(s_bossBmp, dst);
        }
    } else {
        FillWorldRect(hdc, bb.x + 6, bb.y, bb.w - 12, 12, cx, cy, RGB(120, 40, 50));
        FillWorldRect(hdc, bb.x + 10, bb.y + 12, bb.w - 20, bb.h * 0.4f, cx, cy, RGB(200, 60, 80));
        FillWorldRect(hdc, bb.x + 4, bb.y + bb.h * 0.5f, 8, 18, cx, cy, RGB(200, 60, 80));
        FillWorldRect(hdc, bb.x + bb.w - 12, bb.y + bb.h * 0.5f, 8, 18, cx, cy, RGB(200, 60, 80));
        FillWorldRect(hdc, bb.x + 8, bb.y + bb.h - 10, bb.w - 16, 10, cx, cy, RGB(40, 40, 50));
    }
}

// 第 3 关准备：Boss 、可拾取武器、已武装时从角色到鼠标的瞄准线、双方攻击
void DrawLevel3Extra(HDC hdc, const Game& g, float cx, float cy) {
    if (g.currentLevelIndex != 2) return;
    DrawLevel3Boss(hdc, g, cx, cy);

    if (g.level3PickupVisible) {
        FillWorldCircle(hdc, g.level3PickX, g.level3PickY - 6, 9.0f, cx, cy, RGB(90, 220, 255));
        FrameWorldRect(hdc, g.level3PickX - 10, g.level3PickY - 24, 20, 20, cx, cy, RGB(200, 255, 255));
    }

    if (g.level3BeamArmed) {
        float pw, ph;
        PlayerBodySize(pw, ph);
        float px = g.playerPos.x;
        float py = g.playerPos.y - ph * 0.45f;
        DrawWorldLine(hdc, px, py, g.level3AimX, g.level3AimY, cx, cy, 2, RGB(180, 200, 255));
    }
    for (const auto& pb : g.level3PlayerBullets) {
        FillWorldCircle(hdc, pb.x, pb.y, kL3PlayerBulletR, cx, cy, RGB(255, 210, 80));
        FrameWorldRect(hdc, pb.x - kL3PlayerBulletR, pb.y - kL3PlayerBulletR, kL3PlayerBulletR * 2.0f,
                       kL3PlayerBulletR * 2.0f, cx, cy, RGB(255, 245, 200));
    }
    for (const auto& eb : g.level3EnemyBullets) {
        FillWorldCircle(hdc, eb.x, eb.y, kL3BossBulletR, cx, cy, RGB(255, 255, 255));
    }
}

void DrawUi(HDC hdc, const Game& g) {
    if (g.currentLevelIndex == 2) {
        char buf[256];
        snprintf(buf, sizeof(buf), u8"第 3 / 3 关   生命 %d / 100   黑暗奶龙 %d / 100   F11 全屏  ESC 设置",
                 g.level3PlayerHp, g.level3BossHp);
        DrawUtf8(hdc, 12, 10, 16, RGB(245, 245, 245), buf);
        return;
    }
    char buf[256];
    snprintf(buf, sizeof(buf), u8"第 %d / 3 关   A/D 移动  空格 跳跃  F11 全屏  ESC 设置", g.currentLevelIndex + 1);
    DrawUtf8(hdc, 12, 10, 18, RGB(245, 245, 245), buf);
}

void DrawSettingsOverlay(HDC hdc) {
    int boxW = 420, boxH = 300;
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
    DrawUtf8(hdc, bx + 24, by + 160, 18, RGB(210, 210, 210), u8"[F11] 全屏开/关");
}

// —— 主菜单：按钮布局、绘制与键鼠/Enter 选关 ——
constexpr int kMenuBtnW = 300;
constexpr int kMenuBtnH = 50;
constexpr int kMenuBtnGap = 18;
constexpr int kMenuBtnY0 = 158;

void MainMenuGetButtonRect(const Game& g, int index, RECT* out) {
    int x0 = (kScreenW - kMenuBtnW) / 2;
    int n = g.mainMenuPickLevel ? 4 : 3;
    if (index < 0 || index >= n) {
        SetRect(out, 0, 0, 0, 0);
        return;
    }
    int y = kMenuBtnY0 + index * (kMenuBtnH + kMenuBtnGap);
    SetRect(out, x0, y, x0 + kMenuBtnW, y + kMenuBtnH);
}

void DrawMenuPanelButton(HDC hdc, const RECT& r, const char* utf8, bool hot) {
    HBRUSH bgb = CreateSolidBrush(hot ? RGB(75, 95, 130) : RGB(48, 54, 68));
    FillRect(hdc, &r, bgb);
    DeleteObject(bgb);
    HBRUSH fr = CreateSolidBrush(RGB(160, 170, 190));
    FrameRect(hdc, &r, fr);
    DeleteObject(fr);
    SetBkMode(hdc, TRANSPARENT);
    HFONT f =
        CreateFontW(-22, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    HFONT old = (HFONT)SelectObject(hdc, f);
    int nw = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (nw > 0) {
        std::wstring wbuf((size_t)nw, 0);
        MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wbuf.data(), nw);
        SIZE sz{};
        GetTextExtentPoint32W(hdc, wbuf.c_str(), (int)wcslen(wbuf.c_str()), &sz);
        int tx = r.left + ((r.right - r.left) - (int)sz.cx) / 2;
        int ty = r.top + ((r.bottom - r.top) - (int)sz.cy) / 2;
        SetTextColor(hdc, RGB(248, 248, 252));
        TextOutW(hdc, tx, ty, wbuf.c_str(), (int)wcslen(wbuf.c_str()));
    }
    SelectObject(hdc, old);
    DeleteObject(f);
}

void DrawMainMenu(HDC hdc, Game& g, int mx, int my) {
    RECT full{0, 0, kScreenW, kScreenH};
    HBRUSH bg = CreateSolidBrush(RGB(32, 36, 52));
    FillRect(hdc, &full, bg);
    DeleteObject(bg);

    DrawUtf8Centered(hdc, kScreenW / 2, 72, 40, RGB(255, 214, 90), u8"我才是奶龙");

    int nItems = g.mainMenuPickLevel ? 4 : 3;
    for (int i = 0; i < nItems; ++i) {
        RECT r{};
        MainMenuGetButtonRect(g, i, &r);
        POINT pt{(LONG)mx, (LONG)my};
        bool hover = PtInRect(&r, pt);
        bool hot = hover || g.mainMenuSel == i;
        if (!g.mainMenuPickLevel) {
            const char* labels[] = {u8"开始游戏", u8"选择关卡", u8"结束游戏"};
            DrawMenuPanelButton(hdc, r, labels[i], hot);
        } else {
            const char* labels[] = {u8"第一关", u8"第二关", u8"第三关", u8"返回"};
            DrawMenuPanelButton(hdc, r, labels[i], hot);
        }
    }

    DrawUtf8(hdc, 16, kScreenH - 28, 15, RGB(120, 125, 140),
             u8"鼠标点击，或方向键选择 + Enter    F11 全屏    Esc：返回上一级菜单");
}

// —— 主菜单：从菜单进入指定关卡 ——
void StartPlayingAt(Game& g, int levelIndex) {
    if (levelIndex < 0) levelIndex = 0;
    if (levelIndex > 2) levelIndex = 2;
    g.currentLevelIndex = levelIndex;
    g.mainMenuPickLevel = false;
    g.mainMenuSel = 0;
    ResetLevel(g);
}

void MainMenuHandleInput(Game& g, int mx, int my, bool lmbClick, bool keyUp, bool keyDown, bool keyEnter, bool keyEsc,
                         bool prevUp, bool prevDown, bool prevEnter, bool prevEsc) {
    int nItems = g.mainMenuPickLevel ? 4 : 3;

    for (int i = 0; i < nItems; ++i) {
        RECT r{};
        MainMenuGetButtonRect(g, i, &r);
        POINT pt{(LONG)mx, (LONG)my};
        if (PtInRect(&r, pt)) g.mainMenuSel = i;
    }

    if (keyUp && !prevUp) g.mainMenuSel = (g.mainMenuSel + nItems - 1) % nItems;
    if (keyDown && !prevDown) g.mainMenuSel = (g.mainMenuSel + 1) % nItems;

    if (keyEsc && !prevEsc && g.mainMenuPickLevel) {
        g.mainMenuPickLevel = false;
        g.mainMenuSel = 1;
        return;
    }

    bool clickHit = false;
    if (lmbClick) {
        POINT pt{(LONG)mx, (LONG)my};
        for (int i = 0; i < nItems; ++i) {
            RECT r{};
            MainMenuGetButtonRect(g, i, &r);
            if (PtInRect(&r, pt)) {
                g.mainMenuSel = i;
                clickHit = true;
                break;
            }
        }
    }
    bool activate = (keyEnter && !prevEnter) || (lmbClick && clickHit);
    if (!activate) return;

    if (!g.mainMenuPickLevel) {
        if (g.mainMenuSel == 0)
            StartPlayingAt(g, 0);
        else if (g.mainMenuSel == 1) {
            g.mainMenuPickLevel = true;
            g.mainMenuSel = 0;
        } else if (g.mainMenuSel == 2)
            PostQuitMessage(0);
    } else {
        if (g.mainMenuSel == 0)
            StartPlayingAt(g, 0);
        else if (g.mainMenuSel == 1)
            StartPlayingAt(g, 1);
        else if (g.mainMenuSel == 2)
            StartPlayingAt(g, 2);
        else {
            g.mainMenuPickLevel = false;
            g.mainMenuSel = 1;
        }
    }
}

Game gGame;
HWND gHwnd = nullptr;

// 全屏：无边框覆盖当前窗口所在监视器；内部仍以 960×540 离屏渲染，再 StretchBlt
static bool gFullscreen = false;
static DWORD gSavedWinStyle = 0;
static LONG_PTR gSavedWinExStyle = 0;
static WINDOWPLACEMENT gSavedPlacement{sizeof(WINDOWPLACEMENT)};

static void ToggleFullscreen(HWND hwnd) {
    if (!hwnd) return;
    if (!gFullscreen) {
        if (!GetWindowPlacement(hwnd, &gSavedPlacement)) return;
        gSavedWinStyle = (DWORD)GetWindowLong(hwnd, GWL_STYLE);
        gSavedWinExStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
        HMONITOR hmon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{sizeof(MONITORINFO)};
        if (!GetMonitorInfoW(hmon, &mi)) return;
        int mw = mi.rcMonitor.right - mi.rcMonitor.left;
        int mh = mi.rcMonitor.bottom - mi.rcMonitor.top;
        SetWindowLong(hwnd, GWL_STYLE, (LONG)(WS_POPUP | WS_VISIBLE));
        SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mw, mh, SWP_FRAMECHANGED);
        gFullscreen = true;
    } else {
        SetWindowLong(hwnd, GWL_STYLE, (LONG)gSavedWinStyle);
        SetWindowLongPtr(hwnd, GWL_EXSTYLE, gSavedWinExStyle);
        SetWindowPlacement(hwnd, &gSavedPlacement);
        gFullscreen = false;
    }
}

static HDC gFrameMemDc = nullptr;
static HBITMAP gFrameMemBmp = nullptr;
static HBITMAP gFrameMemOldBmp = nullptr;

static void DestroyFrameBackbuffer() {
    if (gFrameMemDc) {
        if (gFrameMemBmp) {
            SelectObject(gFrameMemDc, gFrameMemOldBmp);
            DeleteObject(gFrameMemBmp);
            gFrameMemBmp = nullptr;
            gFrameMemOldBmp = nullptr;
        }
        DeleteDC(gFrameMemDc);
        gFrameMemDc = nullptr;
    }
}

// —— 胜/负/通关，窗口中的图片与文字提示
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
    wchar_t lineText[128]{};
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
                              56.0f);
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
                        init.windowTitle ? init.windowTitle : L"我才是奶龙", WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y,
                        ww, wh, owner, nullptr, hi, reinterpret_cast<LPVOID>(&initCopy));
    if (!hDlg)
        return;
    RunFailModal(hDlg, owner);
}

void ShowSuccessTauntDialog(HWND owner) {
    EndDlgInit init{};
    init.windowTitle = L"我才是奶龙 — 夯爆了";
    init.pngRelative = L"assets\\win_taunt.png";
    init.lineText = L"夯爆了";
    init.lineArgb = Gdiplus::Color(255, 20, 130, 55).GetValue();
    init.fillArgb = Gdiplus::Color(255, 242, 255, 248).GetValue();
    ShowEndTauntDialog(owner, init);
}

void ShowFailureTauntDialog(HWND owner) {
    EndDlgInit init{};
    init.windowTitle = L"我才是奶龙 — 拉完了";
    init.pngRelative = L"assets\\fail_taunt.png";
    init.lineText = L"拉完了";
    init.lineArgb = Gdiplus::Color(255, 55, 45, 50).GetValue();
    init.fillArgb = Gdiplus::Color(255, 250, 250, 252).GetValue();
    ShowEndTauntDialog(owner, init);
}

void ShowTrueNailongCelebrationDialog(HWND owner) {
    EndDlgInit init{};
    init.windowTitle = L"我才是奶龙 — 通关";
    init.pngRelative = L"assets\\win_true_nailong.png";
    init.lineText = L"恭喜！你已成为真正的奶龙！";
    init.lineArgb = Gdiplus::Color(255, 220, 30, 30).GetValue();
    init.fillArgb = Gdiplus::Color(255, 255, 252, 248).GetValue();
    ShowEndTauntDialog(owner, init);
}
void GameFrame() {
    static LARGE_INTEGER freq{};
    static LARGE_INTEGER prev{};
    static bool timeInit = false;
    static bool prevEsc = false, prevEnter = false, prevR = false, prevN = false, prevQ = false, prevSpace = false;
    static bool prevLmb = false;
    static bool prevUp = false, prevDown = false;
    static bool prevF11 = false;

    if (!timeInit) {
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&prev);
        timeInit = true;
    }
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    float dt = (float)(now.QuadPart - prev.QuadPart) / (float)freq.QuadPart;
    prev = now;
    if (dt > 0.02f) dt = 0.02f;
    if (dt < 0.001f) dt = 0.001f;

    int clientW = kScreenW, clientH = kScreenH;
    if (gHwnd) {
        RECT cr{};
        GetClientRect(gHwnd, &cr);
        clientW = (std::max)(1, (int)(cr.right - cr.left));
        clientH = (std::max)(1, (int)(cr.bottom - cr.top));
    }
    int clientMx = 0, clientMy = 0;
    bool haveClientPt = false;
    if (gHwnd) {
        POINT cpt{};
        if (GetCursorPos(&cpt) && ScreenToClient(gHwnd, &cpt)) {
            clientMx = (int)cpt.x;
            clientMy = (int)cpt.y;
            haveClientPt = true;
        }
    }
    float logMx = (float)clientMx * (float)kScreenW / (float)clientW;
    float logMy = (float)clientMy * (float)kScreenH / (float)clientH;
    int logMxI = (int)std::lround(logMx);
    int logMyI = (int)std::lround(logMy);

    bool keyA = (GetAsyncKeyState('A') & 0x8000) != 0;
    bool keyD = (GetAsyncKeyState('D') & 0x8000) != 0;
    bool spaceDown = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
    bool keySpaceEdge = spaceDown && !prevSpace;
    bool lmbDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    bool lmbClick = lmbDown && !prevLmb;
    bool esc = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    bool enter = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
    bool r = (GetAsyncKeyState('R') & 0x8000) != 0;
    bool n = (GetAsyncKeyState('N') & 0x8000) != 0;
    bool q = (GetAsyncKeyState('Q') & 0x8000) != 0;
    bool keyUp = (GetAsyncKeyState(VK_UP) & 0x8000) != 0;
    bool keyDown = (GetAsyncKeyState(VK_DOWN) & 0x8000) != 0;
    bool f11 = (GetAsyncKeyState(VK_F11) & 0x8000) != 0;

    float camX = 0.0f, camY = 0.0f;
    if (gGame.state != GameState::MainMenu) {
        camX = gGame.playerPos.x;
        camY = gGame.playerPos.y;
        if (gGame.currentLevelIndex == 1 && gGame.level2DoorCellX >= 0) {
            float doorCx = (float)gGame.level2DoorCellX * (float)TILE + (float)TILE * 0.5f;
            camX = (gGame.playerPos.x + doorCx) * 0.5f;
        } else if (gGame.currentLevelIndex == 2) {
            camX = (gGame.playerPos.x + gGame.level3BossX) * 0.5f;
            camY = (gGame.playerPos.y + gGame.level3BossY) * 0.5f;
        }
    }
    if (haveClientPt && gGame.state == GameState::Playing && gGame.currentLevelIndex == 2) {
        gGame.level3AimX = logMx - (float)kScreenW * 0.5f + camX;
        gGame.level3AimY = logMy - (float)kScreenH * 0.55f + camY;
    }

    if (gGame.state == GameState::MainMenu) {
        MainMenuHandleInput(gGame, logMxI, logMyI, lmbClick, keyUp, keyDown, enter, esc, prevUp, prevDown,
                            prevEnter, prevEsc);
    } else if (gGame.state == GameState::Playing) {
        if (esc && !prevEsc) gGame.state = GameState::SettingsOverlay;
        PhysicsStep(gGame, dt, keyA, keyD, keySpaceEdge, lmbDown, lmbClick);
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
    prevLmb = lmbDown;
    prevUp = keyUp;
    prevDown = keyDown;
    {
        bool f11Edge = f11 && !prevF11;
        if (f11Edge && gHwnd) ToggleFullscreen(gHwnd);
        prevF11 = f11;
    }
    static GameState prevEndMsgState = GameState::MainMenu;
    if (gHwnd && gGame.state != prevEndMsgState) {
        if (gGame.state == GameState::Won)
            PostMessageW(gHwnd, WM_APP, 1, 0);
        else if (gGame.state == GameState::Lost)
            PostMessageW(gHwnd, WM_APP, 2, 0);
        prevEndMsgState = gGame.state;
    }

    HDC hdcScr = GetDC(gHwnd);
    if (!gFrameMemDc && hdcScr) {
        gFrameMemDc = CreateCompatibleDC(hdcScr);
        if (gFrameMemDc) {
            gFrameMemBmp = CreateCompatibleBitmap(hdcScr, kScreenW, kScreenH);
            if (!gFrameMemBmp) {
                DeleteDC(gFrameMemDc);
                gFrameMemDc = nullptr;
            } else {
                gFrameMemOldBmp = (HBITMAP)SelectObject(gFrameMemDc, gFrameMemBmp);
            }
        }
    }
    HDC hdcMem = gFrameMemDc;
    if (!hdcScr || !hdcMem) {
        if (hdcScr) ReleaseDC(gHwnd, hdcScr);
        return;
    }

    RECT bg{0, 0, kScreenW, kScreenH};
    HBRUSH bgb = CreateSolidBrush(RGB(28, 30, 38));
    FillRect(hdcMem, &bg, bgb);
    DeleteObject(bgb);

    if (gGame.state == GameState::MainMenu) {
        DrawMainMenu(hdcMem, gGame, clientMx, clientMy);
    } else {
        float cx = camX, cy = camY;
        DrawLevel(hdcMem, gGame.levels[gGame.currentLevelIndex], cx, cy);
        DrawLevel3Extra(hdcMem, gGame, cx, cy);
        DrawNailongBeam(hdcMem, gGame, cx, cy);
        DrawPixelDude(hdcMem, gGame.playerPos.x, gGame.playerPos.y, gGame.facingRight, cx, cy, RGB(90, 140, 220),
                      gGame.currentLevelIndex);

        DrawUi(hdcMem, gGame);

        if (gGame.state == GameState::SettingsOverlay) DrawSettingsOverlay(hdcMem);
    }

    SetStretchBltMode(hdcScr, HALFTONE);
    StretchBlt(hdcScr, 0, 0, clientW, clientH, hdcMem, 0, 0, kScreenW, kScreenH, SRCCOPY);

    if (gHwnd) ValidateRect(gHwnd, nullptr);

    ReleaseDC(gHwnd, hdcScr);
}
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_DESTROY:
            DestroyFrameBackbuffer();
            KillTimer(hwnd, 1);
            PostQuitMessage(0);
            return 0;
        case WM_TIMER:
            if (wParam == 1) GameFrame();
            return 0;
        case WM_APP:
            if (wParam == 1) {
                if (gGame.currentLevelIndex == 2)
                    ShowTrueNailongCelebrationDialog(hwnd);
                else
                    ShowSuccessTauntDialog(hwnd);
                if (gGame.currentLevelIndex < 2) {
                    gGame.currentLevelIndex++;
                    ResetLevel(gGame);
                } else {
                    gGame.state = GameState::MainMenu;
                    gGame.mainMenuPickLevel = false;
                    gGame.mainMenuSel = 0;
                }
            } else if (wParam == 2) {
                ShowFailureTauntDialog(hwnd);
                ResetLevel(gGame);
            }
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

} 

// 程序入口： 初始化、预构建三关 
int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    Gdiplus::GdiplusStartupInput gdpsi{};
    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdpsi, nullptr);
    std::srand((unsigned int)GetTickCount());

    gGame.levels[0] = MakeLevelFromString(kLevel1);
    gGame.levels[1] = MakeLevelFromString(kLevel2);
    gGame.levels[2] = MakeLevelFromString(kLevel3);
    gGame.state = GameState::MainMenu;
    gGame.mainMenuPickLevel = false;
    gGame.mainMenuSel = 0;

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

    gHwnd = CreateWindowExW(0, L"PixelEscapeWnd", L"我才是奶龙", WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
                            CW_USEDEFAULT, CW_USEDEFAULT, ww, wh, nullptr, nullptr, hInst, nullptr);
    ShowWindow(gHwnd, SW_SHOW);
    UpdateWindow(gHwnd);
    GameFrame();

    SetTimer(gHwnd, 1, 10, nullptr);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    Gdiplus::GdiplusShutdown(gdiplusToken);
    return (int)msg.wParam;
}
