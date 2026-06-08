/*
 * video_x720.c
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
 *   - T        : rendu texte ASCII ;
 *   - A        : rendu auto MC6847 simplifie selon X720_CTRL ;
 *   - G        : rendu graphique MC6847 simplifie force ;
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
#define X720_CELL_W         24
#define X720_CELL_H         9
#define X720_MARGIN_X       12
#define X720_MARGIN_Y       12
#define X720_PANEL_W        (X720_COLS * X720_CELL_W)
#define X720_PANEL_H        (X720_ROWS_ZOOM * X720_CELL_H)

/* Taille de la zone TV logique : 256 x 192, agrandie par X720_GFX_SCALE.
 * La grille debug 32 x 64 utilise les memes dimensions finales.
 */
#define X720_WIN_W          (X720_MARGIN_X * 2 + X720_GFX_W * X720_GFX_SCALE)
#define X720_WIN_H          (X720_MARGIN_Y * 2 + X720_GFX_H * X720_GFX_SCALE)

extern byte X720_VRAM[X720_VRAM_SIZE];
extern byte X720_GetCtrl(void);

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

/*
 * 0 = valeurs brutes
 * 1 = actif seulement
 * 2 = texte ASCII simple
 * 3 = auto MC6847 simplifie selon X720_CTRL
 * 4 = graphique MC6847 simplifie force
 */
static int           x720_color_mode = 3;   /* mode automatique par defaut */


/*
 * Ecran graphique logique X-720.
 *
 * Pour avancer proprement, on ne force plus l'affichage graphique a relire
 * toute la VRAM brute. Les routines ROM X-720 savent deja calculer les
 * coordonnees. x07.c signale les ecritures graphiques importantes
 * (notamment PC=A72E pour PSET/PRESET) avec les coordonnees systeme
 * 04C6/04C8 et la couleur courante 04E5.
 */
#define X720_GFX_W          256
#define X720_GFX_H          192
#define X720_GFX_SCALE      3

#define X720_TEXT_CELL_W    (X720_GFX_W * X720_GFX_SCALE / 32)
#define X720_TEXT_CELL_H    (X720_GFX_H * X720_GFX_SCALE / 16)
#define X720_TEXT_PANEL_W   (X720_GFX_W * X720_GFX_SCALE)
#define X720_TEXT_PANEL_H   (X720_GFX_H * X720_GFX_SCALE)

static byte          x720_pixels[X720_GFX_H][X720_GFX_W];
static int           x720_pixels_active = 0;


/* Prototypes locaux utilises avant leur definition. */
void X720_Video_MarkDirty(void);

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


void X720_GfxClear(byte color)
{
    int y, x;

    for (y = 0; y < X720_GFX_H; y++) {
        for (x = 0; x < X720_GFX_W; x++)
            x720_pixels[y][x] = color;
    }

    x720_pixels_active = (color != 0x00) ? (X720_GFX_W * X720_GFX_H) : 0;
    X720_Video_MarkDirty();
}

void X720_GfxSetPixel(int x, int y, byte color)
{
    byte old;

    if (x < 0 || x >= X720_GFX_W || y < 0 || y >= X720_GFX_H)
        return;

    old = x720_pixels[y][x];
    x720_pixels[y][x] = color;

    if (old == 0x00 && color != 0x00)
        x720_pixels_active++;
    else if (old != 0x00 && color == 0x00 && x720_pixels_active > 0)
        x720_pixels_active--;

    X720_Video_MarkDirty();
}

void X720_GfxWriteFromRom(word logical_addr,
                          word phys_addr,
                          byte old_value,
                          byte new_value,
                          int x,
                          int y,
                          byte fg,
                          byte bg,
                          byte ctrl)
{
    int offset;
    int byte_x;
    int yy;
    int x_base;
    int p;

    (void)phys_addr;
    (void)old_value;
    (void)x;
    (void)y;
    (void)bg;

    /*
     * Pour l'instant, on traite surtout SCREEN 3 / CTRL=72.
     */
    if ((ctrl & 0x40) == 0)
        return;

    if (logical_addr < 0x8000 || logical_addr > 0x8FFF)
        return;

    /*
     * Filtre les gros nettoyages observés pendant SCREEN 3.
     */
    if (old_value == 0x20 && new_value == 0x00)
        return;

    if (old_value == 0x20 && new_value == 0x20)
        return;

    if (old_value == 0x00 && new_value == 0x00)
        return;

    offset = logical_addr - 0x8000;

    /*
     * SCREEN 3 observé :
     * 32 octets par ligne.
     * 1 octet = 4 pixels MC6847 de 2 bits.
     * Mais côté coordonnées BASIC, cela correspond à 8 pas horizontaux.
     */
    byte_x = offset & 0x1F;
    yy     = offset >> 5;

    if (yy < 0 || yy >= 192)
        return;

    x_base = byte_x * 8;

    /*
     * Décode les 4 groupes de 2 bits.
     * Chaque groupe est affiché sur 2 pixels horizontaux pour respecter
     * l'échelle BASIC observée : X=8 -> ADDR=8001.
     */
    for (p = 0; p < 4; p++) {
        int shift = 6 - (p * 2);
        int v = (new_value >> shift) & 0x03;

        if (v != 0) {
            byte color = fg ? fg : (byte)v;

            X720_GfxSetPixel(x_base + p * 2,     yy, color);
            X720_GfxSetPixel(x_base + p * 2 + 1, yy, color);
        }
    }

    X720_Video_MarkDirty();
}

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

static const char *X720_RenderModeName(void)
{
    switch (x720_color_mode) {
        case 0: return "valeurs";
        case 1: return "actif";
        case 2: return "texte";
        case 3: return "auto";
        case 4: return "graphique";
        default: return "?";
    }
}

static word X720_AutoPlaneBase(byte ctrl)
{
    /*
     * Bit +1K du registre 90h : redirection logique 8000h -> physique 8400h.
     * CTRL=23 : SCREEN 1 texte, +1K actif -> plan 8400h.
     * CTRL=72 : SCREEN 3 graphique, +1K actif -> plan 8400h.
     * CTRL=03 : sans +1K -> plan 8000h.
     */
    return (ctrl & 0x20) ? X720_PLANE_8400 : X720_PLANE_8000;
}

static int X720_AutoIsGraphics(byte ctrl)
{
    /* AG observe sur OUT 90h : CTRL=72 -> graphique, CTRL=23 -> texte. */
    return (ctrl & 0x40) ? 1 : 0;
}

static const char *X720_AutoKindName(byte ctrl)
{
    return X720_AutoIsGraphics(ctrl) ? "auto-graph" : "auto-texte";
}

static void X720_SetWindowTitle(void)
{
    char title[128];

    if (x720_window == NULL)
        return;

    if (x720_color_mode == 3) {
        byte ctrl = X720_GetCtrl();
        word auto_base = X720_AutoPlaneBase(ctrl);

        snprintf(title, sizeof(title),
                 "Canon X-07 - X-720 TV - AUTO %s base=%04X CTRL=%02X",
                 X720_AutoKindName(ctrl),
                 0x8000 + auto_base,
                 ctrl);
    } else {
        snprintf(title, sizeof(title),
                 "Canon X-07 - X-720 TV - plan %s - mode %s - CTRL=%02X",
                 x720_plane_name[x720_view_plane],
                 X720_RenderModeName(),
                 X720_GetCtrl());
    }
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



/*
 * Police MC6847 8x12 issue du pilote MAME mc6847.cpp.
 * Source MAME : mc6847_friend_device::vdg_fontdata8x12
 * Licence d'origine : BSD-3-Clause.
 *
 * Organisation :
 *   index 0..63  : jeu MC6847 standard, code caractere = c & 0x3F
 *   index 64..95 : extension minuscules presente dans la table MAME
 */
static const byte X720_MC6847_FONT_8X12[] =
{
0x00, 0x00, 0x00, 0x1C, 0x22, 0x02, 0x1A, 0x2A, 0x2A, 0x1C, 0x00, 0x00, /* @ */
	0x00, 0x00, 0x00, 0x08, 0x14, 0x22, 0x22, 0x3E, 0x22, 0x22, 0x00, 0x00, /* A */
	0x00, 0x00, 0x00, 0x3C, 0x12, 0x12, 0x1C, 0x12, 0x12, 0x3C, 0x00, 0x00, /* B */
	0x00, 0x00, 0x00, 0x1C, 0x22, 0x20, 0x20, 0x20, 0x22, 0x1C, 0x00, 0x00, /* C */
	0x00, 0x00, 0x00, 0x3C, 0x12, 0x12, 0x12, 0x12, 0x12, 0x3C, 0x00, 0x00, /* D */
	0x00, 0x00, 0x00, 0x3E, 0x20, 0x20, 0x3C, 0x20, 0x20, 0x3E, 0x00, 0x00, /* E */
	0x00, 0x00, 0x00, 0x3E, 0x20, 0x20, 0x3C, 0x20, 0x20, 0x20, 0x00, 0x00, /* F */
	0x00, 0x00, 0x00, 0x1E, 0x20, 0x20, 0x26, 0x22, 0x22, 0x1E, 0x00, 0x00, /* G */
	0x00, 0x00, 0x00, 0x22, 0x22, 0x22, 0x3E, 0x22, 0x22, 0x22, 0x00, 0x00, /* H */
	0x00, 0x00, 0x00, 0x1C, 0x08, 0x08, 0x08, 0x08, 0x08, 0x1C, 0x00, 0x00, /* I */
	0x00, 0x00, 0x00, 0x02, 0x02, 0x02, 0x02, 0x22, 0x22, 0x1C, 0x00, 0x00, /* J */
	0x00, 0x00, 0x00, 0x22, 0x24, 0x28, 0x30, 0x28, 0x24, 0x22, 0x00, 0x00, /* K */
	0x00, 0x00, 0x00, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x3E, 0x00, 0x00, /* L */
	0x00, 0x00, 0x00, 0x22, 0x36, 0x2A, 0x2A, 0x22, 0x22, 0x22, 0x00, 0x00, /* M */
	0x00, 0x00, 0x00, 0x22, 0x32, 0x2A, 0x26, 0x22, 0x22, 0x22, 0x00, 0x00, /* N */
	0x00, 0x00, 0x00, 0x3E, 0x22, 0x22, 0x22, 0x22, 0x22, 0x3E, 0x00, 0x00, /* O */
	0x00, 0x00, 0x00, 0x3C, 0x22, 0x22, 0x3C, 0x20, 0x20, 0x20, 0x00, 0x00, /* P */
	0x00, 0x00, 0x00, 0x1C, 0x22, 0x22, 0x22, 0x2A, 0x24, 0x1A, 0x00, 0x00, /* Q */
	0x00, 0x00, 0x00, 0x3C, 0x22, 0x22, 0x3C, 0x28, 0x24, 0x22, 0x00, 0x00, /* R */
	0x00, 0x00, 0x00, 0x1C, 0x22, 0x10, 0x08, 0x04, 0x22, 0x1C, 0x00, 0x00, /* S */
	0x00, 0x00, 0x00, 0x3E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x00, /* T */
	0x00, 0x00, 0x00, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x1C, 0x00, 0x00, /* U */
	0x00, 0x00, 0x00, 0x22, 0x22, 0x22, 0x14, 0x14, 0x08, 0x08, 0x00, 0x00, /* V */
	0x00, 0x00, 0x00, 0x22, 0x22, 0x22, 0x2A, 0x2A, 0x36, 0x22, 0x00, 0x00, /* W */
	0x00, 0x00, 0x00, 0x22, 0x22, 0x14, 0x08, 0x14, 0x22, 0x22, 0x00, 0x00, /* X */
	0x00, 0x00, 0x00, 0x22, 0x22, 0x14, 0x08, 0x08, 0x08, 0x08, 0x00, 0x00, /* Y */
	0x00, 0x00, 0x00, 0x3E, 0x02, 0x04, 0x08, 0x10, 0x20, 0x3E, 0x00, 0x00, /* Z */
	0x00, 0x00, 0x00, 0x38, 0x20, 0x20, 0x20, 0x20, 0x20, 0x38, 0x00, 0x00, /* [ */
	0x00, 0x00, 0x00, 0x20, 0x20, 0x10, 0x08, 0x04, 0x02, 0x02, 0x00, 0x00, /* \ */
	0x00, 0x00, 0x00, 0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E, 0x00, 0x00, /* ] */
	0x00, 0x00, 0x00, 0x08, 0x1C, 0x2A, 0x08, 0x08, 0x08, 0x08, 0x00, 0x00, /* up arrow */
	0x00, 0x00, 0x00, 0x00, 0x08, 0x10, 0x3E, 0x10, 0x08, 0x00, 0x00, 0x00, /* left arrow */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* space */
	0x00, 0x00, 0x00, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x08, 0x00, 0x00, /* ! */
	0x00, 0x00, 0x00, 0x14, 0x14, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* " */
	0x00, 0x00, 0x00, 0x14, 0x14, 0x36, 0x00, 0x36, 0x14, 0x14, 0x00, 0x00, /* # */
	0x00, 0x00, 0x00, 0x08, 0x1E, 0x20, 0x1C, 0x02, 0x3C, 0x08, 0x00, 0x00, /* $ */
	0x00, 0x00, 0x00, 0x32, 0x32, 0x04, 0x08, 0x10, 0x26, 0x26, 0x00, 0x00, /* % */
	0x00, 0x00, 0x00, 0x10, 0x28, 0x28, 0x10, 0x2A, 0x24, 0x1A, 0x00, 0x00, /* & */
	0x00, 0x00, 0x00, 0x18, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* ' */
	0x00, 0x00, 0x00, 0x08, 0x10, 0x20, 0x20, 0x20, 0x10, 0x08, 0x00, 0x00, /* ( */
	0x00, 0x00, 0x00, 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08, 0x00, 0x00, /* ) */
	0x00, 0x00, 0x00, 0x00, 0x08, 0x1C, 0x3E, 0x1C, 0x08, 0x00, 0x00, 0x00, /* * */
	0x00, 0x00, 0x00, 0x00, 0x08, 0x08, 0x3E, 0x08, 0x08, 0x00, 0x00, 0x00, /* + */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x30, 0x10, 0x20, 0x00, 0x00, /* , */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3E, 0x00, 0x00, 0x00, 0x00, 0x00, /* - */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x30, 0x00, 0x00, /* . */
	0x00, 0x00, 0x00, 0x02, 0x02, 0x04, 0x08, 0x10, 0x20, 0x20, 0x00, 0x00, /* / */
	0x00, 0x00, 0x00, 0x18, 0x24, 0x24, 0x24, 0x24, 0x24, 0x18, 0x00, 0x00, /* 0 */
	0x00, 0x00, 0x00, 0x08, 0x18, 0x08, 0x08, 0x08, 0x08, 0x1C, 0x00, 0x00, /* 1 */
	0x00, 0x00, 0x00, 0x1C, 0x22, 0x02, 0x1C, 0x20, 0x20, 0x3E, 0x00, 0x00, /* 2 */
	0x00, 0x00, 0x00, 0x1C, 0x22, 0x02, 0x0C, 0x02, 0x22, 0x1C, 0x00, 0x00, /* 3 */
	0x00, 0x00, 0x00, 0x04, 0x0C, 0x14, 0x3E, 0x04, 0x04, 0x04, 0x00, 0x00, /* 4 */
	0x00, 0x00, 0x00, 0x3E, 0x20, 0x3C, 0x02, 0x02, 0x22, 0x1C, 0x00, 0x00, /* 5 */
	0x00, 0x00, 0x00, 0x1C, 0x20, 0x20, 0x3C, 0x22, 0x22, 0x1C, 0x00, 0x00, /* 6 */
	0x00, 0x00, 0x00, 0x3E, 0x02, 0x04, 0x08, 0x10, 0x20, 0x20, 0x00, 0x00, /* 7 */
	0x00, 0x00, 0x00, 0x1C, 0x22, 0x22, 0x1C, 0x22, 0x22, 0x1C, 0x00, 0x00, /* 8 */
	0x00, 0x00, 0x00, 0x1C, 0x22, 0x22, 0x1E, 0x02, 0x02, 0x1C, 0x00, 0x00, /* 9 */
	0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, /* : */
	0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x08, 0x10, 0x00, 0x00, /* ; */
	0x00, 0x00, 0x00, 0x04, 0x08, 0x10, 0x20, 0x10, 0x08, 0x04, 0x00, 0x00, /* < */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x3E, 0x00, 0x3E, 0x00, 0x00, 0x00, 0x00, /* = */
	0x00, 0x00, 0x00, 0x10, 0x08, 0x04, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00, /* > */
	0x00, 0x00, 0x00, 0x18, 0x24, 0x04, 0x08, 0x08, 0x00, 0x08, 0x00, 0x00, /* ? */

	/* Lower case */
	0x00, 0x00, 0x00, 0x0C, 0x12, 0x10, 0x38, 0x10, 0x12, 0x3C, 0x00, 0x00, /* ^ */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x1C, 0x02, 0x1E, 0x22, 0x1E, 0x00, 0x00, /* a */
	0x00, 0x00, 0x00, 0x20, 0x20, 0x3C, 0x22, 0x22, 0x22, 0x3C, 0x00, 0x00, /* b */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x1C, 0x20, 0x20, 0x20, 0x1C, 0x00, 0x00, /* c */
	0x00, 0x00, 0x00, 0x02, 0x02, 0x1E, 0x22, 0x22, 0x22, 0x1E, 0x00, 0x00, /* d */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x1C, 0x22, 0x3E, 0x20, 0x1C, 0x00, 0x00, /* e */
	0x00, 0x00, 0x00, 0x0C, 0x12, 0x10, 0x38, 0x10, 0x10, 0x10, 0x00, 0x00, /* f */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x1E, 0x22, 0x22, 0x22, 0x1E, 0x02, 0x1C, /* g */
	0x00, 0x00, 0x00, 0x20, 0x20, 0x3C, 0x22, 0x22, 0x22, 0x22, 0x00, 0x00, /* h */
	0x00, 0x00, 0x00, 0x08, 0x00, 0x18, 0x08, 0x08, 0x08, 0x1C, 0x00, 0x00, /* i */
	0x00, 0x00, 0x00, 0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x24, 0x18, /* j */
	0x00, 0x00, 0x00, 0x20, 0x20, 0x24, 0x28, 0x38, 0x24, 0x22, 0x00, 0x00, /* k */
	0x00, 0x00, 0x00, 0x18, 0x08, 0x08, 0x08, 0x08, 0x08, 0x1C, 0x00, 0x00, /* l */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x3C, 0x2A, 0x2A, 0x2A, 0x2A, 0x00, 0x00, /* m */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x2C, 0x32, 0x22, 0x22, 0x22, 0x00, 0x00, /* n */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x1C, 0x22, 0x22, 0x22, 0x1C, 0x00, 0x00, /* o */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x3C, 0x22, 0x22, 0x22, 0x3C, 0x20, 0x20, /* p */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x1E, 0x22, 0x22, 0x22, 0x1E, 0x02, 0x03, /* q */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x2C, 0x32, 0x20, 0x20, 0x20, 0x00, 0x00, /* r */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x1E, 0x20, 0x1C, 0x02, 0x3C, 0x00, 0x00, /* s */
	0x00, 0x00, 0x00, 0x10, 0x3C, 0x10, 0x10, 0x10, 0x12, 0x0C, 0x00, 0x00, /* t */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x22, 0x22, 0x22, 0x26, 0x1A, 0x00, 0x00, /* u */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x22, 0x22, 0x14, 0x14, 0x08, 0x00, 0x00, /* v */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x22, 0x2A, 0x2A, 0x1C, 0x14, 0x00, 0x00, /* w */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x22, 0x14, 0x08, 0x14, 0x22, 0x00, 0x00, /* x */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x22, 0x22, 0x22, 0x22, 0x1E, 0x02, 0x1C, /* y */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x3E, 0x04, 0x08, 0x10, 0x3E, 0x00, 0x00, /* z */
	0x00, 0x00, 0x00, 0x08, 0x10, 0x10, 0x20, 0x10, 0x10, 0x08, 0x00, 0x00, /* { */
	0x00, 0x00, 0x00, 0x08, 0x08, 0x08, 0x00, 0x08, 0x08, 0x08, 0x00, 0x00, /* | */
	0x00, 0x00, 0x00, 0x08, 0x04, 0x04, 0x02, 0x04, 0x04, 0x08, 0x00, 0x00, /* } */
	0x00, 0x00, 0x00, 0x08, 0x08, 0x08, 0x08, 0x2A, 0x1C, 0x08, 0x00, 0x00, /* ~ */
	0x00, 0x00, 0x00, 0x08, 0x04, 0x3E, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00  /* _ */
};


static int X720_FontIndex(unsigned char c, int *inverse)
{
    unsigned char cc = (unsigned char)(c & 0x7F);

    if (inverse)
        *inverse = (c & 0x80) ? 1 : 0;

    /*
     * Le MC6847 standard masque les caracteres sur 6 bits.
     * Cela donne bien :
     *   0x40 '@' -> index 0
     *   0x41 'A' -> index 1
     *   0x20 ' ' -> index 32
     *   0x30 '0' -> index 48
     *
     * Pour les codes ASCII minuscules 0x60-0x7F, on utilise la partie
     * basse de la table MAME afin de faciliter les tests modernes.
     */
    if (cc >= 0x60 && cc <= 0x7F)
        return 64 + (cc - 0x60);

    return cc & 0x3F;
}

static void X720_DrawGlyph(int x, int y, unsigned char c)
{
    int gy, gx;
    int inverse = 0;
    int idx = X720_FontIndex(c, &inverse);
    const byte *glyph = &X720_MC6847_FONT_8X12[idx * 12];

    int px_w = X720_TEXT_CELL_W / 8;
    int px_h = X720_TEXT_CELL_H / 12;

    if (px_w < 1) px_w = 1;
    if (px_h < 1) px_h = 1;

    SDL_SetRenderDrawColor(x720_renderer, 0, 255, 0, 255);

    for (gy = 0; gy < 12; gy++) {
        byte row = glyph[gy];

        /*
         * La fonte MC6847 stocke des formes sur 6 bits utiles.
         * On les centre dans une cellule de 8 pixels.
         */
        for (gx = 0; gx < 8; gx++) {
            int bit;

            if (gx == 0 || gx == 7)
                bit = 0;
            else
                bit = (row & (1 << (6 - gx))) ? 1 : 0;  /* gx 1..6 -> bits 5..0 */

            if (inverse)
                bit = !bit;

            if (bit) {
                SDL_Rect r;
                r.x = x + gx * px_w;
                r.y = y + gy * px_h;
                r.w = px_w;
                r.h = px_h;
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
        SDL_Rect bg = { X720_MARGIN_X, X720_MARGIN_Y, X720_TEXT_PANEL_W, X720_TEXT_PANEL_H };
        SDL_RenderFillRect(x720_renderer, &bg);
    }

    for (y = 0; y < 16; y++) {
        for (x = 0; x < 32; x++) {
            byte v = X720_ReadPlaneByte(base, x, y);
            if (((v & 0x7F) != 0x20) || (v & 0x80)) {
                if (active_count)
                    (*active_count)++;
                X720_DrawGlyph(X720_MARGIN_X + x * X720_TEXT_CELL_W,
                               X720_MARGIN_Y + y * X720_TEXT_CELL_H,
                               (unsigned char)v);
            }
        }
    }

    {
        SDL_Rect border = { X720_MARGIN_X - 1, X720_MARGIN_Y - 1, X720_TEXT_PANEL_W + 1, X720_TEXT_PANEL_H + 1 };
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



static void X720_SetLogicalPixelColor(byte color)
{
    static const unsigned char pal[9][3] = {
        {  0,   0,   0},  /* 0 : fond */
        {  0, 220,   0},  /* 1 : vert */
        {255, 230,   0},  /* 2 : jaune */
        {  0, 120, 255},  /* 3 : bleu */
        {255,  70,  40},  /* 4 : rouge */
        {230, 210, 160},  /* 5 : buff */
        {  0, 220, 220},  /* 6 : cyan */
        {255,  70, 255},  /* 7 : magenta */
        {255, 150,   0}   /* 8 : orange */
    };

    if (color > 8)
        color = 8;

    SDL_SetRenderDrawColor(x720_renderer, pal[color][0], pal[color][1], pal[color][2], 255);
}

static void X720_DrawLogicalPixels(int *active_count)
{
    int x, y;
    SDL_Rect r;

    if (active_count)
        *active_count = x720_pixels_active;

    SDL_SetRenderDrawColor(x720_renderer, 0, 0, 0, 255);
    r.x = X720_MARGIN_X;
    r.y = X720_MARGIN_Y;
    r.w = X720_GFX_W * X720_GFX_SCALE;
    r.h = X720_GFX_H * X720_GFX_SCALE;
    SDL_RenderFillRect(x720_renderer, &r);

    for (y = 0; y < X720_GFX_H; y++) {
        for (x = 0; x < X720_GFX_W; x++) {
            byte color = x720_pixels[y][x];
            if (color == 0x00)
                continue;

            X720_SetLogicalPixelColor(color);
            r.x = X720_MARGIN_X + x * X720_GFX_SCALE;
            r.y = X720_MARGIN_Y + y * X720_GFX_SCALE;
            r.w = X720_GFX_SCALE;
            r.h = X720_GFX_SCALE;
            SDL_RenderFillRect(x720_renderer, &r);
        }
    }

    r.x = X720_MARGIN_X - 1;
    r.y = X720_MARGIN_Y - 1;
    r.w = X720_GFX_W * X720_GFX_SCALE + 1;
    r.h = X720_GFX_H * X720_GFX_SCALE + 1;
    SDL_SetRenderDrawColor(x720_renderer, 150, 150, 150, 255);
    SDL_RenderDrawRect(x720_renderer, &r);
}

static void X720_SetMC6847PaletteColor(int idx)
{
    /* Palette simplifiee inspiree MC6847 : vert, jaune, bleu, rouge, buff, cyan, magenta, orange, noir. */
    static const unsigned char pal[9][3] = {
        {  0, 210,   0},  /* vert */
        {255, 230,   0},  /* jaune */
        {  0, 110, 255},  /* bleu */
        {255,  60,  40},  /* rouge */
        {230, 210, 160},  /* buff */
        {  0, 220, 220},  /* cyan */
        {255,  70, 255},  /* magenta */
        {255, 150,   0},  /* orange */
        {  0,   0,   0}   /* noir */
    };

    if (idx < 0 || idx > 8)
        idx = 8;

    SDL_SetRenderDrawColor(x720_renderer, pal[idx][0], pal[idx][1], pal[idx][2], 255);
}

static int X720_GraphicsBitsPerPixel(byte ctrl)
{
    int gm = (ctrl >> 2) & 7;

    /*
     * Premiere approximation : la plupart des modes MC6847 haute resolution
     * utilises par le X-720 se decodent correctement en 1 bit/pixel.
     * Les modes impairs sont testes en 2 bits/pixel pour l'experimentation.
     */
    return (gm & 1) ? 2 : 1;
}

static void X720_DrawMC6847Graphics(word base, byte ctrl, int *active_count)
{
    int y, bx, bit;
    int scale = 4;
    int bpp = X720_GraphicsBitsPerPixel(ctrl);
    int bytes_per_row = (bpp == 1) ? 32 : 64;
    int pixels_per_byte = 8 / bpp;
    int color_base = (ctrl & 0x03) ? 4 : 0;
    SDL_Rect r;

    if (active_count)
        *active_count = 0;

    SDL_SetRenderDrawColor(x720_renderer, 0, 0, 0, 255);
    r.x = X720_MARGIN_X;
    r.y = X720_MARGIN_Y;
    r.w = 256 * scale;
    r.h = 192 * scale;
    SDL_RenderFillRect(x720_renderer, &r);

    for (y = 0; y < 192; y++) {
        for (bx = 0; bx < bytes_per_row; bx++) {
            word off = base + (word)y * bytes_per_row + bx;
            byte v;

            if (off >= X720_VRAM_SIZE)
                continue;

            v = X720_ReadByte(off);

            for (bit = 0; bit < pixels_per_byte; bit++) {
                int shift = 8 - (bit + 1) * bpp;
                int mask = (1 << bpp) - 1;
                int col = (v >> shift) & mask;

                if (col == 0)
                    continue;

                if (active_count)
                    (*active_count)++;

                X720_SetMC6847PaletteColor(color_base + col);
                r.x = X720_MARGIN_X + (bx * pixels_per_byte + bit) * scale;
                r.y = X720_MARGIN_Y + y * scale;
                r.w = scale;
                r.h = scale;
                SDL_RenderFillRect(x720_renderer, &r);
            }
        }
    }

    r.x = X720_MARGIN_X - 1;
    r.y = X720_MARGIN_Y - 1;
    r.w = 256 * scale + 1;
    r.h = 192 * scale + 1;
    SDL_SetRenderDrawColor(x720_renderer, 150, 150, 150, 255);
    SDL_RenderDrawRect(x720_renderer, &r);
}

static word X720_DrawAutoMC6847(int *active_count)
{
    byte ctrl = X720_GetCtrl();
    word base = X720_AutoPlaneBase(ctrl);

    if (X720_AutoIsGraphics(ctrl))
        X720_DrawLogicalPixels(active_count);
    else
        X720_DrawTextPanel(base, active_count);

    return base;
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
    X720_GfxClear(0);

    X720_SetWindowTitle();
    X720_Video_SelfTest();
    X720_Video_Update();

    fprintf(stderr, "[X720 VIDEO] Fenetre TV x2 initialisee (%dx%d), vue %s mode %s\n",
            X720_WIN_W, X720_WIN_H, x720_plane_name[x720_view_plane], X720_RenderModeName());
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

    if (x720_color_mode == 2) {
        X720_DrawTextPanel(base, &active);
    } else if (x720_color_mode == 3) {
        base = X720_DrawAutoMC6847(&active);
    } else if (x720_color_mode == 4) {
        X720_DrawLogicalPixels(&active);
    } else {
        X720_DrawZoomPanel(base, &active);
    }

    /* Pas de LED de debug dans la fenêtre TV. */

    /* Cadre global rouge. */
    {
        SDL_Rect border = { 0, 0, X720_WIN_W - 1, X720_WIN_H - 1 };
        SDL_SetRenderDrawColor(x720_renderer, 255, 0, 0, 255);
        SDL_RenderDrawRect(x720_renderer, &border);
    }

    X720_SetWindowTitle();
    SDL_RenderPresent(x720_renderer);
    x720_dirty = 0;

    if (x720_update_count <= 20 || (x720_update_count % 60) == 0) {
        fprintf(stderr,
                "[X720 VIDEO] Update #%lu view=%s mode=%s rendu=%04X kind=%s active=%d "
                "8000=%02X 8001=%02X 8400=%02X 8401=%02X 8580=%02X 9000=%02X 9001=%02X 9020=%02X 9400=%02X 9401=%02X\n",
                x720_update_count,
                x720_plane_name[x720_view_plane],
                X720_RenderModeName(),
                0x8000 + base,
                (x720_color_mode == 3) ? X720_AutoKindName(X720_GetCtrl()) : "manuel",
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

    /*
     * Evenements de la fenetre X-720.
     * Si l'evenement appartient a cette fenetre, il ne doit pas etre
     * retransmis au clavier du Canon X-07.
     */
    if (event->type == SDL_WINDOWEVENT) {
        if (event->window.windowID != x720_window_id)
            return 0;

        if (event->window.event == SDL_WINDOWEVENT_CLOSE)
            X720_Video_SetEnabled(0);

        return 1;
    }

    /*
     * SDL envoie parfois un SDL_TEXTINPUT apres un SDL_KEYDOWN.
     * Pour eviter qu'un 0/1/2/3 utilise dans la fenetre TV apparaisse
     * aussi sur le LCD X-07, on consomme aussi le TEXTINPUT de cette fenetre.
     */
    if (event->type == SDL_TEXTINPUT) {
        if (event->text.windowID == x720_window_id)
            return 1;
        return 0;
    }

    /* KEYUP de la fenetre TV : consomme pour ne pas toucher TestChr_KeyDown. */
    if (event->type == SDL_KEYUP) {
        if (event->key.windowID == x720_window_id)
            return 1;
        return 0;
    }

    if (event->type == SDL_KEYDOWN) {
        SDL_Keycode k;

        if (event->key.windowID != x720_window_id)
            return 0;

        k = event->key.keysym.sym;

        switch (k) {
            case SDLK_0:
                x720_view_plane = 0;
                break;

            case SDLK_1:
                x720_view_plane = 1;
                break;

            case SDLK_2:
                x720_view_plane = 2;
                break;

            case SDLK_3:
                x720_view_plane = 3;
                break;

            case SDLK_TAB:
                x720_view_plane = (x720_view_plane + 1) % 4;
                break;

            case SDLK_v:
                x720_color_mode = (x720_color_mode + 1) % 5;
                break;

            case SDLK_t:
                x720_color_mode = 2;
                break;

            case SDLK_a:
                x720_color_mode = 3;
                break;

            case SDLK_g:
                x720_color_mode = 4;
                break;

            default:
                /* Touche inconnue, mais elle appartient a la fenetre TV :
                 * on la consomme quand meme pour ne pas polluer le LCD. */
                return 1;
        }

        X720_SetWindowTitle();
        X720_Video_MarkDirty();
        fprintf(stderr, "[X720 VIDEO] Vue=%s mode=%s\n",
                x720_plane_name[x720_view_plane],
                X720_RenderModeName());
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
