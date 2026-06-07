/*

     CCCCCC       A       NNN     NN   OOOOOOOO   NNN     NN 
    CC          AA AA     NN N    NN  OO      OO  NN N    NN
    CC         AA   AA    NN  N   NN  OO      OO  NN  N   NN
    CC        AA    AA    NN   N  NN  OO      OO  NN   N  NN
    CC       AAAAAAAAAA   NN    N NN  OO      OO  NN    N NN
    CC       AA      AA   NN     NNN  OO      OO  NN     NNN
     CCCCCC  AA      AA   NN      NN   OOOOOOOO   NN      NN 

             XX      XX    OOOOOOOO   7777777777
              XX    XX    OO      OO         77
               XX  XX     OO      OO        77
                XXXX      OO      OO       77
               XX  XX     OO      OO      77
              XX    XX    OO      OO     77
             XX      XX    OOOOOOOO     77
           
*/
/*--------------------------------------------------------------------------*/
/*                         History                                          */
/*--------------------------------------------------------------------------*/
/*   Date   | Author    | Vers.  | Changes                                  */
/*----------+-----------+--------+------------------------------------------*/
/* 08/06/26 | L.VALTER  |  0001  | Creation                                 */
/*----------+-----------+--------+------------------------------------------*/
/* video_x720.c
 *
 * Fenetre SDL separee pour la sortie TV / module X-720 du Canon X-07.
 *
 * Version ZOOM x2 debug :
 *   - une seule zone memoire affichee a la fois, avec des cellules plus grandes ;
 *   - touche 0 : plan physique 8000h ;
 *   - touche 1 : plan physique 8400h ;
 *   - touche 2 : plan physique 9000h ;
 *   - touche 3 : plan physique 9400h ;
 *   - TAB      : change de plan ;
 *   - V        : change de mode valeurs/actif ;
 *   - T        : bascule rendu texte ASCII/debug ;
 *   - rafraichissement periodique pour ne pas dependre uniquement du dirty flag.
 */

#include <stdio.h>
#include <SDL2/SDL.h>

#include "Z80.h"
#include "struct.h"
#include "const.h"
#include "var.h"
#include "proto.h"

#ifndef USE_X720_VIDEO
#define USE_X720_VIDEO 1
#endif

#ifndef X720_VIDEO_SELFTEST
#define X720_VIDEO_SELFTEST 0
#endif

#define X720_VRAM_SIZE      0x1800

#define X720_PLANE_8000     0x0000      /* 8000h - 8000h */
#define X720_PLANE_8400     0x0400      /* 8400h - 8000h */
#define X720_PLANE_9000     0x1000      /* 9000h - 8000h */
#define X720_PLANE_9400     0x1400      /* 9400h - 8000h */

/*
 * Vue zoom : 32 octets par ligne.
 * 64 lignes permettent de voir les zones 8400, 8580, 8700, 8880...
 * avec un affichage encore lisible.
 */
#define X720_COLS           32
#define X720_ROWS_ZOOM      64
#define X720_CELL_W         32   /* x2 */
#define X720_CELL_H         16   /* x2 */
#define X720_MARGIN_X       24
#define X720_MARGIN_Y       24
#define X720_PANEL_W        (X720_COLS * X720_CELL_W)
#define X720_PANEL_H        (X720_ROWS_ZOOM * X720_CELL_H)

#define X720_WIN_W          (X720_MARGIN_X * 2 + X720_PANEL_W)
#define X720_WIN_H          (X720_MARGIN_Y * 2 + X720_PANEL_H)

extern byte X720_VRAM[X720_VRAM_SIZE];

static SDL_Window   *x720_window = NULL;
static SDL_Renderer *x720_renderer = NULL;
static int           x720_ready = 0;
static int           x720_dirty = 1;
static int           x720_enabled = USE_X720_VIDEO;
static Uint32        x720_window_id = 0;
static unsigned long x720_update_count = 0;
static Uint32        x720_last_update_ms = 0;

/* 0=8000, 1=8400, 2=9000, 3=9400 */
static int           x720_view_plane = 1;

/* 0=valeurs visibles, 1=actif seulement, 2=texte ASCII. */
static int           x720_color_mode = 2;

static const word x720_plane_base[4] = {
    X720_PLANE_8000,
    X720_PLANE_8400,
    X720_PLANE_9000,
    X720_PLANE_9400
};

static const char *x720_plane_name[4] = {
    "8000h",
    "8400h",
    "9000h",
    "9400h"
};

static byte X720_ReadByte(word off)
{
    if (off < X720_VRAM_SIZE)
        return X720_VRAM[off];
    return 0x00;
}

static byte X720_ReadPlaneByte(word base, int bx, int by)
{
    return X720_ReadByte(base + (word)(by * 0x20 + bx));
}

static int X720_IsActiveValue(byte v)
{
    return (v != 0x00 && v != 0x20);
}

static void X720_SetWindowTitle(void)
{
    char title[128];

    if (x720_window == NULL)
        return;

    snprintf(title, sizeof(title),
             "Canon X-07 - X-720 TV - plan %s - mode %s",
             x720_plane_name[x720_view_plane],
             (x720_color_mode == 0) ? "valeurs" : ((x720_color_mode == 1) ? "actif" : "texte"));
    SDL_SetWindowTitle(x720_window, title);
}

static void X720_SetColorForValue(byte v)
{
    if (x720_color_mode == 1) {
        if (X720_IsActiveValue(v))
            SDL_SetRenderDrawColor(x720_renderer, 255, 255, 0, 255);  /* actif : jaune */
        else
            SDL_SetRenderDrawColor(x720_renderer, 12, 12, 12, 255);   /* fond */
        return;
    }

    /*
     * Mode valeurs : volontairement lisible.
     * 00 et 20 ne veulent pas toujours la meme chose selon le mode,
     * donc on les colore differemment au lieu de les masquer.
     */
    if (v == 0x00) {
        SDL_SetRenderDrawColor(x720_renderer, 0, 80, 220, 255);       /* 00 : bleu */
    } else if (v == 0x20) {
        SDL_SetRenderDrawColor(x720_renderer, 28, 28, 28, 255);       /* 20 : gris tres sombre */
    } else if (v == 0x02) {
        SDL_SetRenderDrawColor(x720_renderer, 255, 255, 0, 255);      /* 02 : jaune */
    } else if (v == 0xFF) {
        SDL_SetRenderDrawColor(x720_renderer, 255, 0, 255, 255);      /* FF : magenta */
    } else {
        SDL_SetRenderDrawColor(x720_renderer, 0, 255, 255, 255);      /* autres : cyan */
    }
}


static void X720_GetGlyphRows(unsigned char c, byte rows[7])
{
    int i;

    for (i = 0; i < 7; i++)
        rows[i] = 0x00;

    if (c >= 'a' && c <= 'z')
        c = (unsigned char)(c - 'a' + 'A');

    switch (c) {
        case 'A': rows[0]=0x0E; rows[1]=0x11; rows[2]=0x11; rows[3]=0x1F; rows[4]=0x11; rows[5]=0x11; rows[6]=0x11; break;
        case 'B': rows[0]=0x1E; rows[1]=0x11; rows[2]=0x11; rows[3]=0x1E; rows[4]=0x11; rows[5]=0x11; rows[6]=0x1E; break;
        case 'C': rows[0]=0x0E; rows[1]=0x11; rows[2]=0x10; rows[3]=0x10; rows[4]=0x10; rows[5]=0x11; rows[6]=0x0E; break;
        case 'D': rows[0]=0x1E; rows[1]=0x11; rows[2]=0x11; rows[3]=0x11; rows[4]=0x11; rows[5]=0x11; rows[6]=0x1E; break;
        case 'E': rows[0]=0x1F; rows[1]=0x10; rows[2]=0x10; rows[3]=0x1E; rows[4]=0x10; rows[5]=0x10; rows[6]=0x1F; break;
        case 'F': rows[0]=0x1F; rows[1]=0x10; rows[2]=0x10; rows[3]=0x1E; rows[4]=0x10; rows[5]=0x10; rows[6]=0x10; break;
        case 'G': rows[0]=0x0E; rows[1]=0x11; rows[2]=0x10; rows[3]=0x17; rows[4]=0x11; rows[5]=0x11; rows[6]=0x0E; break;
        case 'H': rows[0]=0x11; rows[1]=0x11; rows[2]=0x11; rows[3]=0x1F; rows[4]=0x11; rows[5]=0x11; rows[6]=0x11; break;
        case 'I': rows[0]=0x0E; rows[1]=0x04; rows[2]=0x04; rows[3]=0x04; rows[4]=0x04; rows[5]=0x04; rows[6]=0x0E; break;
        case 'J': rows[0]=0x01; rows[1]=0x01; rows[2]=0x01; rows[3]=0x01; rows[4]=0x11; rows[5]=0x11; rows[6]=0x0E; break;
        case 'K': rows[0]=0x11; rows[1]=0x12; rows[2]=0x14; rows[3]=0x18; rows[4]=0x14; rows[5]=0x12; rows[6]=0x11; break;
        case 'L': rows[0]=0x10; rows[1]=0x10; rows[2]=0x10; rows[3]=0x10; rows[4]=0x10; rows[5]=0x10; rows[6]=0x1F; break;
        case 'M': rows[0]=0x11; rows[1]=0x1B; rows[2]=0x15; rows[3]=0x15; rows[4]=0x11; rows[5]=0x11; rows[6]=0x11; break;
        case 'N': rows[0]=0x11; rows[1]=0x19; rows[2]=0x15; rows[3]=0x13; rows[4]=0x11; rows[5]=0x11; rows[6]=0x11; break;
        case 'O': rows[0]=0x0E; rows[1]=0x11; rows[2]=0x11; rows[3]=0x11; rows[4]=0x11; rows[5]=0x11; rows[6]=0x0E; break;
        case 'P': rows[0]=0x1E; rows[1]=0x11; rows[2]=0x11; rows[3]=0x1E; rows[4]=0x10; rows[5]=0x10; rows[6]=0x10; break;
        case 'Q': rows[0]=0x0E; rows[1]=0x11; rows[2]=0x11; rows[3]=0x11; rows[4]=0x15; rows[5]=0x12; rows[6]=0x0D; break;
        case 'R': rows[0]=0x1E; rows[1]=0x11; rows[2]=0x11; rows[3]=0x1E; rows[4]=0x14; rows[5]=0x12; rows[6]=0x11; break;
        case 'S': rows[0]=0x0F; rows[1]=0x10; rows[2]=0x10; rows[3]=0x0E; rows[4]=0x01; rows[5]=0x01; rows[6]=0x1E; break;
        case 'T': rows[0]=0x1F; rows[1]=0x04; rows[2]=0x04; rows[3]=0x04; rows[4]=0x04; rows[5]=0x04; rows[6]=0x04; break;
        case 'U': rows[0]=0x11; rows[1]=0x11; rows[2]=0x11; rows[3]=0x11; rows[4]=0x11; rows[5]=0x11; rows[6]=0x0E; break;
        case 'V': rows[0]=0x11; rows[1]=0x11; rows[2]=0x11; rows[3]=0x11; rows[4]=0x11; rows[5]=0x0A; rows[6]=0x04; break;
        case 'W': rows[0]=0x11; rows[1]=0x11; rows[2]=0x11; rows[3]=0x15; rows[4]=0x15; rows[5]=0x15; rows[6]=0x0A; break;
        case 'X': rows[0]=0x11; rows[1]=0x11; rows[2]=0x0A; rows[3]=0x04; rows[4]=0x0A; rows[5]=0x11; rows[6]=0x11; break;
        case 'Y': rows[0]=0x11; rows[1]=0x11; rows[2]=0x0A; rows[3]=0x04; rows[4]=0x04; rows[5]=0x04; rows[6]=0x04; break;
        case 'Z': rows[0]=0x1F; rows[1]=0x01; rows[2]=0x02; rows[3]=0x04; rows[4]=0x08; rows[5]=0x10; rows[6]=0x1F; break;
        case '0': rows[0]=0x0E; rows[1]=0x11; rows[2]=0x13; rows[3]=0x15; rows[4]=0x19; rows[5]=0x11; rows[6]=0x0E; break;
        case '1': rows[0]=0x04; rows[1]=0x0C; rows[2]=0x04; rows[3]=0x04; rows[4]=0x04; rows[5]=0x04; rows[6]=0x0E; break;
        case '2': rows[0]=0x0E; rows[1]=0x11; rows[2]=0x01; rows[3]=0x02; rows[4]=0x04; rows[5]=0x08; rows[6]=0x1F; break;
        case '3': rows[0]=0x1E; rows[1]=0x01; rows[2]=0x01; rows[3]=0x0E; rows[4]=0x01; rows[5]=0x01; rows[6]=0x1E; break;
        case '4': rows[0]=0x02; rows[1]=0x06; rows[2]=0x0A; rows[3]=0x12; rows[4]=0x1F; rows[5]=0x02; rows[6]=0x02; break;
        case '5': rows[0]=0x1F; rows[1]=0x10; rows[2]=0x10; rows[3]=0x1E; rows[4]=0x01; rows[5]=0x01; rows[6]=0x1E; break;
        case '6': rows[0]=0x0E; rows[1]=0x10; rows[2]=0x10; rows[3]=0x1E; rows[4]=0x11; rows[5]=0x11; rows[6]=0x0E; break;
        case '7': rows[0]=0x1F; rows[1]=0x01; rows[2]=0x02; rows[3]=0x04; rows[4]=0x08; rows[5]=0x08; rows[6]=0x08; break;
        case '8': rows[0]=0x0E; rows[1]=0x11; rows[2]=0x11; rows[3]=0x0E; rows[4]=0x11; rows[5]=0x11; rows[6]=0x0E; break;
        case '9': rows[0]=0x0E; rows[1]=0x11; rows[2]=0x11; rows[3]=0x0F; rows[4]=0x01; rows[5]=0x01; rows[6]=0x0E; break;
        case '>': rows[0]=0x10; rows[1]=0x08; rows[2]=0x04; rows[3]=0x02; rows[4]=0x04; rows[5]=0x08; rows[6]=0x10; break;
        case '<': rows[0]=0x01; rows[1]=0x02; rows[2]=0x04; rows[3]=0x08; rows[4]=0x04; rows[5]=0x02; rows[6]=0x01; break;
        case '-': rows[3]=0x1F; break;
        case '_': rows[6]=0x1F; break;
        case '.': rows[6]=0x04; break;
        case ',': rows[5]=0x04; rows[6]=0x08; break;
        case ':': rows[1]=0x04; rows[5]=0x04; break;
        case ';': rows[1]=0x04; rows[5]=0x04; rows[6]=0x08; break;
        case '(': rows[0]=0x02; rows[1]=0x04; rows[2]=0x08; rows[3]=0x08; rows[4]=0x08; rows[5]=0x04; rows[6]=0x02; break;
        case ')': rows[0]=0x08; rows[1]=0x04; rows[2]=0x02; rows[3]=0x02; rows[4]=0x02; rows[5]=0x04; rows[6]=0x08; break;
        case ' ': break;
        default:
            rows[0]=0x1F; rows[1]=0x11; rows[2]=0x01; rows[3]=0x06; rows[4]=0x04; rows[5]=0x00; rows[6]=0x04;
            break;
    }
}

static void X720_DrawGlyph(int x, int y, unsigned char c)
{
    byte rows[7];
    int gy, gx;
    const int scale = 2;

    if (c < 32 || c > 126)
        return;

    X720_GetGlyphRows(c, rows);

    SDL_SetRenderDrawColor(x720_renderer, 0, 255, 0, 255);

    for (gy = 0; gy < 7; gy++) {
        for (gx = 0; gx < 5; gx++) {
            if (rows[gy] & (1 << (4 - gx))) {
                SDL_Rect r;
                r.x = x + gx * scale;
                r.y = y + gy * scale;
                r.w = scale;
                r.h = scale;
                SDL_RenderFillRect(x720_renderer, &r);
            }
        }
    }
}

static void X720_DrawTextPanel(word base, int *active_count)
{
    int x, y;

    if (active_count)
        *active_count = 0;

    /* Fond uniforme. */
    SDL_SetRenderDrawColor(x720_renderer, 0, 0, 0, 255);
    {
        SDL_Rect bg = { X720_MARGIN_X, X720_MARGIN_Y, X720_PANEL_W, 16 * X720_CELL_H };
        SDL_RenderFillRect(x720_renderer, &bg);
    }

    for (y = 0; y < 16; y++) {
        for (x = 0; x < 32; x++) {
            byte v = X720_ReadPlaneByte(base, x, y);
            if (v >= 32 && v <= 126 && v != 0x20) {
                if (active_count)
                    (*active_count)++;
                X720_DrawGlyph(X720_MARGIN_X + x * X720_CELL_W + 4,
                               X720_MARGIN_Y + y * X720_CELL_H + 1,
                               (unsigned char)v);
            }
        }
    }

    {
        SDL_Rect border = { X720_MARGIN_X - 1, X720_MARGIN_Y - 1, X720_PANEL_W + 1, 16 * X720_CELL_H + 1 };
        SDL_SetRenderDrawColor(x720_renderer, 120, 120, 120, 255);
        SDL_RenderDrawRect(x720_renderer, &border);
    }
}

static void X720_DrawZoomPanel(word base, int *active_count)
{
    int bx, by;

    if (active_count)
        *active_count = 0;

    for (by = 0; by < X720_ROWS_ZOOM; by++) {
        for (bx = 0; bx < X720_COLS; bx++) {
            byte v = X720_ReadPlaneByte(base, bx, by);
            SDL_Rect r;

            if (X720_IsActiveValue(v) && active_count)
                (*active_count)++;

            r.x = X720_MARGIN_X + bx * X720_CELL_W;
            r.y = X720_MARGIN_Y + by * X720_CELL_H;
            r.w = X720_CELL_W - 1;
            r.h = X720_CELL_H - 1;

            X720_SetColorForValue(v);
            SDL_RenderFillRect(x720_renderer, &r);
        }
    }

    /* Grille / cadre du panneau. */
    {
        SDL_Rect border = { X720_MARGIN_X - 1, X720_MARGIN_Y - 1, X720_PANEL_W + 1, X720_PANEL_H + 1 };
        SDL_SetRenderDrawColor(x720_renderer, 150, 150, 150, 255);
        SDL_RenderDrawRect(x720_renderer, &border);
    }
}

void X720_Video_SelfTest(void)
{
#if X720_VIDEO_SELFTEST
    int i;

    if (!x720_ready)
        return;

    for (i = 0; i < X720_VRAM_SIZE; i++)
        X720_VRAM[i] = 0x20;

    X720_VRAM[X720_PLANE_8400 + 0x0000] = 0x00;
    X720_VRAM[X720_PLANE_8400 + 0x0001] = 0x02;
    X720_VRAM[X720_PLANE_8400 + 0x0020] = 0x55;
    X720_VRAM[X720_PLANE_8400 + 0x0021] = 0xAA;

    X720_VRAM[X720_PLANE_9000 + 0x0000] = 0x02;
    X720_VRAM[X720_PLANE_9400 + 0x0000] = 0xFF;

    x720_dirty = 1;
    fprintf(stderr, "[X720 VIDEO] SelfTest actif\n");
#endif
}

int X720_Video_Init(void)
{
#if USE_X720_VIDEO
    if (!x720_enabled)
        return 0;

    if (x720_ready)
        return 1;

    if (!SDL_WasInit(SDL_INIT_VIDEO)) {
        if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
            fprintf(stderr, "[X720 VIDEO] SDL_InitSubSystem failed: %s\n", SDL_GetError());
            x720_enabled = 0;
            return 0;
        }
    }

    x720_window = SDL_CreateWindow("Canon X-07 - X-720 TV",
                                  SDL_WINDOWPOS_CENTERED,
                                  SDL_WINDOWPOS_CENTERED,
                                  X720_WIN_W,
                                  X720_WIN_H,
                                  SDL_WINDOW_SHOWN);
    if (x720_window == NULL) {
        fprintf(stderr, "[X720 VIDEO] SDL_CreateWindow failed: %s\n", SDL_GetError());
        x720_enabled = 0;
        return 0;
    }

    x720_renderer = SDL_CreateRenderer(x720_window, -1,
                                       SDL_RENDERER_ACCELERATED |
                                       SDL_RENDERER_PRESENTVSYNC);
    if (x720_renderer == NULL) {
        fprintf(stderr, "[X720 VIDEO] SDL_CreateRenderer accelerated failed: %s\n", SDL_GetError());
        x720_renderer = SDL_CreateRenderer(x720_window, -1, SDL_RENDERER_SOFTWARE);
    }

    if (x720_renderer == NULL) {
        fprintf(stderr, "[X720 VIDEO] SDL_CreateRenderer software failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(x720_window);
        x720_window = NULL;
        x720_enabled = 0;
        return 0;
    }

    x720_window_id = SDL_GetWindowID(x720_window);
    x720_ready = 1;
    x720_dirty = 1;
    x720_last_update_ms = 0;

    X720_SetWindowTitle();
    X720_Video_SelfTest();
    X720_Video_Update();

    fprintf(stderr, "[X720 VIDEO] Fenetre TV x2 initialisee (%dx%d), vue zoom %s\n",
            X720_WIN_W, X720_WIN_H, x720_plane_name[x720_view_plane]);
    return 1;
#else
    return 0;
#endif
}

void X720_Video_Close(void)
{
    if (x720_renderer != NULL) {
        SDL_DestroyRenderer(x720_renderer);
        x720_renderer = NULL;
    }

    if (x720_window != NULL) {
        SDL_DestroyWindow(x720_window);
        x720_window = NULL;
    }

    x720_window_id = 0;
    x720_ready = 0;
}

void X720_Video_SetEnabled(int enabled)
{
    x720_enabled = enabled ? 1 : 0;
    if (!x720_enabled)
        X720_Video_Close();
}

void X720_Video_MarkDirty(void)
{
    x720_dirty = 1;
}

int X720_Video_IsReady(void)
{
    return x720_ready;
}

void X720_Video_Update(void)
{
    int active = 0;
    word base;

    if (!x720_enabled)
        return;

    if (!x720_ready) {
        if (!X720_Video_Init())
            return;
    }

    if (x720_renderer == NULL)
        return;

    x720_update_count++;
    x720_last_update_ms = SDL_GetTicks();

    base = x720_plane_base[x720_view_plane];

    SDL_SetRenderDrawColor(x720_renderer, 0, 0, 0, 255);
    SDL_RenderClear(x720_renderer);

    if (x720_color_mode == 2)
        X720_DrawTextPanel(base, &active);
    else
        X720_DrawZoomPanel(base, &active);

    /* LED verte : prouve que la fonction update tourne. */
    {
        SDL_Rect led = { 4, 4, 8, 8 };
        SDL_SetRenderDrawColor(x720_renderer, 0, 255, 0, 255);
        SDL_RenderFillRect(x720_renderer, &led);
    }

    /* Cadre global rouge. */
    {
        SDL_Rect border = { 0, 0, X720_WIN_W - 1, X720_WIN_H - 1 };
        SDL_SetRenderDrawColor(x720_renderer, 255, 0, 0, 255);
        SDL_RenderDrawRect(x720_renderer, &border);
    }

    SDL_RenderPresent(x720_renderer);
    x720_dirty = 0;

    if (x720_update_count <= 20 || (x720_update_count % 60) == 0) {
        fprintf(stderr,
                "[X720 VIDEO] Update #%lu view=%s mode=%s active=%d "
                "8000=%02X 8001=%02X 8400=%02X 8401=%02X 8580=%02X 9000=%02X 9001=%02X 9020=%02X 9400=%02X 9401=%02X\n",
                x720_update_count,
                x720_plane_name[x720_view_plane],
                (x720_color_mode == 0) ? "valeurs" : ((x720_color_mode == 1) ? "actif" : "texte"),
                active,
                X720_ReadByte(X720_PLANE_8000 + 0x0000),
                X720_ReadByte(X720_PLANE_8000 + 0x0001),
                X720_ReadByte(X720_PLANE_8400 + 0x0000),
                X720_ReadByte(X720_PLANE_8400 + 0x0001),
                X720_ReadByte(0x0580),
                X720_ReadByte(X720_PLANE_9000 + 0x0000),
                X720_ReadByte(X720_PLANE_9000 + 0x0001),
                X720_ReadByte(X720_PLANE_9000 + 0x0020),
                X720_ReadByte(X720_PLANE_9400 + 0x0000),
                X720_ReadByte(X720_PLANE_9400 + 0x0001));
    }
}

void X720_Video_Service(void)
{
    Uint32 now;

    if (!x720_enabled)
        return;

    if (!x720_ready) {
        X720_Video_Init();
        return;
    }

    now = SDL_GetTicks();

    if (x720_dirty || now - x720_last_update_ms >= 50)
        X720_Video_Update();
}

int X720_Video_HandleEvent(const void *event_ptr)
{
    const SDL_Event *event = (const SDL_Event *)event_ptr;

    if (event == NULL)
        return 0;

    if (!x720_ready || x720_window_id == 0)
        return 0;

    if (event->type == SDL_WINDOWEVENT &&
        event->window.windowID == x720_window_id) {
        if (event->window.event == SDL_WINDOWEVENT_CLOSE)
            X720_Video_SetEnabled(0);
        return 1;
    }

    if (event->type == SDL_KEYDOWN &&
        event->key.windowID == x720_window_id) {
        SDL_Keycode k = event->key.keysym.sym;

        if (k == SDLK_0) {
            x720_view_plane = 0;
        } else if (k == SDLK_1) {
            x720_view_plane = 1;
        } else if (k == SDLK_2) {
            x720_view_plane = 2;
        } else if (k == SDLK_3) {
            x720_view_plane = 3;
        } else if (k == SDLK_TAB) {
            x720_view_plane = (x720_view_plane + 1) % 4;
        } else if (k == SDLK_v) {
            x720_color_mode = (x720_color_mode + 1) % 3;
        } else if (k == SDLK_t) {
            x720_color_mode = 2;
        } else {
            return 1;
        }

        X720_SetWindowTitle();
        X720_Video_MarkDirty();
        fprintf(stderr, "[X720 VIDEO] Vue=%s mode=%s\n",
                x720_plane_name[x720_view_plane],
                (x720_color_mode == 0) ? "valeurs" : ((x720_color_mode == 1) ? "actif" : "texte"));
        return 1;
    }

    return 0;
}

void X720_Video_DebugDump(void)
{
    int bx, by;
    word base = x720_plane_base[x720_view_plane];

    fprintf(stderr, "[X720 VIDEO DUMP] plane %s\n", x720_plane_name[x720_view_plane]);
    for (by = 0; by < X720_ROWS_ZOOM; by++) {
        fprintf(stderr, "%02d:", by);
        for (bx = 0; bx < X720_COLS; bx++) {
            byte v = X720_ReadPlaneByte(base, bx, by);
            fputc(X720_IsActiveValue(v) ? '#' : '.', stderr);
        }
        fputc('\n', stderr);
    }
}
