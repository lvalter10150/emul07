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
/*
                             EMULATEUR

                         Par Edouard FORLER
                    (edouard.forler@di.epfl.ch)
                          Par Sylvain HUET
                    (huet@poly.polytechnique.fr)
                               1997
                         Par Jacques BRIGAUD
                     adaptation UNIX, Octobre 2000

  video.c : routines video de l'emulateur (+gestion memoire)

*/
/*--------------------------------------------------------------------------*/
/*                         History                                          */
/*--------------------------------------------------------------------------*/
/*   Date   | Author    | Vers.  | Changes                                  */
/*----------+-----------+--------+------------------------------------------*/
/* 18/12/00 | J.BRIGAUD |  0001  | Creation                                 */
/* 23/04/24 | L.VALTER  |  0002  | Intégration de la librairie SDL          */
/* 10/05/26 | L.VALTER  |  0003  | Suite de l'intégration video+clavier     */
/*----------+-----------+--------+------------------------------------------*/

#include <stdio.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <sys/time.h>
#include <signal.h>

#include "Z80.h"
#include "struct.h"
#include "const.h"
#include "var.h"
#include "proto.h"
#include "xo7_car.h"

#define NB_POINT_CAR_X 6
#define NB_POINT_CAR_Y 8
//#define MAX_X          120
//#define MAX_Y          32

#define DEBUG_KEYBOARD 0

struct 			itimerval fifty;
struct 			itimerval Old_Timer;
int 			stopkey = 27; // ESC
int				black,white;
char			*nomstat = NULL;
SDL_Window 		*display = NULL;
SDL_Renderer 	*renderer = NULL;
int				screen;
//Colormap		cmap;
//Visual		*visual;
//Windows		root,win;
//GC		 	gc,mygc[256];
short			imac[400];
//XImage		*ima;
//XColor		tCol,tCol_exact;
byte			Ram_Video[MAX_X][MAX_Y];
int				col[16];
int				crayx,crayy;
int				lasttour;
int				quitf;

static FILE *BasicText_File = NULL;
static int BasicText_LastCR = 0;
static Uint32 BasicText_StartMs = 0;
static Uint32 BasicText_NextMs = 0;
static unsigned long BasicText_Count = 0;

int X720_Video_HandleEvent(const void *event);

/*
 * Mode majuscule interne a l'emulateur.
 * 1 = le clavier PC fonctionne comme le Canon X-07 en majuscules.
 * F9 permet de basculer ce mode pendant l'execution.
 * En mode majuscule, SHIFT + lettre permet de saisir une minuscule.
 */
static int X07_CapsMode = 1;
/*
static char		*nom[16]=
{"black","red","green","yellow","blue","magenta","cyan","white",
 "gray","pink","pale green","light yellow","light slate blue","violet"
 ,"light cyan","orange"};
*/
/*----------------------------------------------------------------------------*/
/* Applique le mode majuscule interne du X-07 sur une lettre ASCII.             */
/*                                                                            */
/* Mode majuscule OFF : on respecte le caractere donne par SDL_TEXTINPUT.      */
/* Mode majuscule ON  : lettre => majuscule, sauf SHIFT + lettre => minuscule. */
/*----------------------------------------------------------------------------*/
static byte X07_Key_ApplyCase(byte c, SDL_Keymod mod)
{
	int shift = (mod & KMOD_SHIFT) ? 1 : 0;

	if (!X07_CapsMode)
		return c;

	if (c >= 'a' && c <= 'z') {
		if (!shift)
			c = (byte)(c - 'a' + 'A');
		return c;
	}

	if (c >= 'A' && c <= 'Z') {
		if (shift)
			c = (byte)(c - 'A' + 'a');
		return c;
	}

	return c;
}

/*----------------------------------------------------------------------------*/
/* Pour l'injection TXT, il n'y a pas de SHIFT physique : majuscules si actif. */
/*----------------------------------------------------------------------------*/
static byte X07_TextFile_ApplyCase(byte c)
{
	if (X07_CapsMode && c >= 'a' && c <= 'z')
		c = (byte)(c - 'a' + 'A');

	return c;
}

/*----------------------------------------------------------------------------*/
/*                                                                            */
/*                                                                            */
/*----------------------------------------------------------------------------*/
void iniscreen()
{
	// Init SDL
	if(0 != SDL_Init(SDL_INIT_VIDEO))
	{
		fprintf(stderr, "Erreur SDL_Init : %s", SDL_GetError());
	}
	display = SDL_CreateWindow("Canon X07", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,1081, 289, SDL_WINDOW_SHOWN);
	if(NULL == display)
	{
		fprintf(stderr, "Erreur SDL_CreateWindow : %s", SDL_GetError());
		SDL_DestroyWindow(display);
	}
	renderer = SDL_CreateRenderer(display, -1, SDL_RENDERER_ACCELERATED);
	if(NULL == renderer)
	{
		fprintf(stderr, "Erreur SDL_CreateRenderer : %s", SDL_GetError());
		SDL_DestroyRenderer(renderer);
	}
	lasttour=15;
	SDL_StartTextInput();
}

/*---------------------------------------------------------------------------*/
/*                                                                           */
/*                                                                           */
/*---------------------------------------------------------------------------*/
void Refresh_Video (void)
{
	if (!X07_Screen_Enable)
		return;
	SDL_SetRenderDrawColor(renderer, 211, 211, 211, 255);
	SDL_RenderClear(renderer);
	for (int i=0;i<120;i++) {
		for (int j=0;j<32;j++) {
			if (Ram_Video[i][j] == 0)
			{
				SDL_SetRenderDrawColor(renderer, 211, 211, 211, 255);
			} else {
				SDL_SetRenderDrawColor(renderer, 48, 48, 48, 255);
			}
			SDL_Rect rect = {(i*8)+1+i, (j*8)+1+j, 8,8};
			SDL_RenderFillRect(renderer, &rect);
			// Débogage : afficher les valeurs de Ram_Video
            //fprintf(stderr, "Ram_Video[%d][%d] = %d\n", i, j, Ram_Video[i][j]);
		}
	}
	SDL_RenderPresent(renderer);
}

/*---------------------------------------------------------------------------*/
/*                                                                           */
/*                                                                           */
/*---------------------------------------------------------------------------*/
void Pset (byte x, byte y)
{
#if AFF_CMD_T6834
	fprintf (stderr,"Pset %d,%d ",x,y);
#endif
	Ram_Video[x][y]=1;
	if (!X07_Screen_Enable)
		return;
	SDL_SetRenderDrawColor(renderer, 48, 48, 48, 255);
	SDL_Rect rect = {(x*8)+1+x, (y*8)+1+y, 8,8};
	SDL_RenderFillRect(renderer, &rect);
	SDL_RenderPresent(renderer);
}

/*---------------------------------------------------------------------------*/
/*                                                                           */
/*                                                                           */
/*---------------------------------------------------------------------------*/
void Preset (byte x, byte y)
{
#if AFF_CMD_T6834
	fprintf (stderr,"Preset %d,%d ",x,y);
#endif
	Ram_Video[x][y]=0;
	if (!X07_Screen_Enable)
		return;
	SDL_SetRenderDrawColor(renderer, 211, 211, 211, 255);
	SDL_Rect rect = {(x*8)+1+x, (y*8)+1+y, 8,8};
	SDL_RenderFillRect(renderer, &rect);
	SDL_RenderPresent(renderer);
}

/*----------------------------------------------------------------------------*/
/*                                                                            */
/*                                                                            */
/*----------------------------------------------------------------------------*/
void Add_Key(byte Key)
{
	/*
	 * x07.c surveille les lignes tapees au clavier pour intercepter
	 * certaines commandes cassette comme CLOAD "TOTO".
	 * La touche est ensuite transmise normalement a la ROM.
	 */
	X07_Key_Monitor(Key);

	if (X07_Click_Enable)
		X07_KeyClick();
		
	if (Clavier.Nb_Key < 20) {
		Clavier.Nb_Key++;
		Clavier.Buff_Key[Clavier.Pt_Ecr] = Key & 0xFF;
		Clavier.Pt_Ecr++;

		if (Clavier.Pt_Ecr >= 20)
			Clavier.Pt_Ecr = 0;
	}
}

/*----------------------------------------------------------------------------*/
/*                                                                            */
/*                                                                            */
/*----------------------------------------------------------------------------*/
void Add_FKey (byte F_Key)
{
 byte i;
 if (F_Key >= 12)
		return;
 for (i=3;(i<80) && (General_Info.F_Key [F_Key][i]);i++)
   Add_Key (General_Info.F_Key [F_Key][i]);
}

/*----------------------------------------------------------------------------*/
/*                                                                            */
/*                                                                            */
/*----------------------------------------------------------------------------*/
byte TestChr_IsDown(byte chr)
{
    /*
     * TESTCHR travaille sur le code ASCII du caractère.
     * On accepte majuscule/minuscule pour éviter les surprises SDL.
     */
    if (TestChr_KeyDown[chr])
        return 1;

    if (chr >= 'A' && chr <= 'Z') {
        if (TestChr_KeyDown[chr + 32])
            return 1;
    }

    if (chr >= 'a' && chr <= 'z') {
        if (TestChr_KeyDown[chr - 32])
            return 1;
    }

    return 0;
}

void TestChr_Clear(byte chr)
{
    TestChr_KeyDown[chr] = 0;

    if (chr >= 'A' && chr <= 'Z')
        TestChr_KeyDown[chr + 32] = 0;

    if (chr >= 'a' && chr <= 'z')
        TestChr_KeyDown[chr - 32] = 0;
}

/*----------------------------------------------------------------------------*/
/* Vide le buffer clavier                                                       */
/*----------------------------------------------------------------------------*/
void Clear_KeyBuffer(void)
{
	memset((void *)&Clavier, 0, sizeof(Clavier));
}

/*----------------------------------------------------------------------------*/
/*                                                                            */
/*                                                                            */
/*----------------------------------------------------------------------------*/
int Voir_Xevent()
{
	SDL_Event event;
	int ret = 0;
	static int Ignore_TextInput_Count = 0;
	
	while (SDL_PollEvent(&event)) {
		/*
		 * La fenetre TV X-720 consomme ses propres evenements SDL.
		 * Si elle retourne 1, on ne transmet pas la touche au clavier X-07.
		 */
		if (X720_Video_HandleEvent(&event))
			continue;

		switch(event.type) {
			case SDL_QUIT:
				ret = 27;
				break;

			case SDL_WINDOWEVENT:
				if (event.window.event == SDL_WINDOWEVENT_CLOSE)
					ret = 27;
				break;

			case SDL_TEXTINPUT:
				if (Ignore_TextInput_Count > 0) {
					Ignore_TextInput_Count--;
					break;
				}

				#if DEBUG_KEYBOARD
					fprintf(stderr, "[SDL_TEXTINPUT] text=\"%s\"", event.text.text);
					for (int i = 0; event.text.text[i] != 0; i++)
						fprintf(stderr, " 0x%02X", (unsigned char)event.text.text[i]);
					fprintf(stderr, "\n");
				#endif

				{
					SDL_Keymod mod = SDL_GetModState();

					for (int i = 0; event.text.text[i] != 0; i++)
						Add_Key(X07_Key_ApplyCase((byte)event.text.text[i], mod));
				}
				break;
			case SDL_KEYUP:
			{
				SDL_Keycode k;
				k = event.key.keysym.sym;
				
				if (k >= 0 && k < 256) {
					TestChr_KeyDown[(byte)k] = 0;

					if (k >= 'a' && k <= 'z')
						TestChr_KeyDown[(byte)(k - 32)] = 0;

					if (k >= 'A' && k <= 'Z')
						TestChr_KeyDown[(byte)(k + 32)] = 0;
				}
			
				switch(k) {
					case SDLK_UP:
					case SDLK_DOWN:
					case SDLK_LEFT:
					case SDLK_RIGHT:
						General_Info.Stick = 0x30;
						break;
					case SDLK_F6:
						General_Info.Strig = 0xFF;
						break;
					case SDLK_SPACE:
						General_Info.Strig1 = 0xFF;
					break;

					default:
						break;
				}
				break;
			}

			case SDL_KEYDOWN:
				SDL_Keycode k = event.key.keysym.sym;

				if (k >= 0 && k < 256) {
					TestChr_KeyDown[(byte)k] = 1;
					if (k >= 'a' && k <= 'z')
						TestChr_KeyDown[(byte)(k - 32)] = 1;
					if (k >= 'A' && k <= 'Z')
						TestChr_KeyDown[(byte)(k + 32)] = 1;
				}
			
				if (event.key.repeat && !X07_Repeat_Enable) {
					if (k >= 32 && k < 127)
						Ignore_TextInput_Count++;
					break;
				}

				// CTRL+R = RESET matériel du X-07
				if ((event.key.keysym.sym == SDLK_r) &&
					(event.key.keysym.mod & KMOD_CTRL)) {
					Reset_Xo7();
					ret = 0;
					break;
				}
				
				// CTRL+Q = quitter l'émulateur
				if ((event.key.keysym.sym == SDLK_q) &&
					(event.key.keysym.mod & KMOD_CTRL)) {
					ret = 27;
					break;
				}
				
				// Touche HOME/CLR
				if (event.key.keysym.sym == SDLK_HOME) {
					if (event.key.keysym.mod & KMOD_SHIFT) {
						Add_Key(0x0C);
					} else {
						Add_Key(0x0B);
					}
					break;
				}

				if (event.key.keysym.sym >= 0 && event.key.keysym.sym < 256) {
					TestChr_KeyDown[(byte)event.key.keysym.sym] = 1;
				}

				switch(event.key.keysym.sym) {
					case SDLK_ESCAPE:
						ret = 27;
						break;

					case SDLK_RETURN:
					case SDLK_KP_ENTER:
						Add_Key('\r');
						break;

					case SDLK_BACKSPACE:
						Add_Key(0x08);
						break;

					case SDLK_TAB:
						Add_Key('\t');
						break;

					case SDLK_DELETE:
						Add_Key(0x7F);
						break;

					case SDLK_LEFT:
						General_Info.Stick = 0x37;
						Add_Key(0x1D);
						break;

					case SDLK_RIGHT:
						General_Info.Stick = 0x33;
						Add_Key(0x1C);
						break;

					case SDLK_UP:
						General_Info.Stick = 0x31;
						Add_Key(0x1E);
						break;

					case SDLK_DOWN:
						General_Info.Stick = 0x35;
						Add_Key(0x1F);
						break;

					case SDLK_HOME:
						Add_Key(0x0B);
						break;

					case SDLK_END:
						Add_Key(0x05);
						break;

					case SDLK_INSERT:
						Add_Key(0x12);
						break;

					case SDLK_F1:
						if (event.key.keysym.mod & KMOD_SHIFT)
							Add_FKey(6);   /* Canon F7 */
						else
							Add_FKey(0);   /* Canon F1 */
						break;

					case SDLK_F2:
						if (event.key.keysym.mod & KMOD_SHIFT)
							Add_FKey(7);   /* Canon F8 */
						else
							Add_FKey(1);   /* Canon F2 */
						break;

					case SDLK_F3:
						if (event.key.keysym.mod & KMOD_SHIFT)
							Add_FKey(8);   /* Canon F9 */
						else
							Add_FKey(2);   /* Canon F3 */
						break;

					case SDLK_F4:
						if (event.key.keysym.mod & KMOD_SHIFT)
							Add_FKey(9);   /* Canon F10 */
						else
							Add_FKey(3);   /* Canon F4 */
						break;

					case SDLK_F5:
						if (event.key.keysym.mod & KMOD_SHIFT)
							Add_FKey(10);  /* Canon F11 */
						else
							Add_FKey(4);   /* Canon F5 */
						break;

					case SDLK_F6:
						if (X07_Click_Enable)
							X07_KeyClick();
						General_Info.Strig = 0x00;
						if (event.key.keysym.mod & KMOD_SHIFT)
							Add_FKey(11);  /* Canon F12 */
						else
							Add_FKey(5);   /* Canon F6 */
						break;

					case SDLK_F9:
						X07_CapsMode = !X07_CapsMode;
						fprintf(stderr, "[KEYBOARD] Mode majuscule X07=%s\n",
							X07_CapsMode ? "ON" : "OFF");
						break;

					case SDLK_F11:
						Power_Off_Xo7();
						break;

					case SDLK_F12:
						Power_OnBreak_Xo7();
						break;
					
					case SDLK_SPACE:
						General_Info.Strig1 = 0x00;
						break;

					default:
						/*
						 * Ne surtout pas faire :
						 * ret = event.key.keysym.sym;
						 *
						 * Les lettres doivent passer uniquement par SDL_TEXTINPUT.
						 */
						break;
				}
				break;

			default:
				break;
		}
	}

	return ret;
}

/*----------------------------------------------------------------------------*/
/*                                                                            */
/*                                                                            */
/*----------------------------------------------------------------------------*/
void new_timer(int a)
{
 int d;
 int b;
 int c = 1;
 
  d= 1;
  b = d+23;
  c= (d*b)/c;
//    retvideo=128;
//    buf[0xa7c3]|=128;
//    do_irq=1;
}

/*----------------------------------------------------------------------------*/
/*                                                                            */
/*                                                                            */
/*----------------------------------------------------------------------------*/
void startintr()
{
 sigset_t set;
 struct sigaction a;
 sigemptyset(&set);

 a.sa_handler = new_timer;
 a.sa_mask = set;
 a.sa_flags = 0;
 sigaction (SIGALRM, &a, NULL);

 fifty.it_interval.tv_sec=0;
 fifty.it_interval.tv_usec=20000;
 fifty.it_value.tv_sec=0;
 fifty.it_value.tv_usec=20000;
 
/*  signal(SIGALRM,new_timer); */
/*    setitimer(ITIMER_REAL,&fifty,NULL);*/
 setitimer(ITIMER_REAL,&fifty,&Old_Timer);
}

/*----------------------------------------------------------------------------*/
/*                                                                            */
/*                                                                            */
/*----------------------------------------------------------------------------*/
void stopintr()
{
 fifty.it_interval.tv_sec=0;
 fifty.it_interval.tv_usec=0;
 fifty.it_value.tv_sec=0;
 fifty.it_value.tv_usec=0;

 signal(SIGALRM,NULL);
/*  setitimer(ITIMER_REAL,&fifty,NULL);*/
 setitimer(ITIMER_REAL,&Old_Timer,NULL);
}

/*---------------------------------------------------------------------------*/
/*                                                                           */
/*                                                                           */
/*---------------------------------------------------------------------------*/
void AffCar(byte x, byte y, byte Car)
{
 int P_x,P_y;
 byte Mask;
 
 /* On efface le caractere precedent */
 /*----------------------------------*/
 
 /* Dessin du caractere point par point */
 /*-------------------------------------*/
 for (P_y=0;P_y<8;P_y++)
  {
   Mask=0x80;
   for (P_x=0;P_x<6;P_x++)
    {
     if (Car_Def[Car][P_y] & Mask)
      {
       /* Positionnement de la mémoire video */
       /*------------------------------------*/
       Ram_Video[(x*NB_POINT_CAR_X)+P_x][(y*NB_POINT_CAR_Y)+P_y]=1;
       
       /* Affichage d'un point noir si necessaire */
       /*-----------------------------------------*/
      }
     else
      {
       Ram_Video[(x*NB_POINT_CAR_X)+P_x][(y*NB_POINT_CAR_Y)+P_y]=0;
      }
     Mask >>=1;
    }
  }
  Refresh_Video();
}

/*---------------------------------------------------------------------------*/
/*                                                                           */
/*                                                                           */
/*---------------------------------------------------------------------------*/
void LineClear (byte P_y)
{
 byte x,y;
 
 /* Effacement de la mémoire video */
 /*--------------------------------*/
 for (x=0;x<MAX_X;x++)
  for (y=P_y*NB_POINT_CAR_Y;y<(P_y+1)*NB_POINT_CAR_Y;y++)
   Ram_Video[x][y]=0;
 
 /* Effacement dans la fenetre video */
 /*----------------------------------*/  
 Refresh_Video();
}

/*---------------------------------------------------------------------------*/
/*                                                                           */
/*                                                                           */
/*---------------------------------------------------------------------------*/
void Scroll_Video(void)
{
    int x, y;
    int y_min = General_Info.Scroll_Min_Y * NB_POINT_CAR_Y;
    int y_max = General_Info.Scroll_Max_Y * NB_POINT_CAR_Y;

    if (y_max <= y_min || y_max > MAX_Y) {
        y_min = 0;
        y_max = MAX_Y;
    }

    for (y = y_min; y < y_max - NB_POINT_CAR_Y; y++) {
        for (x = 0; x < MAX_X; x++) {
            Ram_Video[x][y] = Ram_Video[x][y + NB_POINT_CAR_Y];
        }
    }

    for (y = y_max - NB_POINT_CAR_Y; y < y_max; y++) {
        for (x = 0; x < MAX_X; x++) {
            Ram_Video[x][y] = 0;
        }
    }

    Refresh_Video();
    
    /*fprintf(stderr,
    "SCROLL min=%d max=%d curs=%d,%d size=%d,%d\n",
    General_Info.Scroll_Min_Y,
    General_Info.Scroll_Max_Y,
    General_Info.Curs_X,
    General_Info.Curs_Y,
    General_Info.size_point_x,
    General_Info.size_point_y);
    */
}

/*---------------------------------------------------------------------------*/
/*                                                                           */
/*                                                                           */
/*---------------------------------------------------------------------------*/
void ClrScr (void)
{
 byte x,y;
 
 for (x=0 ; x<MAX_X ; x++)
  for (y=0 ; y<MAX_Y ; y++)
   Ram_Video [x][y] = 0;
 Refresh_Video();
}

/*---------------------------------------------------------------------------*/
/*                                                                           */
/*                                                                           */
/*---------------------------------------------------------------------------*/
void Line(byte x1, byte y1, byte x2, byte y2)
{
	int dx, dy;
	int sx, sy;
	int err, e2;
	int x, y;

	x = x1;
	y = y1;

	dx = abs((int)x2 - (int)x1);
	dy = abs((int)y2 - (int)y1);

	sx = (x1 < x2) ? 1 : -1;
	sy = (y1 < y2) ? 1 : -1;

	err = dx - dy;

	while (1)
	{
		if (x >= 0 && x < MAX_X && y >= 0 && y < MAX_Y)
			Ram_Video[x][y] = 1;

		if (x == x2 && y == y2)
			break;

		e2 = err * 2;

		if (e2 > -dy)
		{
			err -= dy;
			x += sx;
		}

		if (e2 < dx)
		{
			err += dx;
			y += sy;
		}
	}

	Refresh_Video();
}

/*----------------------------------------------------------------------------*/
/*                                                                            */
/*                                                                            */
/*----------------------------------------------------------------------------*/
static void Circle_Plot(byte cx, byte cy, int x, int y)
{
	int px, py;

	px = (int)cx + x; py = (int)cy + y;
	if (px >= 0 && px < MAX_X && py >= 0 && py < MAX_Y)
		Ram_Video[px][py] = 1;

	px = (int)cx - x; py = (int)cy + y;
	if (px >= 0 && px < MAX_X && py >= 0 && py < MAX_Y)
		Ram_Video[px][py] = 1;

	px = (int)cx + x; py = (int)cy - y;
	if (px >= 0 && px < MAX_X && py >= 0 && py < MAX_Y)
		Ram_Video[px][py] = 1;

	px = (int)cx - x; py = (int)cy - y;
	if (px >= 0 && px < MAX_X && py >= 0 && py < MAX_Y)
		Ram_Video[px][py] = 1;

	px = (int)cx + y; py = (int)cy + x;
	if (px >= 0 && px < MAX_X && py >= 0 && py < MAX_Y)
		Ram_Video[px][py] = 1;

	px = (int)cx - y; py = (int)cy + x;
	if (px >= 0 && px < MAX_X && py >= 0 && py < MAX_Y)
		Ram_Video[px][py] = 1;

	px = (int)cx + y; py = (int)cy - x;
	if (px >= 0 && px < MAX_X && py >= 0 && py < MAX_Y)
		Ram_Video[px][py] = 1;

	px = (int)cx - y; py = (int)cy - x;
	if (px >= 0 && px < MAX_X && py >= 0 && py < MAX_Y)
		Ram_Video[px][py] = 1;
}

void Circle(byte x1, byte y1, byte r)
{
	int x;
	int y;
	int d;

	x = 0;
	y = r;
	d = 3 - (2 * r);

	while (x <= y)
	{
		Circle_Plot(x1, y1, x, y);

		if (d < 0)
		{
			d = d + (4 * x) + 6;
		}
		else
		{
			d = d + (4 * (x - y)) + 10;
			y--;
		}

		x++;
	}

	Refresh_Video();
}

/*----------------------------------------------------------------------------*/
/*                                                                            */
/*                                                                            */
/*----------------------------------------------------------------------------*/
void Aff_Curseur(void)
{
    static byte visible = 0;
    static byte saved[NB_POINT_CAR_X][NB_POINT_CAR_Y];
    static byte saved_valid = 0;
    static byte old_px = 0;
    static byte old_py = 0;

    byte px, py;
    byte x, y;

    if (!General_Info.Curseur) {
        /*
         * Si le curseur était affiché, on restaure la zone.
         */
        if (visible && saved_valid) {
            for (x = 0; x < NB_POINT_CAR_X; x++) {
                for (y = 0; y < NB_POINT_CAR_Y; y++) {
                    if ((old_px + x) < MAX_X && (old_py + y) < MAX_Y) {
                        Ram_Video[old_px + x][old_py + y] = saved[x][y];
                    }
                }
            }
            visible = 0;
            Refresh_Video();
        }
        return;
    }

    px = General_Info.Curs_X * NB_POINT_CAR_X;
    py = General_Info.Curs_Y * NB_POINT_CAR_Y;

    if (px >= MAX_X || py >= MAX_Y)
        return;

    /*
     * Si le curseur était visible, on restaure d'abord l'ancien caractère.
     */
    if (visible && saved_valid) {
        for (x = 0; x < NB_POINT_CAR_X; x++) {
            for (y = 0; y < NB_POINT_CAR_Y; y++) {
                if ((old_px + x) < MAX_X && (old_py + y) < MAX_Y) {
                    Ram_Video[old_px + x][old_py + y] = saved[x][y];
                }
            }
        }

        visible = 0;
        Refresh_Video();
        return;
    }

    /*
     * Sauvegarde de la cellule caractère courante.
     */
    for (x = 0; x < NB_POINT_CAR_X; x++) {
        for (y = 0; y < NB_POINT_CAR_Y; y++) {
            if ((px + x) < MAX_X && (py + y) < MAX_Y) {
                saved[x][y] = Ram_Video[px + x][py + y];
            } else {
                saved[x][y] = 0;
            }
        }
    }

    old_px = px;
    old_py = py;
    saved_valid = 1;

    /*
     * Efface la cellule complète.
     */
    for (x = 0; x < NB_POINT_CAR_X; x++) {
        for (y = 0; y < NB_POINT_CAR_Y; y++) {
            if ((px + x) < MAX_X && (py + y) < MAX_Y) {
                Ram_Video[px + x][py + y] = 0;
            }
        }
    }

    /*
     * Dessine le curseur "_" sur la dernière ligne de la cellule.
     */
    for (x = 0; x < NB_POINT_CAR_X; x++) {
        if ((px + x) < MAX_X && (py + NB_POINT_CAR_Y - 1) < MAX_Y) {
            Ram_Video[px + x][py + NB_POINT_CAR_Y - 1] = 1;
        }
    }

    visible = 1;
    Refresh_Video();
}

/*----------------------------------------------------------------------------*/
/*                                                                            */
/*                                                                            */
/*----------------------------------------------------------------------------*/
void X07_LCD_Off(void)
{
	X07_Screen_Enable = 0;
	/*
	 * Ecran eteint : on n'efface pas Ram_Video.
	 * On efface seulement l'affichage SDL.
	 */
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);
	SDL_RenderPresent(renderer);
}

void X07_LCD_On(void)
{
	X07_Screen_Enable = 1;
	/*
	 * On rallume en redessinant depuis Ram_Video.
	 */
	Refresh_Video();
}

/*----------------------------------------------------------------------------*/
/* Injection automatique d'un programme BASIC depuis un fichier TXT             */
/*----------------------------------------------------------------------------*/
void BasicText_Open(char *filename)
{
	if (BasicText_File) {
		fclose(BasicText_File);
		BasicText_File = NULL;
	}

	BasicText_File = fopen(filename, "r");
	BasicText_LastCR = 0;
	BasicText_Count = 0;

	BasicText_StartMs = SDL_GetTicks() + 8000;
	BasicText_NextMs = BasicText_StartMs;

	if (!BasicText_File) {
		fprintf(stderr, "[BASIC TXT] Impossible d'ouvrir %s\n", filename);
		return;
	}
	fprintf(stderr, "[BASIC TXT] Injection clavier depuis %s dans 8 secondes\n", filename);
}
/*----------------------------------------------------------------------------*/
void BasicText_Pump(void)
{
	int c;
	Uint32 now;

	if (!BasicText_File)
		return;

	now = SDL_GetTicks();

	if (now < BasicText_StartMs)
		return;

	if (now < BasicText_NextMs)
		return;

	/*
	 * On attend que le buffer clavier émulateur soit vide.
	 * Add_Key() ne stocke que 20 caractères maximum.
	 */
	if (Clavier.Nb_Key != 0)
		return;

	c = fgetc(BasicText_File);

	if (c == EOF) {
		fclose(BasicText_File);
		BasicText_File = NULL;
		fprintf(stderr, "[BASIC TXT] Fin injection, %lu caracteres envoyes\n", BasicText_Count);
		// TestChr_ClearAll
		// memset(TestChr_KeyDown, 0, sizeof(TestChr_KeyDown));
		return;
	}

	if (c == '\r') {
		Add_Key('\r');
		BasicText_LastCR = 1;
		BasicText_Count++;
		/* Après ENTER, le BASIC doit tokeniser et insérer la ligne. */
		BasicText_NextMs = now + 250;
		return;
	}
	if (c == '\n') {
		if (!BasicText_LastCR) {
			Add_Key('\r');
			BasicText_Count++;

			/* Pause plus longue après chaque ligne BASIC. */
			BasicText_NextMs = now + 250;
		}
		BasicText_LastCR = 0;
		return;
	}

	BasicText_LastCR = 0;

	if (c >= 0x20 && c <= 0x7E) {
		Add_Key(X07_TextFile_ApplyCase((byte)c));
		BasicText_Count++;

		/* Pause entre deux caractères. */
		BasicText_NextMs = now + 25;
		return;
	}

	/*
	 * Caractère ignoré : tabulation, UTF-8, contrôle, etc.
	 * On ne bloque pas l'injection.
	 */
	BasicText_NextMs = now + 1;
}
