// AC's Mario Fan Builder — C++ / SDL2 single-file engine port
// (C) 1999-2026 A.C Holdings | (C) 1999-2026 Redigit | (C) 1985-2026 Nintendo
//
// Build (macOS):
//   clang++ -std=c++17 -O2 mfb.cpp -o mfb \
//     $(sdl2-config --cflags --libs) -lSDL2_ttf
// Build (Linux):
//   g++ -std=c++17 -O2 mfb.cpp -o mfb -lSDL2 -lSDL2_ttf -lm
//   (some distros need also: -lstdc++fs for std::filesystem)
// Build (Windows/MSYS2):
//   g++ -std=c++17 -O2 mfb.cpp -o mfb.exe -lSDL2main -lSDL2 -lSDL2_ttf
//
// Single-file; SDL2 + SDL2_ttf. Save: SMBX .lvl, LunaLua .38a (ZIP+level.lvl),
// Moondust / PGE .lvlx (UTF-8 XML). JSON export included.

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cctype>
#include <ctime>
#include <string>
#include <vector>
#include <deque>
#include <unordered_map>
#include <functional>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <random>
#include <iterator>
#include <filesystem>

// ---------- Constants ----------
static const int WIN_W=1024, WIN_H=700;
static const int SIDEBAR_W=200, MENU_H=22, TOOL_H=28, STATUS_H=24;
static const int CANVAS_X=SIDEBAR_W, CANVAS_Y=MENU_H+TOOL_H;
static const int CANVAS_W=WIN_W-SIDEBAR_W, CANVAS_H=WIN_H-CANVAS_Y-STATUS_H;
static const int GRID=32, FPS=60;
static const float GRAVITY=0.5f, JUMP_STR=-10.f, MOVE_SPD=4.f, VMAX=10.f;

struct Col { Uint8 r,g,b,a=255; };
#define C(r,g,b) Col{(Uint8)r,(Uint8)g,(Uint8)b,255}
static const Col BG_DARK=C(40,40,40),
    MD_DOCK=C(48,50,55), MD_DEEP=C(34,36,40),
    BTN_FACE=C(56,58,60), BTN_LIGHT=C(86,88,91),
    BTN_DARK=C(36,37,38), BTN_SHD=C(22,22,23), WINDOW_C=C(62,64,70),
    HL=C(200,118,42), HL2=C(236,168,72), TEXT_C=C(210,210,210),
    WHITE_C=C(255,255,255), BLACK_C=C(0,0,0), RED_C=C(255,0,0), GREEN_C=C(0,200,0),
    YELLOW_C=C(255,255,0), GRAY_C=C(128,128,128), GRID_C=C(72,74,78);

// ---------- Globals ----------
SDL_Window* gWin=nullptr;  SDL_Renderer* gRen=nullptr;
TTF_Font* gFont=nullptr;   TTF_Font* gFontSmall=nullptr;  TTF_Font* gFontTitle=nullptr;
std::string gCurrentFile;  std::string gStatusMsg="Ready";
int gMouseX=0, gMouseY=0;
bool gRunning=true;
// Modal: 0=none, 1=message (About), 2=Save As, 3=Open, 4=New confirm
static int gModal = 0;
static std::string gMsgBoxTitle;
static std::string gMsgBoxBody;

static std::string gDlgFileName;
static int gDlgFilter = 0;
static int gDlgCaret = 0;
static int gDlgHover = -1;
static int gPasteGX = 0, gPasteGY = 0;

// ---------- Draw helpers ----------
static inline void SetCol(Col c){ SDL_SetRenderDrawColor(gRen,c.r,c.g,c.b,c.a); }
static void FillR(SDL_Rect r, Col c){ SetCol(c); SDL_RenderFillRect(gRen,&r); }
static void DrawR(SDL_Rect r, Col c){ SetCol(c); SDL_RenderDrawRect(gRen,&r); }
static void Line(int x1,int y1,int x2,int y2, Col c){ SetCol(c); SDL_RenderDrawLine(gRen,x1,y1,x2,y2); }

static void DrawEdge(SDL_Rect r, bool raised){
    Col tl = raised?BTN_LIGHT:BTN_SHD, br = raised?BTN_SHD:BTN_LIGHT;
    Line(r.x,r.y,r.x+r.w-1,r.y,tl); Line(r.x,r.y,r.x,r.y+r.h-1,tl);
    Line(r.x,r.y+r.h-1,r.x+r.w-1,r.y+r.h-1,br); Line(r.x+r.w-1,r.y,r.x+r.w-1,r.y+r.h-1,br);
}

static void DrawText(const std::string& s, int x, int y, Col c=TEXT_C, TTF_Font* f=nullptr, bool center=false){
    if(!f) f=gFontSmall; if(!f||s.empty()) return;
    SDL_Color sc={c.r,c.g,c.b,255};
    SDL_Surface* surf=TTF_RenderUTF8_Blended(f,s.c_str(),sc); if(!surf) return;
    SDL_Texture* tex=SDL_CreateTextureFromSurface(gRen,surf);
    SDL_Rect d={x,y,surf->w,surf->h};
    if(center){ d.x=x-surf->w/2; d.y=y-surf->h/2; }
    SDL_RenderCopy(gRen,tex,nullptr,&d); SDL_DestroyTexture(tex); SDL_FreeSurface(surf);
}

static void FillCircle(int cx,int cy,int rad, Col c){
    SetCol(c);
    for(int dy=-rad; dy<=rad; ++dy){
        int dx=(int)std::sqrt((float)(rad*rad-dy*dy));
        SDL_RenderDrawLine(gRen,cx-dx,cy+dy,cx+dx,cy+dy);
    }
}

// ---------- Object types ----------
enum Kind { K_TILE, K_BGO, K_NPC };

struct TileDef { std::string name; int smbxId; Col color; bool solid; };
static const std::vector<TileDef> TILES = {
    {"ground",1,C(0,128,0),true},{"grass",2,C(60,180,60),true},{"sand",3,C(220,200,100),true},
    {"dirt",4,C(150,100,60),true},{"brick",45,C(180,80,40),true},{"question",34,C(255,200,0),true},
    {"pipe_v",112,C(0,200,0),true},{"pipe_h",113,C(0,180,0),true},{"platform",159,C(139,69,19),true},
    {"coin",10,C(255,255,0),false},{"stone",48,C(140,140,140),true},{"ice",55,C(160,220,255),true},
    {"slope_l",182,C(180,180,0),true},{"slope_r",183,C(180,180,0),true},
    {"water",196,C(0,100,255),false},{"lava",197,C(255,80,0),false},{"semisolid",190,C(150,150,200),true},
};
struct BgoDef { std::string name; int smbxId; Col color; };
static const std::vector<BgoDef> BGOS = {
    {"cloud",5,C(220,220,220)},{"bush",6,C(0,160,0)},{"hill",7,C(100,200,100)},
    {"tree",10,C(0,120,0)},{"fence",8,C(150,120,80)},
};
struct NpcDef { std::string name; int smbxId; Col color; };
static const std::vector<NpcDef> NPCS = {
    {"goomba",1,C(200,100,0)},{"koopa_g",2,C(0,200,50)},{"koopa_r",3,C(200,50,50)},
    {"piranha",6,C(0,160,0)},{"mushroom",9,C(255,0,200)},{"flower",10,C(255,140,0)},
    {"star",11,C(255,230,0)},{"cheep",15,C(255,150,150)},{"bowser",18,C(200,100,0)},
};

// ---------- Data model ----------
struct Obj {
    Kind kind; int typeIdx; int x,y; int layer; int eventId=-1; int flags=0;
    bool solid() const { return kind==K_TILE && TILES[typeIdx].solid; }
    Col color() const {
        if(kind==K_TILE) return TILES[typeIdx].color;
        if(kind==K_BGO)  return BGOS[typeIdx].color;
        return NPCS[typeIdx].color;
    }
    std::string name() const {
        if(kind==K_TILE) return TILES[typeIdx].name;
        if(kind==K_BGO)  return BGOS[typeIdx].name;
        return NPCS[typeIdx].name;
    }
};

struct SelTag { int lay, idx; };
static std::vector<SelTag> gSelection;
static std::vector<Obj> gClipboard;

struct Layer { std::string name; bool visible=true, locked=false; std::vector<Obj> objs; };
struct Section {
    int width=100*GRID, height=30*GRID;
    std::vector<Layer> layers;
    int currentLayer=0;
    Col bgColor=C(92,148,252);
    int startX=100, startY=500;
    int music=1;
    Section(){ layers.push_back({"Default",true,false,{}}); layers.push_back({"Background",true,false,{}}); }
    Layer& cur(){ return layers[currentLayer]; }
};
struct Level {
    std::vector<Section> sections;
    int curSec=0;
    std::string name="Untitled", author="Unknown", description;
    int timeLimit=400, stars=0, initialLives=3;
    bool noBackground=false;
    Level(){ sections.emplace_back(); }
    Section& cur(){ return sections[curSec]; }
};

static Level gLevel;

// ---------- Menu bar (Moondust-style dark dropdowns) ----------
struct MenuEntry { std::string label; std::function<void()> onPick; };
static std::vector<std::vector<MenuEntry>> gMenus;
static SDL_Rect gMenuTabRect[6];
static int gOpenMenu = -1;      // which top-level menu is open, or -1
static int gMenuTabHover = -1;  // hovered top tab, or -1
static int gMenuRowHover = -1;  // hovered row inside open dropdown, or -1
static int gMenuBarEndX = 0;    // right edge of last top-level menu tab (for title placement)

static void LayoutMenuTabs(){
    const char* labs[] = {"File", "Edit", "View", "Tools", "Test", "Help"};
    int x = 8;
    for(int i = 0; i < 6; ++i){
        int tw = 0, th = 0;
        if(gFont) TTF_SizeUTF8(gFont, labs[i], &tw, &th);
        else if(gFontSmall) TTF_SizeUTF8(gFontSmall, labs[i], &tw, &th);
        tw = std::max(tw, 32);
        gMenuTabRect[i] = {x, 0, tw + 18, MENU_H};
        x += gMenuTabRect[i].w;
    }
    gMenuBarEndX = x;
}

static SDL_Rect MenuDropdownRect(int mi){
    int x = gMenuTabRect[mi].x;
    int y = MENU_H;
    int w = 148;
    for(const auto& e : gMenus[mi]){
        int tw = 0, th = 0;
        if(gFontSmall) TTF_SizeUTF8(gFontSmall, e.label.c_str(), &tw, &th);
        w = std::max(w, tw + 24);
    }
    int h = (int)gMenus[mi].size() * 22 + 8;
    return {x, y, w, h};
}

static int MenuDropdownHit(int mi, int px, int py){
    if(mi < 0 || mi >= (int)gMenus.size()) return -1;
    SDL_Rect d = MenuDropdownRect(mi);
    SDL_Point p{px, py};
    if(!SDL_PointInRect(&p, &d)) return -1;
    if(px < d.x + 6) return -1;
    int row = (py - d.y - 4) / 22;
    if(row < 0 || row >= (int)gMenus[mi].size()) return -1;
    return row;
}

static void OpenAboutMsgBox();
static void OpenNewConfirmModal();
static void CmdSave();
static void CmdExportJson();
static void CmdZoomIn();
static void CmdZoomOut();
static void CmdUndo();
static void CmdRedo();
static void CmdPlaytest();
static void InitMenus();

// ---------- Undo ----------
struct UndoStep { std::function<void()> undo, redo; };
static std::vector<UndoStep> gUndo, gRedo;
static void PushUndo(UndoStep s){ gUndo.push_back(std::move(s)); gRedo.clear(); }

// ---------- Camera ----------
struct Camera { int x=0,y=0; float zoom=1.0f; } gCam;

// ---------- Drawing sprites (procedural like Python version) ----------
static void DrawObj(const Obj& o, int ox, int oy, float z){
    int w=(int)(GRID*z), h=(int)(GRID*z);
    SDL_Rect r={ox+(int)(o.x*z), oy+(int)(o.y*z), w, h};
    Col c=o.color();
    if(o.kind==K_TILE){
        const auto& td=TILES[o.typeIdx];
        if(td.name=="coin"){
            FillCircle(r.x+w/2, r.y+h/2, w/3, YELLOW_C);
            FillCircle(r.x+w/2, r.y+h/2, w/3-2, C(255,200,0));
        } else if(td.name=="question"){
            FillR(r,C(255,200,0));
            SDL_Rect in={r.x+2,r.y+2,r.w-4,r.h-4}; DrawR(in,C(180,120,0));
            DrawText("?", r.x+w/2, r.y+h/2, BLACK_C, gFontSmall, true);
        } else if(td.name=="slope_l" || td.name=="slope_r"){
            SetCol(c);
            // draw as filled triangle via scanline
            for(int yy=0; yy<h; ++yy){
                int len = td.name=="slope_l" ? (h-yy) : yy;
                int sx = td.name=="slope_l" ? r.x : r.x+(w-len);
                SDL_RenderDrawLine(gRen, sx, r.y+yy, sx+len, r.y+yy);
            }
        } else if(td.name=="water"){
            FillR(r, C(0,100,255));
        } else if(td.name=="lava"){
            FillR(r, C(255,80,0));
            for(int i=0;i<w;i+=8) Line(r.x+i,r.y+h-4,r.x+i+4,r.y+h-8,C(255,200,0));
        } else if(td.name=="brick"){
            FillR(r,c);
            for(int row=1;row<4;++row) Line(r.x,r.y+row*(h/4),r.x+w,r.y+row*(h/4),C(100,50,20));
            for(int xx=w/2;xx<w;xx+=w) Line(r.x+xx,r.y,r.x+xx,r.y+h,C(100,50,20));
        } else if(td.name=="ground"){
            FillR(r,c);
            SDL_Rect top={r.x,r.y,r.w,std::max(2,h/8)}; FillR(top,C(0,100,0));
        } else {
            FillR(r,c);
        }
        DrawR(r,C(0,0,0));
    } else if(o.kind==K_BGO){
        const auto& bd=BGOS[o.typeIdx];
        if(bd.name=="cloud"){ FillCircle(r.x+w/2,r.y+h/2,w/2-2,c); }
        else if(bd.name=="hill"){
            SetCol(c);
            for(int yy=0;yy<h;++yy){ int len=(int)(((float)yy/h)*w); SDL_RenderDrawLine(gRen,r.x+w/2-len/2,r.y+h-1-yy,r.x+w/2+len/2,r.y+h-1-yy); }
        }
        else if(bd.name=="tree"){ FillR({r.x+w/2-3,r.y+h-10,6,10},C(100,60,20)); FillCircle(r.x+w/2,r.y+h-12,10,c); }
        else { SDL_Rect in={r.x+4,r.y+4,w-8,h-8}; FillR(in,c); }
    } else { // NPC
        const auto& nd=NPCS[o.typeIdx];
        if(nd.name=="goomba"){
            FillCircle(r.x+w/2,r.y+h/2,w/2-4,c);
            FillR({r.x+4,r.y+h-8,w-8,4},C(100,50,0));
            FillCircle(r.x+w/3,r.y+h/3,3,WHITE_C); FillCircle(r.x+2*w/3,r.y+h/3,3,WHITE_C);
            FillCircle(r.x+w/3,r.y+h/3,1,BLACK_C); FillCircle(r.x+2*w/3,r.y+h/3,1,BLACK_C);
        } else if(nd.name=="star"){
            FillCircle(r.x+w/2,r.y+h/2,w/2-2,YELLOW_C);
        } else if(nd.name=="mushroom"){
            FillCircle(r.x+w/2,r.y+h/3,w/3,C(255,0,0));
            FillR({r.x+w/2-4,r.y+h/2,8,h/2},C(200,150,100));
        } else {
            FillR({r.x+4,r.y+4,w-8,h-8}, c);
            FillCircle(r.x+w/3,r.y+8,2,BLACK_C); FillCircle(r.x+2*w/3,r.y+8,2,BLACK_C);
        }
        DrawR(r,C(0,0,0));
    }
}

// ---------- Playtest player ----------
struct Player {
    float x,y, vx=0, vy=0; int dir=1; bool onGround=false, jumpHeld=false;
    int coins=0, score=0, invincible=0, jumpTimer=0;
    int startX, startY;
    Player(int sx,int sy): x((float)sx), y((float)sy), startX(sx), startY(sy) {}
    SDL_Rect rect() const { return {(int)x,(int)y,GRID,GRID}; }
} ;
static Player* gPlayer=nullptr;
static bool gPlaytest=false;

static std::vector<Obj*> SolidTiles(){
    std::vector<Obj*> out;
    for(auto& L : gLevel.cur().layers) if(L.visible)
        for(auto& o : L.objs) if(o.solid()) out.push_back(&o);
    return out;
}

static bool RectHit(const SDL_Rect& a, const SDL_Rect& b){ return SDL_HasIntersection(&a,&b); }

static void PlayerUpdate(){
    if(!gPlayer) return;
    const Uint8* k = SDL_GetKeyboardState(nullptr);
    gPlayer->vx = 0;
    if(k[SDL_SCANCODE_LEFT]||k[SDL_SCANCODE_A]){ gPlayer->vx=-MOVE_SPD; gPlayer->dir=-1; }
    if(k[SDL_SCANCODE_RIGHT]||k[SDL_SCANCODE_D]){ gPlayer->vx=MOVE_SPD; gPlayer->dir=1; }
    bool jump = k[SDL_SCANCODE_SPACE];
    if(jump){
        if(gPlayer->onGround && !gPlayer->jumpHeld){
            gPlayer->vy=JUMP_STR; gPlayer->onGround=false; gPlayer->jumpHeld=true; gPlayer->jumpTimer=8;
        } else if(gPlayer->jumpTimer>0 && gPlayer->vy<0){
            gPlayer->vy-=0.5f; gPlayer->jumpTimer--;
        }
    } else { gPlayer->jumpHeld=false; gPlayer->jumpTimer=0; }
    gPlayer->vy = std::min(gPlayer->vy+GRAVITY, VMAX);

    auto tiles = SolidTiles();
    // X
    gPlayer->x += gPlayer->vx;
    SDL_Rect pr = gPlayer->rect();
    for(Obj* t : tiles){
        SDL_Rect tr={t->x,t->y,GRID,GRID};
        if(RectHit(pr,tr)){
            if(gPlayer->vx>0) gPlayer->x = tr.x - GRID;
            else if(gPlayer->vx<0) gPlayer->x = tr.x + GRID;
            gPlayer->vx=0; pr = gPlayer->rect();
        }
    }
    // Y
    gPlayer->y += gPlayer->vy; gPlayer->onGround=false;
    pr = gPlayer->rect();
    for(Obj* t : tiles){
        SDL_Rect tr={t->x,t->y,GRID,GRID};
        if(RectHit(pr,tr)){
            if(gPlayer->vy>0){ gPlayer->y = tr.y - GRID; gPlayer->onGround=true; }
            else if(gPlayer->vy<0) gPlayer->y = tr.y + GRID;
            gPlayer->vy=0; pr = gPlayer->rect();
        }
    }
    // Coin pickup
    for(auto& L : gLevel.cur().layers){
        if(!L.visible) continue;
        for(auto it=L.objs.begin(); it!=L.objs.end();){
            if(it->kind==K_TILE && TILES[it->typeIdx].name=="coin"){
                SDL_Rect tr={it->x,it->y,GRID,GRID};
                if(RectHit(pr,tr)){ it=L.objs.erase(it); gPlayer->coins++; gPlayer->score+=10; continue; }
            }
            ++it;
        }
    }
    // NPC stomp
    for(auto& L : gLevel.cur().layers){
        if(!L.visible) continue;
        for(auto it=L.objs.begin(); it!=L.objs.end();){
            if(it->kind==K_NPC){
                SDL_Rect nr={it->x,it->y,GRID,GRID};
                if(RectHit(pr,nr)){
                    if(gPlayer->vy>0 && pr.y+pr.h <= nr.y + GRID/2 + 4){
                        it = L.objs.erase(it);
                        gPlayer->vy = JUMP_STR*0.7f; gPlayer->score+=100; continue;
                    } else if(gPlayer->invincible<=0){
                        gPlayer->x = (float)gPlayer->startX; gPlayer->y = (float)gPlayer->startY;
                        gPlayer->vy=0; gPlayer->invincible=60; gPlayer->coins=0;
                    }
                }
            }
            ++it;
        }
    }
    if(gPlayer->invincible>0) gPlayer->invincible--;

    // Camera follow
    int cw = (int)(CANVAS_W/gCam.zoom), ch=(int)(CANVAS_H/gCam.zoom);
    gCam.x = std::min(0, std::max(-(gLevel.cur().width-cw), -(int)gPlayer->x + cw/2));
    gCam.y = std::min(0, std::max(-(gLevel.cur().height-ch), -(int)gPlayer->y + ch/2));
}

// ---------- Tool state ----------
enum Tool { T_PENCIL, T_ERASER, T_FILL, T_SELECT };
static Tool gTool = T_PENCIL;
static Kind gCat = K_TILE;  // sidebar category
static int  gSel = 0;       // selected index in that category
static bool gGrid=true;
static bool gDragPlace=false, gDragErase=false;

static int CatCount(Kind k){ return k==K_TILE?(int)TILES.size(): k==K_BGO?(int)BGOS.size():(int)NPCS.size(); }
static std::string CatName(Kind k){ return k==K_TILE?"Tiles":k==K_BGO?"BGOs":"NPCs"; }

// ---------- Place / erase / fill ----------
static void PlaceAt(int gx, int gy){
    auto& L = gLevel.cur().cur();
    if(L.locked) return;
    if(gCat==K_TILE){
        for(auto& o : L.objs) if(o.kind==K_TILE && o.x==gx && o.y==gy) return;
    }
    Obj o{gCat,gSel,gx,gy,gLevel.cur().currentLayer,-1,0};
    L.objs.push_back(o);
    int secIdx=gLevel.curSec, layIdx=gLevel.cur().currentLayer;
    PushUndo({
        [=](){ auto& LL=gLevel.sections[secIdx].layers[layIdx]; if(!LL.objs.empty()) LL.objs.pop_back(); },
        [=](){ gLevel.sections[secIdx].layers[layIdx].objs.push_back(o); }
    });
}

static void EraseAt(int wx, int wy){
    auto& L = gLevel.cur().cur();
    if(L.locked) return;
    // prefer NPC > BGO > TILE by pixel hit
    for(int pass=0; pass<3; ++pass){
        Kind want = pass==0?K_NPC:pass==1?K_BGO:K_TILE;
        for(auto it=L.objs.begin(); it!=L.objs.end(); ++it){
            if(it->kind!=want) continue;
            SDL_Rect rr={it->x,it->y,GRID,GRID};
            SDL_Point p{wx,wy};
            if(SDL_PointInRect(&p,&rr)){
                Obj rem=*it; int idx=(int)(it-L.objs.begin());
                L.objs.erase(it);
                int secIdx=gLevel.curSec, layIdx=gLevel.cur().currentLayer;
                PushUndo({
                    [=](){ auto& LL=gLevel.sections[secIdx].layers[layIdx]; LL.objs.insert(LL.objs.begin()+idx, rem); },
                    [=](){ auto& LL=gLevel.sections[secIdx].layers[layIdx]; if(idx<(int)LL.objs.size()) LL.objs.erase(LL.objs.begin()+idx); }
                });
                return;
            }
        }
    }
}

static void FillAt(int sx, int sy){
    auto& L = gLevel.cur().cur();
    if(L.locked || gCat!=K_TILE) return;
    int target = gSel;
    // find what's there
    int oldType=-2;
    for(auto& o: L.objs) if(o.kind==K_TILE && o.x==sx && o.y==sy){ oldType=o.typeIdx; break; }
    if(oldType==target) return;
    std::deque<std::pair<int,int>> q; q.push_back({sx,sy});
    std::unordered_map<long long,bool> seen;
    auto key=[](int x,int y){ return ((long long)x<<32)^(unsigned)y; };
    auto& sec=gLevel.cur();
    while(!q.empty()){
        auto [x,y]=q.front(); q.pop_front();
        if(seen[key(x,y)]) continue;
        seen[key(x,y)]=true;
        if(x<0||y<0||x>=sec.width||y>=sec.height) continue;
        int cur=-2; int curIdx=-1;
        for(int i=0;i<(int)L.objs.size();++i) if(L.objs[i].kind==K_TILE && L.objs[i].x==x && L.objs[i].y==y){ cur=L.objs[i].typeIdx; curIdx=i; break; }
        if(oldType==-2){ if(cur!=-2) continue; }
        else{ if(cur!=oldType) continue; }
        if(curIdx>=0) L.objs.erase(L.objs.begin()+curIdx);
        L.objs.push_back({K_TILE,target,x,y,sec.currentLayer,-1,0});
        q.push_back({x+GRID,y}); q.push_back({x-GRID,y});
        q.push_back({x,y+GRID}); q.push_back({x,y-GRID});
    }
}

// ---------- .lvl I/O (subset, matches Python write_lvl/read_lvl header) ----------
static void WriteU32(std::ofstream& f, uint32_t v){ f.write((char*)&v,4); }
static uint32_t ReadU32(std::istream& f){ uint32_t v=0; f.read((char*)&v,4); return v; }

static bool SaveLvl(const std::string& path){
    std::ofstream f(path, std::ios::binary); if(!f) return false;
    f.write("LVL\x1a",4); WriteU32(f,1);
    char buf[32]={0}; std::strncpy(buf,gLevel.name.c_str(),31); f.write(buf,32);
    std::memset(buf,0,32); std::strncpy(buf,gLevel.author.c_str(),31); f.write(buf,32);
    WriteU32(f,gLevel.timeLimit); WriteU32(f,gLevel.stars);
    WriteU32(f, gLevel.noBackground ? 1u : 0u);
    // pad to 128
    std::vector<char> pad(128 - (4+4+32+32+4+4+4),0); f.write(pad.data(),pad.size());
    WriteU32(f,(uint32_t)gLevel.sections.size());
    for(auto& s : gLevel.sections){
        WriteU32(f,s.width); WriteU32(f,s.height);
        char rgb[4]={(char)s.bgColor.r,(char)s.bgColor.g,(char)s.bgColor.b,0}; f.write(rgb,4);
        WriteU32(f,s.startX); WriteU32(f,s.startY); WriteU32(f,s.music);
        std::vector<Obj> tiles, bgos, npcs;
        for(int li=0; li<(int)s.layers.size(); ++li){
            for(auto o: s.layers[li].objs){ o.layer=li;
                if(o.kind==K_TILE) tiles.push_back(o);
                else if(o.kind==K_BGO) bgos.push_back(o);
                else npcs.push_back(o);
            }
        }
        WriteU32(f,(uint32_t)tiles.size());
        for(auto& o: tiles){ WriteU32(f,o.x); WriteU32(f,o.y); WriteU32(f,TILES[o.typeIdx].smbxId); WriteU32(f,o.layer); WriteU32(f,(uint32_t)o.eventId); WriteU32(f,o.flags); }
        WriteU32(f,(uint32_t)bgos.size());
        for(auto& o: bgos){ WriteU32(f,o.x); WriteU32(f,o.y); WriteU32(f,BGOS[o.typeIdx].smbxId); WriteU32(f,o.layer); WriteU32(f,o.flags); }
        WriteU32(f,(uint32_t)npcs.size());
        for(auto& o: npcs){ WriteU32(f,o.x); WriteU32(f,o.y); WriteU32(f,NPCS[o.typeIdx].smbxId); WriteU32(f,o.layer); WriteU32(f,(uint32_t)o.eventId); WriteU32(f,o.flags); WriteU32(f,1); WriteU32(f,0); }
        WriteU32(f,0); WriteU32(f,0); // warps, events
    }
    return true;
}

static bool SaveJson(const std::string& path){
    std::ofstream f(path); if(!f) return false;
    f << "{\n  \"name\":\""<<gLevel.name<<"\",\n  \"author\":\""<<gLevel.author<<"\",\n  \"objects\":[\n";
    bool first=true;
    for(int li=0; li<(int)gLevel.cur().layers.size(); ++li){
        for(auto& o: gLevel.cur().layers[li].objs){
            if(!first) f<<",\n"; first=false;
            f<<"    {\"kind\":"<<(int)o.kind<<",\"type\":\""<<o.name()<<"\",\"x\":"<<o.x<<",\"y\":"<<o.y<<",\"layer\":"<<li<<"}";
        }
    }
    f<<"\n  ]\n}\n"; return true;
}

namespace fs = std::filesystem;

static std::string LowerExt(const std::string& path){
    size_t dot = path.rfind('.');
    if(dot == std::string::npos) return "";
    std::string e = path.substr(dot);
    for(char& c : e) c = (char)std::tolower((unsigned char)c);
    return e;
}

static std::string BasenameNoExt(){
    if(gCurrentFile.empty()) return "level";
    fs::path p(gCurrentFile);
    std::string stem = p.stem().string();
    if(stem.empty()) return "level";
    return stem;
}

static uint32_t gCrcTab[256];
static void Crc32Init(){
    static bool done = false;
    if(done) return;
    done = true;
    for(unsigned i = 0; i < 256; ++i){
        uint32_t c = i;
        for(int k = 0; k < 8; ++k) c = (c & 1) ? (0xedb88320u ^ (c >> 1)) : (c >> 1);
        gCrcTab[i] = c;
    }
}

static uint32_t Crc32Bytes(const uint8_t* data, size_t len){
    Crc32Init();
    uint32_t c = 0xffffffffu;
    for(size_t i = 0; i < len; ++i) c = gCrcTab[(c ^ data[i]) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}

static void ZipW16(std::ofstream& o, uint16_t v){ o.write((const char*)&v, 2); }
static void ZipW32(std::ofstream& o, uint32_t v){ o.write((const char*)&v, 4); }

// One entry, STORED (compression 0), compatible with LunaLua .38a (expects level.lvl).
static bool WriteZipStoreOneFile(const std::string& zipPath, const std::string& innerName, const std::vector<uint8_t>& body){
    std::ofstream out(zipPath, std::ios::binary | std::ios::trunc);
    if(!out) return false;
    const uint32_t kLocalSig = 0x04034b50u;
    const uint32_t kCentralSig = 0x02014b50u;
    const uint32_t kEndSig = 0x06054b50u;

    uint32_t crc = Crc32Bytes(body.data(), body.size());
    uint32_t sz = (uint32_t)body.size();
    uint16_t fnLen = (uint16_t)innerName.size();

    uint32_t localOff = (uint32_t)out.tellp();
    ZipW32(out, kLocalSig);
    ZipW16(out, 20);
    ZipW16(out, 0);
    ZipW16(out, 0);
    ZipW16(out, 0);
    ZipW16(out, 0);
    ZipW16(out, 0);
    ZipW32(out, crc);
    ZipW32(out, sz);
    ZipW32(out, sz);
    ZipW16(out, fnLen);
    ZipW16(out, 0);
    out.write(innerName.data(), fnLen);
    out.write((const char*)body.data(), body.size());

    uint32_t centralOff = (uint32_t)out.tellp();
    ZipW32(out, kCentralSig);
    ZipW16(out, 20);
    ZipW16(out, 20);
    ZipW16(out, 0);
    ZipW16(out, 0);
    ZipW16(out, 0);
    ZipW16(out, 0);
    ZipW16(out, 0);
    ZipW32(out, crc);
    ZipW32(out, sz);
    ZipW32(out, sz);
    ZipW16(out, fnLen);
    ZipW16(out, 0);
    ZipW16(out, 0);
    ZipW16(out, 0);
    ZipW16(out, 0);
    ZipW32(out, 0);
    ZipW32(out, localOff);
    out.write(innerName.data(), fnLen);

    uint32_t centralSize = (uint32_t)((uint64_t)out.tellp() - (uint64_t)centralOff);
    uint32_t endOff = (uint32_t)out.tellp();
    (void)endOff;
    ZipW32(out, kEndSig);
    ZipW16(out, 0);
    ZipW16(out, 0);
    ZipW16(out, 1);
    ZipW16(out, 1);
    ZipW32(out, centralSize);
    ZipW32(out, centralOff);
    ZipW16(out, 0);
    return (bool)out;
}

static bool Save38a(const std::string& path){
    fs::path tmp = fs::temp_directory_path() / ("mfb38a_" + std::to_string((unsigned)std::time(nullptr)) + ".lvl");
    std::string tmpStr = tmp.string();
    if(!SaveLvl(tmpStr)) return false;
    std::ifstream in(tmpStr, std::ios::binary);
    if(!in) return false;
    std::vector<uint8_t> body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    std::error_code ec;
    fs::remove(tmp, ec);
    return WriteZipStoreOneFile(path, "level.lvl", body);
}

static std::string XmlEsc(const std::string& s){
    std::string o;
    o.reserve(s.size() + 8);
    for(unsigned char uc : s){
        char c = (char)uc;
        if(c == '&') o += "&amp;";
        else if(c == '<') o += "&lt;";
        else if(c == '>') o += "&gt;";
        else if(c == '\"') o += "&quot;";
        else o += c;
    }
    return o;
}

static bool SaveLvlx(const std::string& path){
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if(!out) return false;
    int sx = gLevel.cur().startX, sy = gLevel.cur().startY;
    std::ostringstream x;
    x << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    x << "<root type=\"LevelFile\" fileformat=\"LVLX\" format_version=\"67\">\n";
    x << "  <head>\n";
    x << "    <title>" << XmlEsc(gLevel.name) << "</title>\n";
    x << "    <author>" << XmlEsc(gLevel.author) << "</author>\n";
    x << "    <timer value=\"" << gLevel.timeLimit << "\"/>\n";
    x << "    <stars value=\"" << gLevel.stars << "\"/>\n";
    x << "  </head>\n";
    x << "  <player_point x=\"" << sx << "\" y=\"" << sy << "\"/>\n";

    for(size_t si = 0; si < gLevel.sections.size(); ++si){
        const Section& sec = gLevel.sections[si];
        x << "  <section id=\"" << si << "\" size_right=\"" << sec.width << "\" size_bottom=\"" << sec.height
          << "\" music_id=\"" << sec.music << "\" bgcolor_r=\"" << (int)sec.bgColor.r << "\" bgcolor_g=\"" << (int)sec.bgColor.g
          << "\" bgcolor_b=\"" << (int)sec.bgColor.b << "\">\n";

        for(size_t li = 0; li < sec.layers.size(); ++li){
            const Layer& layer = sec.layers[li];
            for(const Obj& o : layer.objs){
                if(o.kind == K_TILE){
                    x << "    <block id=\"" << TILES[o.typeIdx].smbxId << "\" x=\"" << o.x << "\" y=\"" << o.y << "\" layer=\"" << li
                      << "\" event_destroy=\"" << o.eventId << "\" invisible=\"" << o.flags << "\"/>\n";
                } else if(o.kind == K_BGO){
                    x << "    <bgo id=\"" << BGOS[o.typeIdx].smbxId << "\" x=\"" << o.x << "\" y=\"" << o.y << "\" layer=\"" << li << "\"/>\n";
                } else if(o.kind == K_NPC){
                    x << "    <npc id=\"" << NPCS[o.typeIdx].smbxId << "\" x=\"" << o.x << "\" y=\"" << o.y << "\" layer=\"" << li
                      << "\" direction=\"1\" special_data=\"0\" event_activate=\"" << o.eventId << "\"/>\n";
                }
            }
        }
        x << "  </section>\n";
    }
    x << "</root>\n";
    std::string s = x.str();
    out.write(s.data(), (std::streamsize)s.size());
    return (bool)out;
}

static bool SaveByExtension(const std::string& path){
    std::string ext = LowerExt(path);
    if(ext == ".lvlx") return SaveLvlx(path);
    if(ext == ".38a") return Save38a(path);
    return SaveLvl(path);
}

static int TileIdxFromSmbx(uint32_t id){
    for(size_t i = 0; i < TILES.size(); ++i) if((uint32_t)TILES[i].smbxId == id) return (int)i;
    return 0;
}
static int BgoIdxFromSmbx(uint32_t id){
    for(size_t i = 0; i < BGOS.size(); ++i) if((uint32_t)BGOS[i].smbxId == id) return (int)i;
    return 0;
}
static int NpcIdxFromSmbx(uint32_t id){
    for(size_t i = 0; i < NPCS.size(); ++i) if((uint32_t)NPCS[i].smbxId == id) return (int)i;
    return 0;
}

static bool LoadLvl(std::istream& f){
    char magic[4];
    f.read(magic, 4);
    if(std::memcmp(magic, "LVL\x1a", 4) != 0) return false;
    Level L;
    (void)ReadU32(f);
    char nb[32], ab[32];
    f.read(nb, 32);
    f.read(ab, 32);
    auto nz = [](const char* p, size_t n){
        size_t i = 0;
        while(i < n && p[i]) ++i;
        return i;
    };
    L.name.assign(nb, nz(nb, 32));
    L.author.assign(ab, nz(ab, 32));
    L.timeLimit = (int)ReadU32(f);
    L.stars = (int)ReadU32(f);
    uint32_t flags = ReadU32(f);
    L.noBackground = (flags & 1u) != 0;
    std::vector<char> pad(128 - (4 + 4 + 32 + 32 + 4 + 4 + 4));
    f.read(pad.data(), pad.size());
    if(!f) return false;
    uint32_t nsec = ReadU32(f);
    L.sections.clear();
    for(uint32_t si = 0; si < nsec; ++si){
        Section s;
        s.width = (int)ReadU32(f);
        s.height = (int)ReadU32(f);
        uint8_t br, bg, bb;
        f.read((char*)&br, 1);
        f.read((char*)&bg, 1);
        f.read((char*)&bb, 1);
        char padb;
        f.read(&padb, 1);
        s.bgColor = {br, bg, bb, 255};
        s.startX = (int)ReadU32(f);
        s.startY = (int)ReadU32(f);
        s.music = (int)ReadU32(f);
        uint32_t nbk = ReadU32(f);
        for(uint32_t i = 0; i < nbk; ++i){
            uint32_t x = ReadU32(f), y = ReadU32(f), tid = ReadU32(f), layer = ReadU32(f), ev = ReadU32(f), fl = ReadU32(f);
            while((int)s.layers.size() <= (int)layer) s.layers.push_back({std::string("Layer ") + std::to_string(s.layers.size() + 1), true, false, {}});
            int tix = TileIdxFromSmbx(tid);
            s.layers[(size_t)layer].objs.push_back({K_TILE, tix, (int)x, (int)y, (int)layer, (int)ev, (int)fl});
        }
        uint32_t nbg = ReadU32(f);
        for(uint32_t i = 0; i < nbg; ++i){
            uint32_t x = ReadU32(f), y = ReadU32(f), tid = ReadU32(f), layer = ReadU32(f), fl = ReadU32(f);
            while((int)s.layers.size() <= (int)layer) s.layers.push_back({std::string("Layer ") + std::to_string(s.layers.size() + 1), true, false, {}});
            int ix = BgoIdxFromSmbx(tid);
            s.layers[(size_t)layer].objs.push_back({K_BGO, ix, (int)x, (int)y, (int)layer, -1, (int)fl});
        }
        uint32_t nn = ReadU32(f);
        for(uint32_t i = 0; i < nn; ++i){
            uint32_t x = ReadU32(f), y = ReadU32(f), tid = ReadU32(f), layer = ReadU32(f), ev = ReadU32(f), fl = ReadU32(f);
            uint32_t dir = ReadU32(f), sp = ReadU32(f);
            (void)dir;
            (void)sp;
            while((int)s.layers.size() <= (int)layer) s.layers.push_back({std::string("Layer ") + std::to_string(s.layers.size() + 1), true, false, {}});
            int ix = NpcIdxFromSmbx(tid);
            s.layers[(size_t)layer].objs.push_back({K_NPC, ix, (int)x, (int)y, (int)layer, (int)ev, (int)fl});
        }
        uint32_t nw = ReadU32(f);
        for(uint32_t i = 0; i < nw; ++i) f.ignore(64);
        uint32_t ne = ReadU32(f);
        for(uint32_t i = 0; i < ne; ++i){
            uint8_t nl = 0;
            f.read((char*)&nl, 1);
            f.ignore(nl);
            f.ignore(4);
            uint32_t ac = ReadU32(f);
            f.ignore(ac * 12u);
        }
        L.sections.push_back(std::move(s));
    }
    if(L.sections.empty()) return false;
    gLevel = std::move(L);
    gLevel.curSec = 0;
    gUndo.clear();
    gRedo.clear();
    gSelection.clear();
    return true;
}

static bool LoadLvl(const std::string& path){
    std::ifstream f(path, std::ios::binary);
    if(!f) return false;
    return LoadLvl(f);
}

// Reads first local file header (STORED) — matches WriteZipStoreOneFile layout.
static bool ReadZipStoredFirstInner(const std::string& zipPath, std::vector<char>& outBody, std::string* innerNameOut){
    std::ifstream f(zipPath, std::ios::binary);
    if(!f) return false;
    uint32_t sig = ReadU32(f);
    if(sig != 0x04034b50u) return false;
    f.ignore(10);
    uint32_t crc = ReadU32(f);
    (void)crc;
    uint32_t compSz = ReadU32(f);
    uint32_t uncSz = ReadU32(f);
    uint16_t fnLen = 0, exLen = 0;
    f.read((char*)&fnLen, 2);
    f.read((char*)&exLen, 2);
    std::string inner((size_t)fnLen, '\0');
    f.read(inner.data(), fnLen);
    f.ignore(exLen);
    if(compSz != uncSz) return false;
    outBody.resize(uncSz);
    f.read(outBody.data(), uncSz);
    if(innerNameOut) *innerNameOut = inner;
    return (bool)f && uncSz == outBody.size();
}

static bool Load38a(const std::string& path){
    std::vector<char> raw;
    std::string inner;
    if(!ReadZipStoredFirstInner(path, raw, &inner)) return false;
    if(inner != "level.lvl") return false;
    std::istringstream in(std::string(raw.begin(), raw.end()), std::ios::binary);
    return LoadLvl(in);
}

static int XmlAttrInt(const std::string& openTag, const char* key, int def){
    std::string pat = std::string(key) + "=\"";
    size_t p = openTag.find(pat);
    if(p == std::string::npos) return def;
    p += pat.size();
    int v = 0;
    bool neg = false;
    if(p < openTag.size() && openTag[p] == '-'){ neg = true; ++p; }
    while(p < openTag.size() && std::isdigit((unsigned char)openTag[p])){
        v = v * 10 + (openTag[p] - '0');
        ++p;
    }
    return neg ? -v : v;
}

static bool LoadLvlx(const std::string& path){
    std::ifstream fi(path, std::ios::binary);
    if(!fi) return false;
    std::string s((std::istreambuf_iterator<char>(fi)), std::istreambuf_iterator<char>());
    Level L;
    size_t hp = s.find("<head>");
    if(hp != std::string::npos){
        size_t he = s.find("</head>", hp);
        if(he != std::string::npos){
            std::string head = s.substr(hp, he - hp);
            size_t t1 = head.find("<title>");
            size_t t2 = head.find("</title>");
            if(t1 != std::string::npos && t2 != std::string::npos && t2 > t1)
                L.name = head.substr(t1 + 7, t2 - (t1 + 7));
            t1 = head.find("<author>");
            t2 = head.find("</author>");
            if(t1 != std::string::npos && t2 != std::string::npos && t2 > t1)
                L.author = head.substr(t1 + 8, t2 - (t1 + 8));
            t1 = head.find("<timer");
            if(t1 != std::string::npos){
                size_t gt = head.find('>', t1);
                L.timeLimit = XmlAttrInt(head.substr(t1, gt - t1), "value", L.timeLimit);
            }
            t1 = head.find("<stars");
            if(t1 != std::string::npos){
                size_t gt = head.find('>', t1);
                L.stars = XmlAttrInt(head.substr(t1, gt - t1), "value", L.stars);
            }
        }
    }
    L.sections.clear();
    size_t pos = 0;
    while((pos = s.find("<section", pos)) != std::string::npos){
        size_t gt = s.find('>', pos);
        if(gt == std::string::npos) break;
        std::string stag = s.substr(pos, gt - pos);
        Section sec;
        sec.width = XmlAttrInt(stag, "size_right", sec.width);
        sec.height = XmlAttrInt(stag, "size_bottom", sec.height);
        sec.music = XmlAttrInt(stag, "music_id", sec.music);
        sec.bgColor.r = (Uint8)XmlAttrInt(stag, "bgcolor_r", sec.bgColor.r);
        sec.bgColor.g = (Uint8)XmlAttrInt(stag, "bgcolor_g", sec.bgColor.g);
        sec.bgColor.b = (Uint8)XmlAttrInt(stag, "bgcolor_b", sec.bgColor.b);
        size_t endSec = s.find("</section>", gt);
        if(endSec == std::string::npos) break;
        std::string chunk = s.substr(gt + 1, endSec - (gt + 1));
        size_t q = 0;
        while((q = chunk.find("<block", q)) != std::string::npos){
            size_t g2 = chunk.find("/>", q);
            if(g2 == std::string::npos) break;
            std::string t = chunk.substr(q, g2 - q + 2);
            int tid = XmlAttrInt(t, "id", 0);
            int x = XmlAttrInt(t, "x", 0), y = XmlAttrInt(t, "y", 0);
            int li = XmlAttrInt(t, "layer", 0);
            int eid = XmlAttrInt(t, "event_destroy", -1);
            int fl = XmlAttrInt(t, "invisible", 0);
            while((int)sec.layers.size() <= li) sec.layers.push_back({std::string("Layer ") + std::to_string(sec.layers.size() + 1), true, false, {}});
            sec.layers[(size_t)li].objs.push_back({K_TILE, TileIdxFromSmbx((uint32_t)tid), x, y, li, eid, fl});
            q = g2 + 2;
        }
        q = 0;
        while((q = chunk.find("<bgo", q)) != std::string::npos){
            size_t g2 = chunk.find("/>", q);
            if(g2 == std::string::npos) break;
            std::string t = chunk.substr(q, g2 - q + 2);
            int tid = XmlAttrInt(t, "id", 0);
            int x = XmlAttrInt(t, "x", 0), y = XmlAttrInt(t, "y", 0);
            int li = XmlAttrInt(t, "layer", 0);
            while((int)sec.layers.size() <= li) sec.layers.push_back({std::string("Layer ") + std::to_string(sec.layers.size() + 1), true, false, {}});
            sec.layers[(size_t)li].objs.push_back({K_BGO, BgoIdxFromSmbx((uint32_t)tid), x, y, li, -1, 0});
            q = g2 + 2;
        }
        q = 0;
        while((q = chunk.find("<npc", q)) != std::string::npos){
            size_t g2 = chunk.find("/>", q);
            if(g2 == std::string::npos) break;
            std::string t = chunk.substr(q, g2 - q + 2);
            int tid = XmlAttrInt(t, "id", 0);
            int x = XmlAttrInt(t, "x", 0), y = XmlAttrInt(t, "y", 0);
            int li = XmlAttrInt(t, "layer", 0);
            int eid = XmlAttrInt(t, "event_activate", -1);
            while((int)sec.layers.size() <= li) sec.layers.push_back({std::string("Layer ") + std::to_string(sec.layers.size() + 1), true, false, {}});
            sec.layers[(size_t)li].objs.push_back({K_NPC, NpcIdxFromSmbx((uint32_t)tid), x, y, li, eid, 0});
            q = g2 + 2;
        }
        L.sections.push_back(std::move(sec));
        pos = endSec + 10;
    }
    size_t pp = s.find("<player_point");
    if(pp != std::string::npos && !L.sections.empty()){
        size_t gt = s.find('>', pp);
        if(gt != std::string::npos){
            std::string t = s.substr(pp, gt - pp);
            L.sections[0].startX = XmlAttrInt(t, "x", L.sections[0].startX);
            L.sections[0].startY = XmlAttrInt(t, "y", L.sections[0].startY);
        }
    }
    if(L.sections.empty()) return false;
    gLevel = std::move(L);
    gLevel.curSec = 0;
    gUndo.clear();
    gRedo.clear();
    gSelection.clear();
    return true;
}

static bool LoadByExtension(const std::string& path){
    std::string ext = LowerExt(path);
    if(ext == ".lvl") return LoadLvl(path);
    if(ext == ".38a") return Load38a(path);
    if(ext == ".lvlx") return LoadLvlx(path);
    return false;
}

// ---------- UI: sidebar & toolbar ----------
struct Btn { SDL_Rect r; std::string label; std::function<void()> cb; bool toggle=false, active=false; };
static std::vector<Btn> gToolbar;

static void OpenSaveAsModal();
static void OpenOpenModal();
static void OpenNewConfirmModal();
static void CmdNewClear();

static void CmdNewClear(){
    gLevel = Level();
    gUndo.clear();
    gRedo.clear();
    gCurrentFile.clear();
    gSelection.clear();
    gClipboard.clear();
    gStatusMsg = "New level";
    if(gPlaytest){
        delete gPlayer;
        gPlayer = nullptr;
        gPlaytest = false;
    }
}
static void CmdSave(){
    if(gCurrentFile.empty()){
        OpenSaveAsModal();
        return;
    }
    bool ok = SaveByExtension(gCurrentFile);
    gStatusMsg = ok ? ("Saved: " + gCurrentFile) : "Save failed";
}
static void CmdExportJson(){
    std::string p = gCurrentFile.empty()? "level.json" : (gCurrentFile+".json");
    gStatusMsg = SaveJson(p) ? ("Exported: "+p) : "Export failed";
}
static void CmdZoomIn(){ gCam.zoom = std::min(4.f, gCam.zoom+0.25f); }
static void CmdZoomOut(){ gCam.zoom = std::max(0.25f, gCam.zoom-0.25f); }
static void CmdUndo(){ if(gUndo.empty()){gStatusMsg="Nothing to undo";return;} auto s=gUndo.back(); gUndo.pop_back(); s.undo(); gRedo.push_back(s); gStatusMsg="Undo"; }
static void CmdRedo(){ if(gRedo.empty()){gStatusMsg="Nothing to redo";return;} auto s=gRedo.back(); gRedo.pop_back(); s.redo(); gUndo.push_back(s); gStatusMsg="Redo"; }
static void CmdPlaytest(){
    gPlaytest = !gPlaytest;
    if(gPlaytest){ delete gPlayer; gPlayer = new Player(gLevel.cur().startX, gLevel.cur().startY); gStatusMsg="PLAYTEST (Esc exits)"; }
    else { delete gPlayer; gPlayer=nullptr; gStatusMsg="Editor mode"; }
}

static void ModalClose(){
    if(gModal == 2 || gModal == 3) SDL_StopTextInput();
    gModal = 0;
    gDlgHover = -1;
}

static int MsgBoxBodyLineCount(){
    int n = 1;
    for(char c : gMsgBoxBody) if(c == '\n') ++n;
    return n;
}

static SDL_Rect MsgBoxFrameRect(){
    int mw = 400;
    int lines = MsgBoxBodyLineCount();
    int mh = 30 + lines * 18 + 52;
    mh = std::max(140, std::min(440, mh));
    return {(WIN_W - mw) / 2, (WIN_H - mh) / 2, mw, mh};
}

static SDL_Rect MsgBoxOkRect(){
    SDL_Rect fr = MsgBoxFrameRect();
    const int bw = 80, bh = 28;
    return {fr.x + (fr.w - bw) / 2, fr.y + fr.h - 44, bw, bh};
}

static void OpenAboutMsgBox(){
    gOpenMenu = -1;
    gModal = 1;
    gMsgBoxTitle = "About";
    gMsgBoxBody =
        "AC'S Mario fan builder\n"
        "Version 0.1\n\n"
        "(C) 1999-2026 A.C Holdings\n"
        "(C) 1999-2026 Redigit\n"
        "(C) 1985-2026 Nintendo";
}

static void DrawMessageBox(){
    if(gModal != 1) return;
    SDL_SetRenderDrawBlendMode(gRen, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(gRen, 0, 0, 0, 190);
    SDL_Rect full{0, 0, WIN_W, WIN_H};
    SDL_RenderFillRect(gRen, &full);

    SDL_Rect fr = MsgBoxFrameRect();
    FillR(fr, BTN_FACE);
    DrawEdge(fr, true);
    SDL_Rect titleR{fr.x + 2, fr.y + 2, fr.w - 4, 22};
    FillR(titleR, HL);
    DrawText(gMsgBoxTitle, titleR.x + 6, titleR.y + 4, WHITE_C);

    int y = titleR.y + titleR.h + 10;
    std::string rest = gMsgBoxBody;
    while(!rest.empty()){
        size_t pos = rest.find('\n');
        std::string line = (pos == std::string::npos) ? rest : rest.substr(0, pos);
        if(pos == std::string::npos) rest.clear();
        else rest = rest.substr(pos + 1);
        DrawText(line, fr.x + 18, y, TEXT_C);
        y += 18;
    }
    SDL_Rect ok = MsgBoxOkRect();
    FillR(ok, MD_DEEP);
    DrawEdge(ok, true);
    DrawText("OK", ok.x + ok.w / 2, ok.y + ok.h / 2, TEXT_C, gFontSmall, true);
}

static int Utf8Prev(const std::string& s, int pos){
    if(pos <= 0) return 0;
    int p = pos - 1;
    while(p > 0 && (unsigned char)s[(size_t)p] >= 0x80u && (unsigned char)s[(size_t)p] < 0xc0u) --p;
    return p;
}
static int Utf8Next(const std::string& s, int pos){
    if(pos >= (int)s.size()) return (int)s.size();
    unsigned char c = (unsigned char)s[(size_t)pos];
    int adv = 1;
    if(c >= 0xf0u) adv = 4;
    else if(c >= 0xe0u) adv = 3;
    else if(c >= 0xc0u) adv = 2;
    return std::min(pos + adv, (int)s.size());
}

static SDL_Rect FileDlgOuter(){
    const int w = 500, h = 320;
    return {(WIN_W - w) / 2, (WIN_H - h) / 2, w, h};
}

static void DlgDrawButton(SDL_Rect r, const char* text, bool hot){
    FillR(r, hot ? HL : MD_DEEP);
    DrawEdge(r, true);
    DrawText(text, r.x + r.w / 2, r.y + r.h / 2, hot ? WHITE_C : TEXT_C, gFontSmall, true);
}

static void FileDlgLayoutRects(SDL_Rect& nameR, SDL_Rect* filtR, SDL_Rect& okR, SDL_Rect& canR){
    SDL_Rect o = FileDlgOuter();
    nameR = {o.x + 16, o.y + 86, o.w - 32, 26};
    for(int i = 0; i < 3; ++i) filtR[i] = {o.x + 16, o.y + 148 + i * 24, o.w - 32, 22};
    int by = o.y + o.h - 44;
    okR = {o.x + o.w / 2 - 100, by, 90, 30};
    canR = {o.x + o.w / 2 + 10, by, 90, 30};
}

static int FileDlgHit(int mx, int my){
    SDL_Point p{mx, my};
    SDL_Rect nameR, filtR[3], okR, canR;
    FileDlgLayoutRects(nameR, filtR, okR, canR);
    if(SDL_PointInRect(&p, &nameR)) return 1;
    for(int i = 0; i < 3; ++i)
        if(SDL_PointInRect(&p, &filtR[i])) return 10 + i;
    if(SDL_PointInRect(&p, &okR)) return 100;
    if(SDL_PointInRect(&p, &canR)) return 101;
    return 0;
}

static std::string DlgTrimPath(std::string s){
    while(!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    size_t i = 0;
    while(i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    if(i) s.erase(0, i);
    return s;
}

static std::string DlgEnsureExtension(std::string path, int filter){
    path = DlgTrimPath(path);
    if(path.empty()) path = "level";
    if(LowerExt(path).empty()){
        if(filter == 0) path += ".lvl";
        else if(filter == 1) path += ".38a";
        else path += ".lvlx";
    }
    return path;
}

static void FileDlgCommitSave(){
    std::string path = DlgEnsureExtension(gDlgFileName, gDlgFilter);
    bool ok = false;
    if(gDlgFilter == 0) ok = SaveLvl(path);
    else if(gDlgFilter == 1) ok = Save38a(path);
    else ok = SaveLvlx(path);
    if(ok){
        gCurrentFile = path;
        gStatusMsg = "Saved: " + path;
    } else gStatusMsg = "Save failed";
    ModalClose();
}

static void FileDlgCommitOpen(){
    std::string path = DlgTrimPath(gDlgFileName);
    if(path.empty() || path.find('*') != std::string::npos){
        gStatusMsg = "Enter a file path to open.";
        ModalClose();
        return;
    }
    if(LowerExt(path).empty()) path = DlgEnsureExtension(path, gDlgFilter);
    if(LoadByExtension(path)){
        gCurrentFile = path;
        gStatusMsg = "Opened: " + path;
    } else gStatusMsg = "Open failed (format or I/O)";
    ModalClose();
}

static void OpenSaveAsModal(){
    gOpenMenu = -1;
    gModal = 2;
    if(gCurrentFile.empty()) gDlgFileName = BasenameNoExt() + ".lvl";
    else gDlgFileName = gCurrentFile;
    gDlgFilter = 0;
    gDlgCaret = (int)gDlgFileName.size();
    SDL_StartTextInput();
}

static void OpenOpenModal(){
    gOpenMenu = -1;
    gModal = 3;
    gDlgFileName = gCurrentFile.empty() ? std::string("level.lvl") : gCurrentFile;
    gDlgFilter = 0;
    gDlgCaret = (int)gDlgFileName.size();
    SDL_StartTextInput();
}

static void OpenNewConfirmModal(){
    gOpenMenu = -1;
    gModal = 4;
}

static SDL_Rect NewDlgYesRect(){
    SDL_Rect o = FileDlgOuter();
    return {o.x + o.w / 2 - 100, o.y + o.h - 50, 88, 30};
}
static SDL_Rect NewDlgNoRect(){
    SDL_Rect o = FileDlgOuter();
    return {o.x + o.w / 2 + 12, o.y + o.h - 50, 88, 30};
}

static void HandleFileDialogsMouse(SDL_Event& e){
    if(e.type == SDL_MOUSEMOTION){
        if(gModal == 2 || gModal == 3) gDlgHover = FileDlgHit(e.motion.x, e.motion.y);
        else if(gModal == 4){
            SDL_Point p{e.motion.x, e.motion.y};
            SDL_Rect yr = NewDlgYesRect(), nr = NewDlgNoRect();
            gDlgHover = SDL_PointInRect(&p, &yr) ? 200 : (SDL_PointInRect(&p, &nr) ? 201 : -1);
        }
        return;
    }
    if(e.type != SDL_MOUSEBUTTONDOWN || e.button.button != SDL_BUTTON_LEFT) return;
    int mx = e.button.x, my = e.button.y;
    if(gModal == 2 || gModal == 3){
        int h = FileDlgHit(mx, my);
        if(h >= 10 && h <= 12) gDlgFilter = h - 10;
        else if(h == 100){
            if(gModal == 2) FileDlgCommitSave();
            else FileDlgCommitOpen();
        } else if(h == 101) ModalClose();
        return;
    }
    if(gModal == 4){
        SDL_Point p{mx, my};
        SDL_Rect yr = NewDlgYesRect(), nr = NewDlgNoRect();
        if(SDL_PointInRect(&p, &yr)){
            CmdNewClear();
            ModalClose();
        } else if(SDL_PointInRect(&p, &nr)) ModalClose();
    }
}

static void HandleFileDialogsKey(SDL_Event& e){
    if(e.type != SDL_KEYDOWN) return;
    SDL_Keymod mods = SDL_GetModState();
    (void)mods;
    auto sym = e.key.keysym.sym;
    if(gModal == 4){
        if(sym == SDLK_ESCAPE || sym == SDLK_n) ModalClose();
        if(sym == SDLK_y || sym == SDLK_RETURN || sym == SDLK_KP_ENTER){
            CmdNewClear();
            ModalClose();
        }
        return;
    }
    if(gModal != 2 && gModal != 3) return;
    if(sym == SDLK_ESCAPE){
        ModalClose();
        return;
    }
    if(sym == SDLK_RETURN || sym == SDLK_KP_ENTER){
        if(gModal == 2) FileDlgCommitSave();
        else FileDlgCommitOpen();
        return;
    }
    if(sym == SDLK_TAB){
        gDlgFilter = (gDlgFilter + 1) % 3;
        return;
    }
    if(sym == SDLK_BACKSPACE){
        if(gDlgCaret > 0){
            int p = Utf8Prev(gDlgFileName, gDlgCaret);
            gDlgFileName.erase((size_t)p, (size_t)(gDlgCaret - p));
            gDlgCaret = p;
        }
        return;
    }
    if(sym == SDLK_DELETE){
        if(gDlgCaret < (int)gDlgFileName.size()){
            int n = Utf8Next(gDlgFileName, gDlgCaret);
            gDlgFileName.erase((size_t)gDlgCaret, (size_t)(n - gDlgCaret));
        }
        return;
    }
    if(sym == SDLK_LEFT){
        gDlgCaret = Utf8Prev(gDlgFileName, gDlgCaret);
        return;
    }
    if(sym == SDLK_RIGHT){
        gDlgCaret = Utf8Next(gDlgFileName, gDlgCaret);
        return;
    }
    if(sym == SDLK_HOME) gDlgCaret = 0;
    else if(sym == SDLK_END) gDlgCaret = (int)gDlgFileName.size();
}

static void HandleTextInput(const SDL_Event& e){
    if(e.type != SDL_TEXTINPUT) return;
    if(gModal != 2 && gModal != 3) return;
    std::string t = e.text.text;
    gDlgFileName.insert((size_t)gDlgCaret, t);
    gDlgCaret += (int)t.size();
}

static void DrawFileDialogs(){
    if(gModal != 2 && gModal != 3 && gModal != 4) return;
    SDL_SetRenderDrawBlendMode(gRen, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(gRen, 0, 0, 0, 190);
    SDL_Rect full{0, 0, WIN_W, WIN_H};
    SDL_RenderFillRect(gRen, &full);

    SDL_Rect o = FileDlgOuter();
    FillR(o, BTN_FACE);
    DrawEdge(o, true);
    SDL_Rect titleR{o.x + 2, o.y + 2, o.w - 4, 22};
    FillR(titleR, HL);

    if(gModal == 2){
        DrawText("Save As", titleR.x + 6, titleR.y + 4, WHITE_C);
        DrawText("File name:", o.x + 16, o.y + 64, TEXT_C);
        SDL_Rect nameR, filtR[3], okR, canR;
        FileDlgLayoutRects(nameR, filtR, okR, canR);
        FillR(nameR, WINDOW_C);
        DrawEdge(nameR, false);
        SDL_RenderSetClipRect(gRen, &nameR);
        DrawText(gDlgFileName, nameR.x + 4, nameR.y + 5, TEXT_C);
        Uint32 tick = SDL_GetTicks();
        if((tick / 530) % 2 == 0){
            int cx = nameR.x + 4;
            if(gFontSmall && !gDlgFileName.empty() && gDlgCaret > 0){
                std::string before = gDlgFileName.substr(0, (size_t)gDlgCaret);
                int tw = 0, th = 0;
                TTF_SizeUTF8(gFontSmall, before.c_str(), &tw, &th);
                (void)th;
                cx += tw;
            }
            Line(cx, nameR.y + 4, cx, nameR.y + nameR.h - 4, TEXT_C);
        }
        SDL_RenderSetClipRect(gRen, nullptr);
        DrawText("Save as type:", o.x + 16, o.y + 126, TEXT_C);
        const char* labs[] = {
            "SMBX / Mario Forever Level (*.lvl)",
            "LunaLua Archive (*.38a)",
            "Moondust / PGE Level XML (*.lvlx)",
        };
        for(int i = 0; i < 3; ++i){
            bool sel = (gDlgFilter == i);
            bool hot = (gDlgHover == 10 + i);
            FillR(filtR[i], sel ? HL : (hot ? WINDOW_C : MD_DEEP));
            DrawEdge(filtR[i], true);
            DrawText(labs[i], filtR[i].x + 8, filtR[i].y + 4, (sel || hot) ? WHITE_C : TEXT_C);
        }
        DlgDrawButton(okR, "OK", gDlgHover == 100);
        DlgDrawButton(canR, "Cancel", gDlgHover == 101);
    } else if(gModal == 3){
        DrawText("Open", titleR.x + 6, titleR.y + 4, WHITE_C);
        DrawText("File name:", o.x + 16, o.y + 64, TEXT_C);
        SDL_Rect nameR, filtR[3], okR, canR;
        FileDlgLayoutRects(nameR, filtR, okR, canR);
        FillR(nameR, WINDOW_C);
        DrawEdge(nameR, false);
        SDL_RenderSetClipRect(gRen, &nameR);
        DrawText(gDlgFileName, nameR.x + 4, nameR.y + 5, TEXT_C);
        Uint32 tick = SDL_GetTicks();
        if((tick / 530) % 2 == 0){
            int cx = nameR.x + 4;
            if(gFontSmall && !gDlgFileName.empty() && gDlgCaret > 0){
                std::string before = gDlgFileName.substr(0, (size_t)gDlgCaret);
                int tw = 0, th = 0;
                TTF_SizeUTF8(gFontSmall, before.c_str(), &tw, &th);
                (void)th;
                cx += tw;
            }
            Line(cx, nameR.y + 4, cx, nameR.y + nameR.h - 4, TEXT_C);
        }
        SDL_RenderSetClipRect(gRen, nullptr);
        DrawText("File of type:", o.x + 16, o.y + 126, TEXT_C);
        const char* labs[] = {"SMBX Level (*.lvl)", "LunaLua (*.38a)", "Moondust (*.lvlx)"};
        for(int i = 0; i < 3; ++i){
            bool sel = (gDlgFilter == i);
            bool hot = (gDlgHover == 10 + i);
            FillR(filtR[i], sel ? HL : (hot ? WINDOW_C : MD_DEEP));
            DrawEdge(filtR[i], true);
            DrawText(labs[i], filtR[i].x + 8, filtR[i].y + 4, (sel || hot) ? WHITE_C : TEXT_C);
        }
        DlgDrawButton(okR, "OK", gDlgHover == 100);
        DlgDrawButton(canR, "Cancel", gDlgHover == 101);
    } else if(gModal == 4){
        DrawText("New level", titleR.x + 6, titleR.y + 4, WHITE_C);
        DrawText("Discard the current level and start a new one?", o.x + 20, o.y + 56, TEXT_C);
        DlgDrawButton(NewDlgYesRect(), "Yes", gDlgHover == 200);
        DlgDrawButton(NewDlgNoRect(), "No", gDlgHover == 201);
    }
}

static void CmdSelectAll(){
    gSelection.clear();
    int li = gLevel.cur().currentLayer;
    auto& L = gLevel.cur().layers[(size_t)li].objs;
    for(int i = 0; i < (int)L.size(); ++i) gSelection.push_back({li, i});
    gStatusMsg = gSelection.empty() ? "Nothing on layer" : "Select all";
}

static void CmdCopy(){
    gClipboard.clear();
    for(const auto& st : gSelection){
        if(st.lay < 0 || st.lay >= (int)gLevel.cur().layers.size()) continue;
        auto& L = gLevel.cur().layers[(size_t)st.lay].objs;
        if(st.idx >= 0 && st.idx < (int)L.size()) gClipboard.push_back(L[(size_t)st.idx]);
    }
    gStatusMsg = gClipboard.empty() ? "Nothing to copy" : "Copied";
}

static void CmdDeleteSelection(){
    if(gSelection.empty()){
        gStatusMsg = "Nothing selected";
        return;
    }
    struct Item { int lay, idx; Obj o; };
    std::vector<Item> items;
    for(const auto& st : gSelection){
        if(st.lay < 0 || st.lay >= (int)gLevel.cur().layers.size()) continue;
        auto& L = gLevel.cur().layers[(size_t)st.lay].objs;
        if(st.idx < 0 || st.idx >= (int)L.size()) continue;
        items.push_back({st.lay, st.idx, L[(size_t)st.idx]});
    }
    if(items.empty()) return;
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b){
        if(a.lay != b.lay) return a.lay < b.lay;
        return a.idx > b.idx;
    });
    int secIdx = gLevel.curSec;
    std::vector<Item> saved = items;
    for(const auto& it : items){
        gLevel.sections[secIdx].layers[(size_t)it.lay].objs.erase(
            gLevel.sections[secIdx].layers[(size_t)it.lay].objs.begin() + it.idx);
    }
    gSelection.clear();
    PushUndo({
        [saved, secIdx](){
            std::vector<Item> asc = saved;
            std::sort(asc.begin(), asc.end(), [](const Item& a, const Item& b){
                if(a.lay != b.lay) return a.lay < b.lay;
                return a.idx < b.idx;
            });
            for(const auto& it : asc){
                gLevel.sections[secIdx].layers[(size_t)it.lay].objs.insert(
                    gLevel.sections[secIdx].layers[(size_t)it.lay].objs.begin() + it.idx, it.o);
            }
        },
        [saved, secIdx](){
            std::vector<Item> d = saved;
            std::sort(d.begin(), d.end(), [](const Item& a, const Item& b){
                if(a.lay != b.lay) return a.lay < b.lay;
                return a.idx > b.idx;
            });
            for(const auto& it : d){
                gLevel.sections[secIdx].layers[(size_t)it.lay].objs.erase(
                    gLevel.sections[secIdx].layers[(size_t)it.lay].objs.begin() + it.idx);
            }
        }
    });
    gStatusMsg = "Deleted";
}

static void CmdCut(){
    CmdCopy();
    if(!gClipboard.empty()) CmdDeleteSelection();
}

static void CmdPaste(){
    if(gClipboard.empty()){
        gStatusMsg = "Clipboard empty";
        return;
    }
    auto& L = gLevel.cur().cur();
    if(L.locked){
        gStatusMsg = "Layer locked";
        return;
    }
    int li = gLevel.cur().currentLayer;
    const int dx = GRID * 2, dy = GRID * 2;
    for(Obj o : gClipboard){
        o.x += dx;
        o.y += dy;
        o.layer = li;
        L.objs.push_back(o);
    }
    gStatusMsg = "Pasted";
}

static void CmdResetPlayer(){
    if(!gPlaytest || !gPlayer) return;
    delete gPlayer;
    gPlayer = new Player(gLevel.cur().startX, gLevel.cur().startY);
    gStatusMsg = "Player reset to start";
}

static void InitMenus(){
    gMenus.clear();
    gMenus.push_back({
        {"New", OpenNewConfirmModal},
        {"Open...", OpenOpenModal},
        {"Save", CmdSave},
        {"Save As...", OpenSaveAsModal},
        {"Export JSON", CmdExportJson},
        {"Quit", [](){ gRunning = false; }},
    });
    gMenus.push_back({
        {"Undo", CmdUndo},
        {"Redo", CmdRedo},
        {"Cut", CmdCut},
        {"Copy", CmdCopy},
        {"Paste", CmdPaste},
        {"Delete", CmdDeleteSelection},
        {"Select All", CmdSelectAll},
    });
    gMenus.push_back({
        {"Toggle grid", [](){ gGrid = !gGrid; gOpenMenu = -1; gStatusMsg = gGrid ? "Grid on" : "Grid off"; }},
        {"Zoom in",     [](){ CmdZoomIn();  gOpenMenu = -1; }},
        {"Zoom out",    [](){ CmdZoomOut(); gOpenMenu = -1; }},
        {"Reset zoom",  [](){ gCam.zoom = 1.f; gOpenMenu = -1; gStatusMsg = "Zoom 100%"; }},
    });
    gMenus.push_back({
        {"Pencil",  [](){ gTool = T_PENCIL;  gOpenMenu = -1; gStatusMsg = "Pencil — left: place, right: erase"; }},
        {"Eraser",  [](){ gTool = T_ERASER;  gOpenMenu = -1; gStatusMsg = "Eraser — left: place, right: erase"; }},
        {"Fill",    [](){ gTool = T_FILL;    gOpenMenu = -1; gStatusMsg = "Fill"; }},
        {"Select",  [](){ gTool = T_SELECT;  gOpenMenu = -1; gStatusMsg = "Select"; }},
    });
    gMenus.push_back({
        {"Playtest", [](){ CmdPlaytest(); gOpenMenu = -1; }},
        {"Reset player", [](){ CmdResetPlayer(); gOpenMenu = -1; }},
    });
    gMenus.push_back({
        {"About", [](){ OpenAboutMsgBox(); }},
    });
    LayoutMenuTabs();
}

static void BuildToolbar(){
    gToolbar.clear();
    int x=SIDEBAR_W+6, y=MENU_H+(TOOL_H-24)/2;
    auto add=[&](std::string lbl, std::function<void()> cb, bool tog=false){
        gToolbar.push_back({{x,y,48,24},lbl,cb,tog,false}); x+=52;
    };
    add("New", OpenNewConfirmModal);
    add("Save",CmdSave);
    add("JSON",CmdExportJson);
    x+=8;
    add("Undo",CmdUndo); add("Redo",CmdRedo);
    x+=8;
    add("Pen", []{ gTool=T_PENCIL; gStatusMsg="Pencil — L:place R:erase"; });
    add("Erase",[]{ gTool=T_ERASER; gStatusMsg="Eraser — L:place R:erase"; });
    add("Fill", []{ gTool=T_FILL; gStatusMsg="Fill"; });
    add("Sel",  []{ gTool=T_SELECT; gStatusMsg="Select"; });
    x+=8;
    add("Grid", []{ gGrid=!gGrid; }, true);
    add("Z+",CmdZoomIn); add("Z-",CmdZoomOut);
    x+=8;
    add("PLAY",CmdPlaytest, true);
}

static void DrawToolbar(){
    FillR({0, MENU_H, WIN_W, TOOL_H}, MD_DOCK);
    Line(0, MENU_H, WIN_W, MENU_H, BTN_SHD);
    Line(0, MENU_H + TOOL_H - 1, WIN_W, MENU_H + TOOL_H - 1, BTN_DARK);
    for(auto& b : gToolbar){
        bool on = b.toggle && ((b.label=="Grid"&&gGrid)||(b.label=="PLAY"&&gPlaytest));
        FillR(b.r, on ? HL : MD_DEEP);
        DrawEdge(b.r, !on);
        DrawText(b.label, b.r.x+b.r.w/2, b.r.y+b.r.h/2, on?WHITE_C:TEXT_C, gFontSmall, true);
    }
}

static void DrawSidebar(){
    SDL_Rect sb={0,CANVAS_Y,SIDEBAR_W,CANVAS_H};
    FillR(sb, MD_DOCK);
    DrawEdge(sb, false);
    SDL_Rect title={sb.x+2,sb.y+2,sb.w-4,18}; FillR(title,HL);
    DrawText("Item Toolbox",title.x+4,title.y+3,WHITE_C);
    // tabs
    const char* tabs[3]={"Tiles","BGOs","NPCs"};
    Kind kinds[3]={K_TILE,K_BGO,K_NPC};
    int tw=(sb.w-4)/3;
    for(int i=0;i<3;++i){
        SDL_Rect tr={sb.x+2+i*tw, title.y+title.h+2, tw-2, 20};
        bool sel=(gCat==kinds[i]);
        FillR(tr, sel ? WINDOW_C : MD_DEEP);
        DrawEdge(tr, !sel);
        DrawText(tabs[i], tr.x+tr.w/2, tr.y+10, TEXT_C, gFontSmall, true);
    }
    // items
    SDL_Rect content={sb.x+2, title.y+title.h+24, sb.w-4, sb.h-44};
    FillR(content, WINDOW_C);
    int n=CatCount(gCat);
    for(int i=0;i<n;++i){
        SDL_Rect cell={content.x+4+(i%5)*36, content.y+4+(i/5)*36, 32,32};
        if(!SDL_HasIntersection(&content,&cell)) continue;
        if(i==gSel){ SDL_Rect sr={cell.x-2,cell.y-2,cell.w+4,cell.h+4}; FillR(sr,HL2); }
        Obj o{gCat,i,0,0,0,-1,0};
        // draw sprite into cell (no zoom factor here; draw using temp obj with 0,0 and manual offset)
        // We'll translate via ox/oy in DrawObj, feeding 1.0 zoom. Object x=0,y=0 so world pos = cell.
        // Temporarily shift camera-unaware: simpler—call DrawObj with origin at cell.x,cell.y and z=1.
        // But DrawObj uses o.x*z + ox. With o.x=0,o.y=0 it's ox,oy.
        DrawObj(o, cell.x, cell.y, 1.0f);
    }
}

static void DrawMenuDropdown(){
    if(gOpenMenu < 0 || gOpenMenu >= (int)gMenus.size()) return;
    SDL_Rect d = MenuDropdownRect(gOpenMenu);
    FillR(d, MD_DEEP);
    DrawEdge(d, true);
    SDL_Rect accent{d.x, d.y, 3, d.h};
    FillR(accent, HL);
    Line(d.x + 3, d.y, d.x + d.w - 1, d.y, BTN_LIGHT);
    for(int r = 0; r < (int)gMenus[gOpenMenu].size(); ++r){
        SDL_Rect row{d.x + 6, d.y + 4 + r * 22, d.w - 8, 20};
        bool hi = (r == gMenuRowHover);
        if(hi) FillR(row, HL);
        DrawText(gMenus[gOpenMenu][r].label, row.x + 6, row.y + 3, hi ? WHITE_C : TEXT_C);
    }
}

static void DrawMenuBar(){
    FillR({0, 0, WIN_W, MENU_H}, MD_DOCK);
    Line(0, MENU_H - 2, WIN_W, MENU_H - 2, HL);
    Line(0, MENU_H - 1, WIN_W, MENU_H - 1, BTN_SHD);
    const char* labs[] = {"File", "Edit", "View", "Tools", "Test", "Help"};
    TTF_Font* menuFont = gFont ? gFont : gFontSmall;
    gMenuTabHover = -1;
    SDL_Point mp{gMouseX, gMouseY};
    if(mp.y < MENU_H){
        for(int i = 0; i < 6; ++i)
            if(SDL_PointInRect(&mp, &gMenuTabRect[i])){ gMenuTabHover = i; break; }
    }
    for(int i = 0; i < 6; ++i){
        bool open = (gOpenMenu == i);
        bool hot = (gMenuTabHover == i);
        if(open || hot){
            SDL_Rect tr = gMenuTabRect[i];
            FillR(tr, open ? HL : WINDOW_C);
        }
        int tw = 0, th = 0;
        if(menuFont) TTF_SizeUTF8(menuFont, labs[i], &tw, &th);
        int tx = gMenuTabRect[i].x + (gMenuTabRect[i].w - tw) / 2;
        int ty = std::max(0, (MENU_H - th) / 2);
        Col labCol = (open || hot) ? WHITE_C : C(235, 235, 240);
        DrawText(labs[i], tx, ty, labCol, menuFont);
    }
    const char* cap = "AC'S Mario fan builder";
    int cw = 0, ch = 0;
    if(menuFont) TTF_SizeUTF8(menuFont, cap, &cw, &ch);
    int titleX = WIN_W - cw - 10;
    if(titleX < gMenuBarEndX + 12){
        cap = "AC'S MFB";
        if(menuFont) TTF_SizeUTF8(menuFont, cap, &cw, &ch);
        titleX = WIN_W - cw - 10;
    }
    int titleY = std::max(0, (MENU_H - ch) / 2);
    if(titleX > gMenuBarEndX + 4) DrawText(cap, titleX, titleY, HL2, menuFont);
}

static void DrawStatusBar(){
    int y=WIN_H-STATUS_H;
    FillR({0,y,WIN_W,STATUS_H}, BG_DARK);
    Line(0,y,WIN_W,y,BTN_SHD);
    auto panel=[&](int px,int pw,const std::string& s){
        SDL_Rect r={px,y+2,pw,STATUS_H-4};
        FillR(r,BTN_FACE); DrawEdge(r,false);
        DrawText(s, r.x+4, r.y+4, TEXT_C);
    };
    std::string modeStr = gPlaytest?"PLAYTEST":(gTool==T_PENCIL?"PENCIL":gTool==T_ERASER?"ERASER":gTool==T_FILL?"FILL":"SELECT");
    panel(2,120,"Mode: "+modeStr);
    panel(126,160,"Layer: "+gLevel.cur().cur().name);
    int wx=(int)((gMouseX-SIDEBAR_W)/gCam.zoom)-gCam.x;
    int wy=(int)((gMouseY-CANVAS_Y)/gCam.zoom)-gCam.y;
    int gx=wx/GRID, gy=wy/GRID;
    char buf[64]; std::snprintf(buf,64,"X:%d Y:%d",gx,gy); panel(290,120,buf);
    std::snprintf(buf,64,"Zoom: %d%%",(int)(gCam.zoom*100)); panel(414,100,buf);
    if(gPlaytest && gPlayer){
        std::snprintf(buf,64,"Coins:%d Score:%d",gPlayer->coins,gPlayer->score);
        panel(518,200,buf);
    } else {
        panel(518,WIN_W-522,gStatusMsg);
    }
}

static void DrawCanvas(){
    SDL_Rect cv={SIDEBAR_W,CANVAS_Y,CANVAS_W,CANVAS_H};
    SDL_RenderSetClipRect(gRen,&cv);
    FillR(cv, gLevel.cur().bgColor);

    // grid
    if(gGrid){
        float z=gCam.zoom;
        int gs=(int)(GRID*z); if(gs<4) gs=4;
        int ox = SIDEBAR_W + (int)(gCam.x*z) % gs;
        int oy = CANVAS_Y  + (int)(gCam.y*z) % gs;
        for(int x=ox; x<cv.x+cv.w; x+=gs) Line(x,cv.y,x,cv.y+cv.h,GRID_C);
        for(int y=oy; y<cv.y+cv.h; y+=gs) Line(cv.x,y,cv.x+cv.w,y,GRID_C);
    }

    int ox = SIDEBAR_W + (int)(gCam.x*gCam.zoom);
    int oy = CANVAS_Y  + (int)(gCam.y*gCam.zoom);

    // layers: draw BGO first (behind), then tiles, then NPCs
    auto& sec = gLevel.cur();
    for(int pass=0; pass<3; ++pass){
        Kind want = pass==0?K_BGO:pass==1?K_TILE:K_NPC;
        for(auto& L : sec.layers){
            if(!L.visible) continue;
            for(auto& o : L.objs) if(o.kind==want) DrawObj(o, ox, oy, gCam.zoom);
        }
    }

    // start marker
    if(!gPlaytest){
        SDL_Rect sp={ox+(int)(sec.startX*gCam.zoom), oy+(int)(sec.startY*gCam.zoom),
                     (int)(GRID*gCam.zoom),(int)(GRID*gCam.zoom)};
        DrawR(sp,GREEN_C); DrawText("S",sp.x+2,sp.y+2,GREEN_C);
    }

    // player
    if(gPlaytest && gPlayer){
        if(gPlayer->invincible<=0 || (gPlayer->invincible/5)%2==0){
            SDL_Rect pr={ox+(int)(gPlayer->x*gCam.zoom), oy+(int)(gPlayer->y*gCam.zoom),
                         (int)(GRID*gCam.zoom),(int)(GRID*gCam.zoom)};
            FillR({pr.x+pr.w/4,pr.y+pr.h/3,pr.w/2,pr.h*2/3}, C(44,44,140));
            FillR({pr.x+pr.w/4,pr.y+2,pr.w/2,pr.h/3}, C(229,37,37));
            FillCircle(pr.x+pr.w/3, pr.y+pr.h/4+2, 2, WHITE_C);
            FillCircle(pr.x+2*pr.w/3, pr.y+pr.h/4+2, 2, WHITE_C);
        }
    }

    if(!gPlaytest){
        float z = gCam.zoom;
        for(const auto& st : gSelection){
            if(st.lay < 0 || st.lay >= (int)sec.layers.size()) continue;
            const auto& L = sec.layers[(size_t)st.lay];
            if(st.idx < 0 || st.idx >= (int)L.objs.size()) continue;
            const Obj& o = L.objs[(size_t)st.idx];
            SDL_Rect rr{ox + (int)(o.x * z), oy + (int)(o.y * z), (int)(GRID * z), (int)(GRID * z)};
            DrawR(rr, YELLOW_C);
        }
    }

    SDL_RenderSetClipRect(gRen,nullptr);
    DrawEdge(cv,false);
}

// ---------- Input ----------
static void HandleMouse(SDL_Event& e){
    if(e.type == SDL_MOUSEMOTION){
        gMouseX = e.motion.x;
        gMouseY = e.motion.y;
    } else if(e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP){
        gMouseX = e.button.x;
        gMouseY = e.button.y;
    }

    if(gModal != 0){
        if(gModal == 1){
            if(e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT){
                SDL_Point p{e.button.x, e.button.y};
                SDL_Rect okr = MsgBoxOkRect();
                if(SDL_PointInRect(&p, &okr)) ModalClose();
            }
            return;
        }
        if(gModal == 2 || gModal == 3 || gModal == 4) HandleFileDialogsMouse(e);
        return;
    }

    if(!gPlaytest && gOpenMenu >= 0 && e.type == SDL_MOUSEMOTION){
        gMenuRowHover = MenuDropdownHit(gOpenMenu, gMouseX, gMouseY);
    } else if(e.type == SDL_MOUSEMOTION){
        gMenuRowHover = -1;
    }

    // ---- Menu bar & dropdown (Moondust-style strip) ----
    if(!gPlaytest && (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP)){
        SDL_Point p{e.button.x, e.button.y};
        if(e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT){
            if(gOpenMenu >= 0){
                int hit = MenuDropdownHit(gOpenMenu, p.x, p.y);
                if(hit >= 0){
                    gMenus[gOpenMenu][hit].onPick();
                    return;
                }
                SDL_Rect dr = MenuDropdownRect(gOpenMenu);
                if(SDL_PointInRect(&p, &dr))
                    return;
                if(p.y < MENU_H){
                    for(int i = 0; i < 6; ++i){
                        if(SDL_PointInRect(&p, &gMenuTabRect[i])){
                            gOpenMenu = (gOpenMenu == i) ? -1 : i;
                            return;
                        }
                    }
                    gOpenMenu = -1;
                    return;
                }
                gOpenMenu = -1;
            } else if(p.y < MENU_H){
                for(int i = 0; i < 6; ++i){
                    if(SDL_PointInRect(&p, &gMenuTabRect[i])){
                        gOpenMenu = (gOpenMenu == i) ? -1 : i;
                        return;
                    }
                }
            }
        }
    }

    // toolbar
    if(e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT){
        SDL_Point p{e.button.x, e.button.y};
        for(auto& b : gToolbar)
            if(SDL_PointInRect(&p, &b.r)){
                b.cb();
                return;
            }
    }
    // sidebar
    if(e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT){
        SDL_Rect sb{0, CANVAS_Y, SIDEBAR_W, CANVAS_H};
        SDL_Point p{e.button.x, e.button.y};
        if(SDL_PointInRect(&p, &sb)){
            int tw = (sb.w - 4) / 3;
            Kind kinds[3] = {K_TILE, K_BGO, K_NPC};
            for(int i = 0; i < 3; ++i){
                SDL_Rect tr{sb.x + 2 + i * tw, sb.y + 22, tw - 2, 20};
                if(SDL_PointInRect(&p, &tr)){
                    gCat = kinds[i];
                    gSel = 0;
                    return;
                }
            }
            SDL_Rect content{sb.x + 2, sb.y + 46, sb.w - 4, sb.h - 48};
            int n = CatCount(gCat);
            for(int i = 0; i < n; ++i){
                SDL_Rect cell{content.x + 4 + (i % 5) * 36, content.y + 4 + (i / 5) * 36, 32, 32};
                if(SDL_PointInRect(&p, &cell)){
                    gSel = i;
                    return;
                }
            }
            return;
        }
    }

    // canvas — left: place (pencil/eraser) or fill; right: erase (all edit tools)
    if(gPlaytest) return;
    if(!(e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEMOTION || e.type == SDL_MOUSEWHEEL)) return;
    SDL_Point pv{gMouseX, gMouseY};
    SDL_Rect cv{SIDEBAR_W, CANVAS_Y, CANVAS_W, CANVAS_H};
    if(!SDL_PointInRect(&pv, &cv) && e.type != SDL_MOUSEBUTTONUP) return;

    int wx = (int)((gMouseX - SIDEBAR_W) / gCam.zoom) - gCam.x;
    int wy = (int)((gMouseY - CANVAS_Y) / gCam.zoom) - gCam.y;
    int gx = (wx / GRID) * GRID, gy = (wy / GRID) * GRID;

    if(e.type == SDL_MOUSEWHEEL){
        if(e.wheel.y > 0) CmdZoomIn();
        else if(e.wheel.y < 0) CmdZoomOut();
        return;
    }
    if(e.type == SDL_MOUSEBUTTONDOWN){
        if(e.button.button == SDL_BUTTON_LEFT){
            if(gTool == T_SELECT){
                gSelection.clear();
                SDL_Point hit{wx, wy};
                auto& L = gLevel.cur().cur();
                for(int pass = 0; pass < 3; ++pass){
                    Kind want = pass == 0 ? K_NPC : pass == 1 ? K_BGO : K_TILE;
                    for(int i = (int)L.objs.size() - 1; i >= 0; --i){
                        if(L.objs[(size_t)i].kind != want) continue;
                        SDL_Rect rr{L.objs[(size_t)i].x, L.objs[(size_t)i].y, GRID, GRID};
                        if(SDL_PointInRect(&hit, &rr)){
                            gSelection.push_back({gLevel.cur().currentLayer, i});
                            return;
                        }
                    }
                }
                return;
            }
            if(gTool == T_PENCIL || gTool == T_ERASER){
                gDragPlace = true;
                PlaceAt(gx, gy);
            } else if(gTool == T_FILL){
                FillAt(gx, gy);
            }
        } else if(e.button.button == SDL_BUTTON_RIGHT){
            gDragErase = true;
            EraseAt(wx, wy);
        }
    } else if(e.type == SDL_MOUSEMOTION){
        if(gDragPlace && (gTool == T_PENCIL || gTool == T_ERASER)) PlaceAt(gx, gy);
        if(gDragErase) EraseAt(wx, wy);
    } else if(e.type == SDL_MOUSEBUTTONUP){
        if(e.button.button == SDL_BUTTON_LEFT) gDragPlace = false;
        if(e.button.button == SDL_BUTTON_RIGHT) gDragErase = false;
    }
}

static void HandleKey(SDL_Event& e){
    if(e.type!=SDL_KEYDOWN) return;
    if(gModal != 0){
        if(gModal == 1){
            auto k = e.key.keysym.sym;
            if(k == SDLK_ESCAPE || k == SDLK_RETURN || k == SDLK_KP_ENTER) ModalClose();
            return;
        }
        if(gModal == 2 || gModal == 3 || gModal == 4) HandleFileDialogsKey(e);
        return;
    }
    SDL_Keymod m = SDL_GetModState();
    bool ctrl = (m&KMOD_CTRL);
    auto k = e.key.keysym.sym;
    if(k==SDLK_ESCAPE){
        if(gOpenMenu>=0){ gOpenMenu=-1; return; }
        if(gPlaytest) CmdPlaytest();
        return;
    }
    if(ctrl){
        bool shift = (m & KMOD_SHIFT) != 0;
        if(shift && k == SDLK_s){ OpenSaveAsModal(); return; }
        if(k == SDLK_s){ CmdSave(); return; }
        if(k == SDLK_n){ OpenNewConfirmModal(); return; }
        if(k == SDLK_o){ OpenOpenModal(); return; }
        if(k == SDLK_z){ CmdUndo(); return; }
        if(k == SDLK_y){ CmdRedo(); return; }
        if(k == SDLK_x){ CmdCut(); return; }
        if(k == SDLK_c){ CmdCopy(); return; }
        if(k == SDLK_v){ CmdPaste(); return; }
        if(k == SDLK_a){ CmdSelectAll(); return; }
        if(k == SDLK_EQUALS || k == SDLK_PLUS){ CmdZoomIn(); return; }
        if(k == SDLK_MINUS){ CmdZoomOut(); return; }
        if(k == SDLK_0){ gCam.zoom = 1.0f; return; }
        return;
    }
    if(gPlaytest) return;
    if(k==SDLK_p) gTool=T_PENCIL;
    else if(k==SDLK_e) gTool=T_ERASER;
    else if(k==SDLK_f) gTool=T_FILL;
    else if(k==SDLK_s) gTool=T_SELECT;
    else if(k==SDLK_g) gGrid=!gGrid;
    else if(k==SDLK_F5) CmdPlaytest();
    else if(k==SDLK_LEFT)  gCam.x += GRID;
    else if(k==SDLK_RIGHT) gCam.x -= GRID;
    else if(k==SDLK_UP)    gCam.y += GRID;
    else if(k==SDLK_DOWN)  gCam.y -= GRID;
}

// ---------- Font loader ----------
static TTF_Font* TryFont(int pt){
    const char* candidates[] = {
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "C:\\Windows\\Fonts\\arial.ttf",
        "/Library/Fonts/Arial.ttf",
    };
    for(auto* p : candidates){
        TTF_Font* f = TTF_OpenFont(p, pt);
        if(f) return f;
    }
    return nullptr;
}

// ---------- Main ----------
int main(int argc, char** argv){
    if(SDL_Init(SDL_INIT_VIDEO)!=0){ std::fprintf(stderr,"SDL_Init: %s\n",SDL_GetError()); return 1; }
    if(TTF_Init()!=0){ std::fprintf(stderr,"TTF_Init: %s\n",TTF_GetError()); return 1; }
    gWin = SDL_CreateWindow("AC'S Mario fan builder", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIN_W, WIN_H, SDL_WINDOW_SHOWN);
    gRen = SDL_CreateRenderer(gWin, -1, SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
    SDL_SetRenderDrawBlendMode(gRen, SDL_BLENDMODE_BLEND);

    if(argc>1){ gFontSmall=TTF_OpenFont(argv[1],13); gFont=TTF_OpenFont(argv[1],15); gFontTitle=TTF_OpenFont(argv[1],24); }
    if(!gFontSmall){ gFontSmall=TryFont(13); gFont=TryFont(15); gFontTitle=TryFont(24); }
    if(!gFontSmall){ std::fprintf(stderr,"No TTF font found. Pass one as argv[1].\n"); return 2; }

    // Seed a little content so the canvas isn't empty
    auto& L0 = gLevel.cur().cur();
    for(int x=0; x<20; ++x) L0.objs.push_back({K_TILE,0,x*GRID,20*GRID,0,-1,0});
    L0.objs.push_back({K_NPC,0,5*GRID,19*GRID,0,-1,0});
    L0.objs.push_back({K_TILE,9,3*GRID,18*GRID,0,-1,0}); // coin

    BuildToolbar();
    InitMenus();

    Uint32 last = SDL_GetTicks();
    while(gRunning){
        SDL_Event e;
        while(SDL_PollEvent(&e)){
            if(e.type==SDL_QUIT) gRunning=false;
            HandleMouse(e);
            if(e.type == SDL_TEXTINPUT) HandleTextInput(e);
            HandleKey(e);
        }
        if(gPlaytest) PlayerUpdate();

        FillR({0,0,WIN_W,WIN_H}, BG_DARK);
        DrawMenuBar();
        DrawToolbar();
        DrawSidebar();
        DrawCanvas();
        DrawStatusBar();
        DrawMenuDropdown();
        DrawMessageBox();
        DrawFileDialogs();
        SDL_RenderPresent(gRen);

        Uint32 now = SDL_GetTicks();
        Uint32 dt = now-last; last=now;
        if(dt < 1000/FPS) SDL_Delay(1000/FPS - dt);
    }

    delete gPlayer;
    if(gFont) TTF_CloseFont(gFont);
    if(gFontSmall) TTF_CloseFont(gFontSmall);
    if(gFontTitle) TTF_CloseFont(gFontTitle);
    SDL_DestroyRenderer(gRen); SDL_DestroyWindow(gWin);
    TTF_Quit(); SDL_Quit();
    return 0;
}
