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
/* 18/12/00 | J.BRIGAUD |  0001  | Creation                                 */
/* 10/05/26 | L.VALTER  |  0003  | Suite                                    */
/*----------+-----------+--------+------------------------------------------*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <ctype.h>
#include <errno.h>
#include "Z80.h"

/*
           Mémoire du XO7
  0000    ----------------
         |  XXXX  |  RAM  |
  2000    ----------------
         | Carte RAM 8Ko  |
  4000    ----------------
         | Prise ROM 8Ko  |
  6000    ----------------
         | ROM Carte 8Ko  |
  8000    ----------------
         | V-RAM 6Ko      |
  9800    ----------------
         | ~~~~~~~~~~~~~~ |
  A000    ----------------
         | ROM TV 4Ko     |
  B000    ----------------
         | ROM 20Ko BASIC |
  FFFF    ----------------

*/

#define VAR_GLOB
#include "struct.h"
#include "const.h"
#include "var.h"
#include "proto.h"
#include "rom_xo7.h"
#include "rom_x720c.h"

/* Ces fonctions sont definies dans video.c. */
void BasicText_Open(char *filename);
void BasicText_Pump(void);

#define USE_ROM_TV 1
#define PATCH_MISSING_ROM_TV 0

#define X720_VRAM_START 0x8000
#define X720_VRAM_END   0x97FF
#define X720_VRAM_SIZE  0x1800

#define X07_CPU_HZ 1574800UL
#define SYNC_HZ    200
#define SLICE_NS   (1000000000ULL / SYNC_HZ)

#define X07_RESET_ENTRY     0xC3C3

static byte Power_Off    = 0;
static byte Mode_K7      = 0;
static byte Presence_k7  = 0; /* Une k7 est presente */
static FILE *Fichier_k7  = 0; /* Fichier de k7 */
static byte K7_EOF       = 0;
static byte K7_Read_Enabled = 0;
static byte K7_Allow_Start   = 0;
static uint64_t next_sync_ns = 0;

/* X-720 : VRAM physique 6 Ko et dernier registre de contr�le vid�o. */
byte X720_VRAM[X720_VRAM_SIZE];
static byte X720_CTRL = 0x00;
static int Trace_X720 = 1;

/* Traces X-720 filtrées. Mettre à 1 ponctuellement pour réactiver une catégorie. */
#define TRACE_X720_OUT        1
#define TRACE_X720_IN         0
#define TRACE_X720_VRAM_W     1
#define TRACE_X720_VRAM_R     0
#define TRACE_X720_RAM        1
#define TRACE_X720_DUMP_ON_GRAPH_WRITE 0
#define TRACE_X720_DUMP_MAX            16


/* Injection retardee de la commande CLOAD.
 * Il ne faut pas l'envoyer avant que la ROM ait fini son demarrage,
 * sinon la commande n'apparait pas et la K7 peut demarrer trop tot.
 */
static byte Auto_CLOAD_Pending = 0;
static unsigned long Auto_CLOAD_StartMs = 0;
static int Auto_CLOAD_Pos = 0;

//static char K7_Name[80];

static unsigned long now_ms(void);
static void Auto_CLOAD_Pump(void);
static void CSave_Write_Byte(byte b);
static void CSave_Pump(void);
static void CSave_Close(void);
static void cload_make_path(char *dst, size_t dst_sz, const char *name);
static void cload_try_open_from_line(const char *line);

static int has_cas_ext(const char *name)
{
    const char *dot;

    if (!name)
        return 0;

    dot = strrchr(name, '.');
    if (!dot)
        return 0;

    return (!strcasecmp(dot, ".CAS") || !strcasecmp(dot, ".K7"));
}

/*----------------------------------------------------------------------*/
/* Chargement cassette manuel : CLOAD "NOM" -> ouverture de NOM.cas     */
/*----------------------------------------------------------------------*/
#define CLOAD_LINE_MAX 128
#define CLOAD_NAME_MAX 64

static void cload_make_path(char *dst, size_t dst_sz, const char *name)
{
	char clean[CLOAD_NAME_MAX + 1];
	size_t i, j = 0;

	for (i = 0; name[i] && j < CLOAD_NAME_MAX; i++) {
		unsigned char c = (unsigned char)name[i];

		if (isalnum(c) || c == '_' || c == '-')
			clean[j++] = (char)tolower(c);
	}
	clean[j] = 0;

	if (clean[0] == 0)
		return;

	/* Comme CSAVE cree toto.cas, CLOAD "toto" doit ouvrir toto.cas. */
	if (has_cas_ext(clean))
		snprintf(dst, dst_sz, "%s", clean);
	else
		snprintf(dst, dst_sz, "%s.cas", clean);
}

static void cload_try_open_from_line(const char *line)
{
	const char *p = line;
	char name[CLOAD_NAME_MAX + 1];
	char path[256];
	size_t n = 0;

	while (*p && isspace((unsigned char)*p))
		p++;

	/* On intercepte uniquement CLOAD. CLOAD? reste le comportement ROM. */
	if (strncasecmp(p, "CLOAD", 5) != 0)
		return;
	p += 5;

	while (*p && isspace((unsigned char)*p))
		p++;

	if (*p != '"')
		return;
	p++;

	while (*p && *p != '"' && n < CLOAD_NAME_MAX)
		name[n++] = *p++;
	name[n] = 0;

	if (name[0] == 0)
		return;

	path[0] = 0;
	cload_make_path(path, sizeof(path), name);
	if (path[0] == 0)
		return;

	if (open_k7(path) == RC_OK) {
		/* La ROM declenchera ensuite la lecture par OUT F5 bit 0x04. */
		K7_Allow_Start = 1;
		fprintf(stderr, "[CLOAD] %s -> %s pret\n", name, path);
	} else {
		K7_Allow_Start = 0;
		fprintf(stderr, "[CLOAD] impossible d'ouvrir %s pour %s\n", path, name);
	}
}

void X07_Key_Monitor(byte Key)
{
	static char line[CLOAD_LINE_MAX];
	static int len = 0;

	if (Key == '\r' || Key == '\n') {
		line[len] = 0;
		cload_try_open_from_line(line);
		len = 0;
		line[0] = 0;
		return;
	}

	/* Corrections simples au clavier. */
	if (Key == 0x08 || Key == 0x7F) {
		if (len > 0)
			len--;
		line[len] = 0;
		return;
	}

	/* CTRL+E sur X-07 : effacement de la fin de ligne ; ici on repart propre. */
	if (Key == 0x05 || Key == 0x0C) {
		len = 0;
		line[0] = 0;
		return;
	}

	if (Key >= 0x20 && Key <= 0x7E) {
		if (len < CLOAD_LINE_MAX - 1) {
			line[len++] = (char)Key;
			line[len] = 0;
		}
	}
}

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * 1000000000ULL) + ts.tv_nsec;
}

static void sync_emulation(void)
{
    uint64_t n = now_ns();

    if (next_sync_ns == 0) {
        next_sync_ns = n + SLICE_NS;
        return;
    }

    if (n < next_sync_ns) {
        uint64_t delay_ns = next_sync_ns - n;

        struct timespec req;
        req.tv_sec  = delay_ns / 1000000000ULL;
        req.tv_nsec = delay_ns % 1000000000ULL;
        nanosleep(&req, NULL);
    }

    next_sync_ns += SLICE_NS;

    /*
     * Protection si l'émulateur est en retard :
     * on évite d'accumuler plusieurs secondes de retard.
     */
    n = now_ns();
    if (next_sync_ns < n - SLICE_NS) {
        next_sync_ns = n + SLICE_NS;
    }
}

int main (int argc, char **argv) {
	/* Initialisation des variables globales */
	/*---------------------------------------*/
	Tmp = 0;
	IT_T6834 = 0;
	int snapshot_ok = 0;
	char *Auto_Name = NULL;
	int Auto_CLOAD = 0;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-cload") && (i + 1 < argc)) {
			Auto_Name = argv[++i];
			Auto_CLOAD = 1;
		} else {
			Auto_Name = argv[i];
			Auto_CLOAD = has_cas_ext(Auto_Name);
		}
	}
	/* Allocation de la memoire du XO7 */
	/*---------------------------------*/
	if (!(RAM= (byte *) malloc (Taille_Ram))) {
	   fprintf (stderr,"Allocation Memoire impossible!/n");
	   exit (1);
	}
	/* Effacementt de la memoire */
	/*---------------------------*/
	memset((void*)RAM, 0xFF, Taille_Ram);
	memset((void*)&Port_FX, 0, sizeof(Port_FX));
	memset((void*)&Clavier, 0, sizeof(Clavier));
	memset((void*)&General_Info, 0, sizeof(General_Info));
	
	snapshot_ok = (Load_Ram_Temp() == RC_OK);
	
	/* Mise en place des touches speciales */
	/*-------------------------------------*/
	signal (SIGINT, Active_Debugger); /* Ctrl C */
	signal (SIGTSTP, Active_Int);     /* Ctrl Z */

	if (!snapshot_ok) {
		/*
		 * Démarrage froid / reset.
		 *
		 * Aucun fichier RAM n'a été restauré.
		 * La ROM doit voir un démarrage normal.
		 */
		X07_OnStat = 0x00;

		ResetZ80(&Reg_Xo7);

		Reg_Xo7.IPeriod = X07_CPU_HZ / SYNC_HZ;
		Reg_Xo7.Trace   = 1;

		Int_nsc800_BB = 0;

		/*
		 * Entrée normale ROM.
		 */
		Reg_Xo7.PC.W = X07_RESET_ENTRY;

		Init_Etat_Xo7();

		fprintf(stderr,
				"Démarrage RESET PC=%04X OnStat=%02X\n",
				Reg_Xo7.PC.W,
				X07_OnStat);
	} else {
		/*
		 * Reprise après OFF.
		 *
		 * La RAM a été restaurée depuis /tmp/x07_ram.bin.
		 * On ne reprend PAS l'ancien PC.
		 * On redémarre par la ROM normale, mais avec OnStat=0x01.
		 *
		 * La ROM doit alors conserver le programme BASIC,
		 * mais réinitialiser les variables.
		 */
		X07_OnStat = 0x01;

		ResetZ80(&Reg_Xo7);

		Reg_Xo7.IPeriod = X07_CPU_HZ / SYNC_HZ;
		Reg_Xo7.Trace   = 0;

		Int_nsc800_BB = 0;

		/*
		 * Important :
		 * On repasse par C3C3, pas C3D7.
		 * C'est OnStat=0x01 qui indique à la ROM
		 * que c'est un ON après OFF.
		 */
		Reg_Xo7.PC.W = X07_RESET_ENTRY;

		Init_Etat_Xo7();

		fprintf(stderr,
				"Reprise POWER ON via ROM PC=%04X OnStat=%02X\n",
				Reg_Xo7.PC.W,
				X07_OnStat);
	}
	/* Lancement de la fenetre X11 */
	/*-----------------------------*/
	iniscreen ();
	X720_Video_Init();

	if (Auto_Name) {
		if (Auto_CLOAD) {
			if (open_k7(Auto_Name) == RC_OK) {
				fprintf(stderr, "[K7] Auto CLOAD depuis %s\n", Auto_Name);
				/*
				 * Ne pas injecter CLOAD ici : la ROM n'est pas encore prete.
				 * On attend dans LoopZ80(), puis on tape CLOAD comme un clavier.
				 */
				Auto_CLOAD_Pending = 1;
				Auto_CLOAD_StartMs = now_ms();
				Auto_CLOAD_Pos = 0;
				K7_Allow_Start = 0;
			} else {
				fprintf(stderr, "[K7] Impossible d'ouvrir %s\n", Auto_Name);
			}
		} else {
			fprintf(stderr, "[BASIC TXT] Injection clavier depuis %s\n", Auto_Name);
			BasicText_Open(Auto_Name);
		}
	}
	
	fprintf(stderr, "AVANT RunZ80 PC=%04X OP=%02X Power_Off=%d\n",
	        Reg_Xo7.PC.W,
	        RdZ80(Reg_Xo7.PC.W),
	        Power_Off);
	fflush(stderr);
	
	/*------------------------*/
	RunZ80(&Reg_Xo7);
	/* Liberation de la memoire */
	/*--------------------------*/
	X720_Video_Close();
	free (RAM);
	/* Fin du programme */
	/*------------------*/
	return (0);
}

void Init_Etat_Xo7 (void)
{
	memset((void *)General_Info.F_Key, 0, sizeof(General_Info.F_Key));
	strncpy((char *)General_Info.F_Key[0],  "tim?TIME$\r", 80);  /* F1  */
	strncpy((char *)General_Info.F_Key[1],  "cldCLOAD\"", 80);   /* F2  */
	strncpy((char *)General_Info.F_Key[2],  "locLOCATE", 80);    /* F3  */
	strncpy((char *)General_Info.F_Key[3],  "lstLIST", 80);      /* F4  */
	strncpy((char *)General_Info.F_Key[4],  "runRUN\r", 80);     /* F5  */
	strncpy((char *)General_Info.F_Key[5],  "nul", 80);          /* F6  */

	strncpy((char *)General_Info.F_Key[6],  "dat?DATE$\r", 80);  /* F7  */
	strncpy((char *)General_Info.F_Key[7],  "csaCSAVE", 80);     /* F8  */
	strncpy((char *)General_Info.F_Key[8],  "prtPRINT", 80);     /* F9  */
	strncpy((char *)General_Info.F_Key[9],  "slpSLEEP", 80);     /* F10 */
	strncpy((char *)General_Info.F_Key[10], "cntCONT\r", 80);    /* F11 */
	strncpy((char *)General_Info.F_Key[11], "nul", 80);          /* F12 */
	
	General_Info.Curs_X       = 0;
	General_Info.Curs_Y       = 0;
	General_Info.Curseur      = 0;
	General_Info.Aff_Udk      = 0;
	General_Info.Rem_Canal    = 0;
	General_Info.Stick        = 0x30;
	General_Info.Strig        = 0xFF;
	General_Info.Strig1       = 0xFF;
	General_Info.Break        = 0;
	General_Info.size_point_x = 1;
	General_Info.size_point_y = 1;
	
	X07_Keyb_Enable  = 1;  /* clavier actif */
	X07_Click_Enable = 1;  /* clic touche actif */
	X07_Repeat_Enable = 1; /* répétition active */
	X07_UDK_Enable = 1;
	X07_Screen_Enable = 1;
	X07_Repeat_Enable = 1;
	
	Init_T6834();
}

void Reset_Xo7(void)
{
	fprintf(stderr, "RESET X07\n");

	memset((void*)RAM, 0xFF, Taille_Ram);
	memset((void*)&Port_FX, 0, sizeof(Port_FX));
	memset((void*)&Clavier, 0, sizeof(Clavier));

	if (Load_Ram_Temp() == RC_OK) {
		fprintf(stderr, "Reprise automatique de la RAM sauvegardée\n");
	} else {
		fprintf(stderr, "Pas de RAM sauvegardée, démarrage normal\n");
	}

	Tmp = 0;
	IT_T6834 = 0;
	Int_nsc800_BB = 0;
	Mode_K7 = 0;
	Power_Off = 0;

	ResetZ80(&Reg_Xo7);
	Reg_Xo7.IPeriod = X07_CPU_HZ / SYNC_HZ;
	Reg_Xo7.Trace   = 0;
	Reg_Xo7.PC.W    = 0xC3C3;

	Init_Etat_Xo7();
	ClrScr();
}

void Power_Off_Xo7(void)
{
	if (Power_Off)
		return;

	fprintf(stderr, "POWER OFF X07\n");

	Power_Off = 1;

	Save_Ram_Temp();
	
	General_Info.Curseur = 0;
	ClrScr();
}

void Power_OnBreak_Xo7(void)
{
	if (Power_Off) {
		fprintf(stderr, "POWER ON X07\n");
		Power_Off = 0;
		X07_OnStat = 0x01;
		memset((void*)&Port_FX, 0, sizeof(Port_FX));
		memset((void*)&Clavier, 0, sizeof(Clavier));
		memset((void*)&General_Info, 0, sizeof(General_Info));

		Tmp = 0;
		IT_T6834 = 0;
		Int_nsc800_BB = 0;

		ResetZ80(&Reg_Xo7);

		Reg_Xo7.IPeriod = X07_CPU_HZ / SYNC_HZ;
		Reg_Xo7.Trace   = 0;
		Reg_Xo7.PC.W    = X07_RESET_ENTRY;   /* 0xC3C3 */

		Init_Etat_Xo7();

		fprintf(stderr,
		        "POWER ON via ROM PC=%04X OnStat=%02X\n",
		        Reg_Xo7.PC.W,
		        X07_OnStat);

		return;
	}

	fprintf(stderr, "ON/BREAK\n");
	General_Info.Break = 1;
}

/*----------------------------------------------------------------------*/

/*----------------------------------------------------------------------*/
/* Sauvegarde cassette : capture du flux genere par la ROM pour CSAVE    */
/*----------------------------------------------------------------------*/
#define CSAVE_DETECT_MAX 2048
#define CSAVE_NAME_MAX      6

static FILE *CSave_File = NULL;
static byte  CSave_Detect[CSAVE_DETECT_MAX];
static int   CSave_Detect_Len = 0;
static int   CSave_Active = 0;
static int   CSave_Error = 0;
static char  CSave_Path[128];
static unsigned long CSave_LastMs = 0;

static int csave_is_name_char(int c)
{
	return isalnum((unsigned char)c) || c == '_' || c == '-';
}

static void csave_make_path(char *dst, size_t dst_sz, const char *name)
{
	char clean[CSAVE_NAME_MAX + 1];
	size_t i, j = 0;

	for (i = 0; name[i] && j < CSAVE_NAME_MAX; i++) {
		unsigned char c = (unsigned char)name[i];

		if (isalnum(c) || c == '_' || c == '-')
			clean[j++] = (char)tolower(c);
	}
	clean[j] = 0;

	if (clean[0] == 0)
		strncpy(clean, "x07", sizeof(clean));

	snprintf(dst, dst_sz, "%s.cas", clean);
}

static int csave_try_extract_name(char *name, size_t name_sz)
{
	int i;
	int best_len = 0;
	char best[CSAVE_NAME_MAX + 1];

	best[0] = 0;

	if (CSave_Detect_Len < 16)
		return 0;

	for (i = 0; i < CSave_Detect_Len; i++) {
		int j = i;
		int n = 0;
		char tmp[CSAVE_NAME_MAX + 1];

		if (!csave_is_name_char(CSave_Detect[j]))
			continue;

		while (j < CSave_Detect_Len && n < CSAVE_NAME_MAX &&
		       csave_is_name_char(CSave_Detect[j])) {
			tmp[n++] = (char)CSave_Detect[j++];
		}
		tmp[n] = 0;

		/* Evite de prendre une portion au milieu d'un mot plus long. */
		if (j < CSave_Detect_Len && csave_is_name_char(CSave_Detect[j]))
			continue;

		if (n > best_len) {
			best_len = n;
			strncpy(best, tmp, sizeof(best) - 1);
			best[sizeof(best) - 1] = 0;
		}
	}

	if (best_len >= 2) {
		strncpy(name, best, name_sz - 1);
		name[name_sz - 1] = 0;
		return 1;
	}

	return 0;
}

static int CSave_Open_From_Detect(void)
{
	char name[CSAVE_NAME_MAX + 1];
	int fd;
	int i;

	if (CSave_File || CSave_Error)
		return CSave_File != NULL;

	if (!csave_try_extract_name(name, sizeof(name)))
		return 0;

	csave_make_path(CSave_Path, sizeof(CSave_Path), name);

	/* Erreur si le fichier existe deja : pas d'ecrasement silencieux. */
	fd = open(CSave_Path, O_WRONLY | O_CREAT | O_EXCL, 0644);
	if (fd < 0) {
		fprintf(stderr, "\n[CSAVE] impossible de creer %s : %s\n",
		        CSave_Path, strerror(errno));
		CSave_Error = 1;
		return 0;
	}

	CSave_File = fdopen(fd, "wb");
	if (!CSave_File) {
		fprintf(stderr, "\n[CSAVE] fdopen %s : %s\n",
		        CSave_Path, strerror(errno));
		close(fd);
		unlink(CSave_Path);
		CSave_Error = 1;
		return 0;
	}

	fprintf(stderr, "\n[CSAVE] creation %s\n", CSave_Path);

	for (i = 0; i < CSave_Detect_Len; i++)
		fputc(CSave_Detect[i], CSave_File);

	CSave_Active = 1;
	CSave_LastMs = now_ms();
	return 1;
}

static void CSave_Write_Byte(byte b)
{
	if (CSave_Error)
		return;

	CSave_LastMs = now_ms();

	if (!CSave_File) {
		if (CSave_Detect_Len < CSAVE_DETECT_MAX)
			CSave_Detect[CSave_Detect_Len++] = b;
		else {
			fprintf(stderr, "\n[CSAVE] nom de fichier non trouve dans l'entete\n");
			CSave_Error = 1;
			return;
		}

		if (!CSave_Open_From_Detect())
			return;
		return;
	}

	fputc(b, CSave_File);
}

static void CSave_Pump(void)
{
	if ((CSave_File || CSave_Detect_Len > 0 || CSave_Error) &&
	    (now_ms() - CSave_LastMs) > 1000)
		CSave_Close();
}

static void CSave_Close(void)
{
	if (CSave_File) {
		fflush(CSave_File);
		fclose(CSave_File);
		fprintf(stderr, "\n[CSAVE] fichier ferme : %s\n", CSave_Path);
	}

	CSave_File = NULL;
	CSave_Detect_Len = 0;
	CSave_Active = 0;
	CSave_Error = 0;
	CSave_Path[0] = 0;
	CSave_LastMs = 0;
}

/* Gestion des K7 */
/*----------------------------------------------------------------------*/
void Send_to_K7 (PorT_FX *Port)
{
	/*
	 * La ROM envoie un octet cassette dans F7, puis valide
	 * l'émission par OUT F5 avec le bit 0x08.
	 * On capture donc ici le flux brut généré par la ROM pour CSAVE.
	 */
	CSave_Write_Byte(Port->W.F7);
}

void init_k7 (void)
{
	Presence_k7 = 0;
	Fichier_k7 = NULL;
	Mode_K7 = 0;
	K7_EOF = 0;
	K7_Read_Enabled = 0;
	fprintf(stderr, "[K7] init\n");
}

int open_k7(char *K7_Name)
{
	close_k7();

	Fichier_k7 = fopen(K7_Name, "rb");
	if (!Fichier_k7) {
		perror("[K7] fopen");
		Presence_k7 = 0;
		return RC_KO;
	}

	Presence_k7 = 1;
	K7_EOF = 0;
	K7_Read_Enabled = 0;
	Mode_K7 = 0;
	Port_FX.R.F6 &= ~0x02;
	Port_FX.R.F7 = 0x00;

	fprintf(stderr, "[K7] CAS ouvert : %s\n", K7_Name);
	fflush(stderr);
	return RC_OK;
}

void close_k7(void)
{
	if (Fichier_k7) {
		fclose(Fichier_k7);
		Fichier_k7 = NULL;
	}

	Presence_k7 = 0;
	Mode_K7 = 0;
	K7_Read_Enabled = 0;
	K7_Allow_Start = 0;
	K7_EOF = 1;

	/* Plus aucun octet K7 disponible */
	Port_FX.R.F6 &= ~0x02;
	Port_FX.R.F7 = 0x00;

	/* Plus d'interruption K7 en attente */
	IT_T6834 = 0;
}

void display_info_k7(void)
{
	long pos, size;

	if (!Presence_k7 || !Fichier_k7) {
		fprintf(stderr, "[K7] aucune cassette\n");
		return;
	}

	pos = ftell(Fichier_k7);
	fseek(Fichier_k7, 0, SEEK_END);
	size = ftell(Fichier_k7);
	fseek(Fichier_k7, pos, SEEK_SET);

	fprintf(stderr, "[K7] position %ld / %ld octets\n", pos, size);
}

void Receive_from_K7(PorT_FX *Port)
{
	int c;

	static const char anim[] = "/-\\|";
	static int anim_pos = 0;

	if (!Presence_k7 || !Fichier_k7 || K7_EOF)
		return;

	/* Ne pas lire la cassette avant que la ROM ait demandé CLOAD
	 * via OUT F5 avec le bit 0x04.
	 */
	if (!K7_Read_Enabled)
		return;

	/* Un octet est deja pret : attendre que la ROM lise F7 */
	if (Port->R.F6 & 0x02)
		return;

	c = fgetc(Fichier_k7);

	if (c == EOF) {
		fprintf(stderr, "[K7] fin du fichier CAS pos=%ld\n",
		        Fichier_k7 ? ftell(Fichier_k7) : -1L);
		fflush(stderr);

		close_k7();
		return;
	}

	/* Animation a chaque octet lu */
	fprintf(stderr, "\r[K7] lecture %c", anim[anim_pos]);
	//fflush(stderr);
	anim_pos++;
	if (anim_pos >= 4)
		anim_pos = 0;

	Port->R.F7 = (byte)c;

	/* Bit : octet disponible */
	Port->R.F6 |= 0x02;
	IT_T6834 = 3;
}

void Print (byte Cmd, PorT_FX *Port) {
	static byte Cpt_Bit=0;
	static byte Data=0;
	switch (Cmd) {
		case PRT_DATA:
			if (Port->R.F4 & MASK_PRT_DATA) 
				Data |= 1;
			Data <<= 1;
			Cpt_Bit ++;
			if (Cpt_Bit == 8) {
				fprintf (stderr,"Prt_data=0x%02X\n",Data);
				Cpt_Bit = 0;
				Data = 0;
			}
			break;
		case PRT_ACK: 
			break;
	}
}

/*----------------------------------------------------------------------*/
/*                 Affichage des registres du Z80                       */
/*----------------------------------------------------------------------*/
void DisplayReg(register Z80 *R)
{
 int value;
 char Flag[9];
 strcpy (Flag,"        ");
 if (R->AF.B.l & S_FLAG) Flag[0]='S';
 if (R->AF.B.l & Z_FLAG) Flag[1]='Z';
 if (R->AF.B.l & H_FLAG) Flag[3]='H';
 if (R->AF.B.l & V_FLAG) Flag[5]='V';
 if (R->AF.B.l & N_FLAG) Flag[6]='N';
 if (R->AF.B.l & C_FLAG) Flag[7]='C';
 fprintf (stderr,"AF :%04X BC :%04X DE :%04X HL :%04X\n",
           R->AF.W, R->BC.W, R->DE.W, R->HL.W);
 fprintf (stderr,"AF':%04X BC':%04X DE':%04X HL':%04X\n",
           R->AF1.W, R->BC1.W, R->DE1.W, R->HL1.W);
 fprintf (stderr,"PC :%04X SP :%04X IX :%04X IY :%04X\n",
           R->PC.W,R->SP.W,R->IX.W,R->IY.W);
 fprintf (stderr,"I  :%04X IFF:%04X %s\n",
           R->I,R->IFF,Flag);
 if ((R->PC.W >= 0xB000) && (R->PC.W <= 0xFFFF)) {
    value = ROM[R->PC.W - 0xB000];
    fprintf(stderr, "ROM[%04X] = %04X\n\n", R->PC.W, value);
 } else {
    value = RAM[R->PC.W];
    fprintf(stderr, "RAM[%04X] = %04X\n\n", R->PC.W, value);
 }
}

/*--------------------------------------------------------------------------*/
/* Ecriture des fonctions CPU Z80                                           */
/*--------------------------------------------------------------------------*/

static int X720_IsImportantRamAddr(word Addr)
{
    switch (Addr) {
        case 0x00B6: /* page active */
        case 0x00B7: /* page affichée */
        case 0x00D1: /* mode écran actif */
        case 0x04B7: /* registre/page interne */
        case 0x04BA: /* flag accélération / bascule */
        case 0x04C6: /* coordonnée X */
        case 0x04C8: /* coordonnée Y */
        case 0x04E5: /* couleur avant-plan */
        case 0x04E6: /* couleur arrière-plan */
        case 0x04E7: /* palette */
            return 1;
        default:
            return 0;
    }
}

static void Trace_X720_RamWrite(word Addr, byte Old, byte New)
{
    if (!Trace_X720 || !TRACE_X720_RAM)
        return;

    if (Old == New)
        return;

    if (!X720_IsImportantRamAddr(Addr))
        return;

    fprintf(stderr,
            "[X720 RAM] PC=%04X ADDR=%04X OLD=%02X NEW=%02X A=%02X BC=%04X DE=%04X HL=%04X\n",
            Reg_Xo7.PC.W,
            Addr,
            Old,
            New,
            Reg_Xo7.AF.B.h,
            Reg_Xo7.BC.W,
            Reg_Xo7.DE.W,
            Reg_Xo7.HL.W);
}

/*
 * Conversion adresse logique X-720 -> offset VRAM physique.
 * La documentation X-720 indique que le bit +1K du registre 90h change
 * le segment de 1 Ko adressé. C'est une première modélisation simple.
 */
static word X720_PhysAddr(word Addr)
{
    word off = Addr - X720_VRAM_START;

    if (X720_CTRL & 0x20)
        off += 0x0400;

    off %= X720_VRAM_SIZE;
    return off;
}

static int X720_IsPrintable(byte c)
{
    return (c >= 32 && c <= 126);
}

static int X720_ShouldTraceVramWrite(word Addr, word Phys, byte Old, byte New, word PC)
{
    (void)Addr;
    (void)Phys;

    /* Les écritures graphiques directes sont toujours intéressantes,
     * même si OLD == NEW : plusieurs pixels voisins peuvent tomber dans
     * le même octet VRAM. */
    if (PC == 0xA72E)
        return 1;

    /* Nettoyages / remplissages massifs peu utiles en trace courante. */
    if ((PC == 0xA832 || PC == 0xA824) && New == 0x00)
        return 0;

    /* Remplissage de zones avec des espaces pendant les changements d'écran. */
    if ((PC == 0xA264 || PC == 0xA1A9) && Old == 0x00 && New == 0x20)
        return 0;

    /* Remise à zéro d'attributs texte très bavarde. */
    if (PC == 0xA575 && Old == 0x20 && New == 0x00)
        return 0;

    return 1;
}

static void X720_Dump_Line(word Phys)
{
    word base;
    int i;

    if (Phys < X720_VRAM_START || Phys > X720_VRAM_END)
        return;

    base = Phys & 0xFFF0;

    fprintf(stderr, "[X720 DUMP] %04X:", base);
    for (i = 0; i < 16; i++) {
        word a = base + i;
        byte v = 0xFF;
        if (a >= X720_VRAM_START && a <= X720_VRAM_END)
            v = X720_VRAM[a - X720_VRAM_START];
        fprintf(stderr, " %02X", v);
    }

    fprintf(stderr, "  |");
    for (i = 0; i < 16; i++) {
        word a = base + i;
        byte v = '.';
        if (a >= X720_VRAM_START && a <= X720_VRAM_END) {
            byte b = X720_VRAM[a - X720_VRAM_START];
            v = X720_IsPrintable(b) ? b : '.';
        }
        fputc(v, stderr);
    }
    fprintf(stderr, "|\n");
}

static void X720_Dump_TextBase(word Base)
{
    int y, x;

    if (Base < X720_VRAM_START || Base + 0x80 > X720_VRAM_END + 1)
        return;

    fprintf(stderr, "[X720 TEXT %04X] base=%04X\n", Base, Base);

    for (y = 0; y < 4; y++) {
        fprintf(stderr, "  %02d: ", y);
        for (x = 0; x < 32; x++) {
            word a = Base + (y * 32) + x;
            byte c = X720_VRAM[a - X720_VRAM_START];
            fputc(X720_IsPrintable(c) ? c : '.', stderr);
        }
        fputc('\n', stderr);
    }
}

static void X720_Dump_OnGraphWrite(word Addr, word Phys, byte Old, byte New)
{
#if TRACE_X720_DUMP_ON_GRAPH_WRITE
    static unsigned int graph_count = 0;

    graph_count++;

    fprintf(stderr,
            "[X720 GRAPH] #%u PC=%04X LOG=%04X PHYS=%04X OLD=%02X NEW=%02X CTRL=%02X\n",
            graph_count,
            Reg_Xo7.PC.W,
            Addr,
            Phys,
            Old,
            New,
            X720_CTRL);

    if (graph_count <= TRACE_X720_DUMP_MAX) {
        X720_Dump_Line(Phys);
        X720_Dump_TextBase(0x8000);
        X720_Dump_TextBase(0x8400);
    }
#else
    (void)Addr;
    (void)Phys;
    (void)Old;
    (void)New;
#endif
}

/** RdZ80()/WrZ80() ******************************************/
/** These functions are called when access to RAM occurs.   **/
/** They allow to control memory access.                    **/
/************************************ TO BE WRITTEN BY USER **/
void WrZ80(register word Addr, register byte Value)
{
    byte old;

    /* ROM BASIC X-07 : �criture interdite. */
    if (Addr >= 0xB000)
        return;

    /* ROM X-720 : �criture interdite. */
    if ((Addr >= 0xA000) && (Addr <= 0xAFFF))
        return;

    /* VRAM X-720 : 8000h-97FFh. */
    if ((Addr >= X720_VRAM_START) && (Addr <= X720_VRAM_END)) {
        word off = X720_PhysAddr(Addr);

        old = X720_VRAM[off];
        X720_VRAM[off] = Value;

        /*
         * Toute modification de la VRAM X-720 doit rafraichir
         * la fenetre TV, meme si l'ecriture vient d'un POKE BASIC
         * et non d'une routine graphique ROM comme A72E.
         */
        if (old != Value) {
            X720_Video_MarkDirty();
        }

        {
            word phys = X720_VRAM_START + off;
            int is_graph_write = (Reg_Xo7.PC.W == 0xA72E);

            if (Trace_X720 && TRACE_X720_VRAM_W && (old != Value || is_graph_write)) {
                if (X720_ShouldTraceVramWrite(Addr, phys, old, Value, Reg_Xo7.PC.W)) {
                    fprintf(stderr,
                            "[X720 VRAM W] PC=%04X ADDR=%04X PHYS=%04X OLD=%02X NEW=%02X CTRL=%02X\n",
                            Reg_Xo7.PC.W,
                            Addr,
                            phys,
                            old,
                            Value,
                            X720_CTRL);
                }

                if (is_graph_write) {
                    X720_Dump_OnGraphWrite(Addr, phys, old, Value);
                }

            }
        }

        return;
    }

    /* RAM normale X-07 uniquement. */
    if ((Addr >= BEG_RAM) && (Addr < END_RAM)) {
        old = RAM[Addr];
        RAM[Addr] = Value;

        if (old != Value)
            Trace_X720_RamWrite(Addr, old, Value);

        return;
    }

    /* Zone non �quip�e : �criture ignor�e. */
}

byte RdZ80(register word Addr)
{
    static word last_trace_pc = 0xFFFF;

    if (Addr == Reg_Xo7.PC.W) {
        if ((Addr >= 0xC3AC && Addr <= 0xC3C0) ||
            (Addr >= 0xDE9F && Addr <= 0xDEA6)) {
            if (last_trace_pc != Addr) {
                last_trace_pc = Addr;
            }
        }
    }

    /* ROM BASIC X-07. */
    if ((Addr >= 0xB000) && (Addr <= 0xFFFF))
        return ROM[Addr - 0xB000];

    /* ROM X-720 / ROM TV. */
#if USE_ROM_TV
    if ((Addr >= 0xA000) && (Addr <= 0xAFFF))
        return ROMTV[Addr - 0xA000];
#else
    if ((Addr >= 0xA000) && (Addr <= 0xAFFF)) {
        fprintf(stderr,
                "ROM TV absente: lecture Addr=%04X depuis PC=%04X SP=%04X HL=%04X\n",
                Addr, Reg_Xo7.PC.W, Reg_Xo7.SP.W, Reg_Xo7.HL.W);
        return 0xFF;
    }
#endif

    /* VRAM X-720 : 8000h-97FFh. */
    if ((Addr >= X720_VRAM_START) && (Addr <= X720_VRAM_END)) {
        word off = X720_PhysAddr(Addr);
        byte v = X720_VRAM[off];

        if (Trace_X720 && TRACE_X720_VRAM_R) {
            fprintf(stderr,
                    "[X720 VRAM R] PC=%04X ADDR=%04X PHYS=%04X VAL=%02X CTRL=%02X\n",
                    Reg_Xo7.PC.W,
                    Addr,
                    X720_VRAM_START + off,
                    v,
                    X720_CTRL);
        }

        return v;
    }

    /* RAM normale X-07. */
    if ((Addr >= BEG_RAM) && (Addr < END_RAM))
        return RAM[Addr];

    /* Zone non �quip�e. */
    return 0xFF;
}

/** InZ80()/OutZ80() *****************************************/
/** Z80 emulation calls these functions to read/write from  **/
/** I/O ports. There can be 65536 I/O ports, but only first **/
/** 256 are usually used                                    **/
/************************************ TO BE WRITTEN BY USER **/
void OutZ80(register word Port,register byte Value) {
	Port &= 0xFF;
	switch (Port) {
		case 0xBB : /* Validation des interuptions du NSC800 */
			Int_nsc800_BB = Value;
			break;
		case 0xF0 : /* Controle des interruptions */
			Port_FX.W.F0 = Value;
			break;
		case 0xF1 : /* XBTR : Stockage des Informations pour CCU */
			Port_FX.W.F1 = Value;
			if (Port_FX.R.F0 == 0x00)
				Port_FX.R.F2 &= 0xFE;
			break;
		case 0xF2 : /* Controle de BAUDS (poids faible) */
			Port_FX.W.F2 = Value;
			break;
		case 0xF3 : /* Controle de BAUDS (poids fort) */ 
			Port_FX.W.F3 = Value;
			break;
		case 0xF4 : /* Modes */
			/*
			Port_FX.W.F4 = Value;
			Port_FX.R.F4 = Value;
			if (K7_Read_Enabled && (Port_FX.W.F5 & 0x04))
				Receive_from_K7(&Port_FX);
			*/
			Value = Port_FX.R.F4 & ~0x0E;
			break;
		case 0xF5 : /* Interruptions (RESET) */
			Port_FX.W.F5 = Value;
			/* Reception d'un octet venant du T6834 *
			*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
			if (Value & 0x01)
				Receive_from_T6834 (LEC_T6834_ACK,&Port_FX);
			/* Envoie d'un octet a destination du T6834 *
			*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
			if (Value & 0x02)
				Send_to_T6834 (&Port_FX);
			/* Reception d'un octet EN PROVENANCE du lecteur de K7 *
			*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
			if ((Value & 0x04) && K7_Allow_Start) {
				Mode_K7 = 1;
				K7_Read_Enabled = 1;
				Receive_from_K7 (&Port_FX);
			}
			/* Envoie d'un octet a destination du lecteur de K7 *
			*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
			if (Value & 0x08)
				Send_to_K7 (&Port_FX);
			/* Envoie d'un bit sur le port imprimante  *
			*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
			if (Value & 0x20)
				Print (PRT_DATA,&Port_FX);
			break;
		case 0xF6:
            if (Value == 0x10) {
                /*
                 * On joue uniquement sur DEA1 pour éviter le double beep.
                 */
                if (Reg_Xo7.PC.W == 0xDEA1) {
					unsigned int tone_period;
					unsigned int duration_counter;
					int duration_ms;
					int freq_hz;

					tone_period = Reg_Xo7.DE.W;
					duration_counter = Reg_Xo7.HL.W;

					duration_ms = (duration_counter * 50) / 44;

					if (duration_ms < 10)
						duration_ms = 10;

					freq_hz = Sound_X07_Period_To_Freq(tone_period);
					/*
					fprintf(stderr,
						"[BEEP REAL] DE=%04X HL=%04X -> freq=%d Hz duration=%d ms\n",
						tone_period,
						duration_counter,
						freq_hz,
						duration_ms
					);
					fflush(stderr);
					*/
					Sound_Beep(freq_hz, duration_ms);
				}
                /*
                 * F6=0x10 est une impulsion.
                 */
                Port_FX.W.F6 = Value & ~0x10;
            } else {
                Port_FX.W.F6 = Value;
            }
            break;
    	case 0xF7 : /* Données UART */
            Port_FX.W.F7 = Value;
            Port_FX.R.F7 = Value;
            break;
        case 0x90:
        case 0x91:
        case 0x92:
        case 0x93:
        case 0x94:
        case 0x95:
        case 0x96:
        case 0x97:
            X720_CTRL = Value;

            /*
             * A011 alterne souvent 83/03 pour la sortie/cursor : trop bavard.
             * On garde les changements de mode et commandes ROM utiles.
             */
            if (Trace_X720 && TRACE_X720_OUT &&
                !(Reg_Xo7.PC.W == 0xA011 && (Value == 0x83 || Value == 0x03))) {
                fprintf(stderr,
                        "[X720 OUT] PC=%04X PORT=%02X VALUE=%02X "
                        "A=%02X BC=%04X DE=%04X HL=%04X "
                        "FSR=%d AG=%d PLUS1K=%d GM=%d CSS=%d\n",
                        Reg_Xo7.PC.W,
                        Port & 0xFF,
                        Value,
                        Reg_Xo7.AF.B.h,
                        Reg_Xo7.BC.W,
                        Reg_Xo7.DE.W,
                        Reg_Xo7.HL.W,
                        (Value >> 7) & 1,
                        (Value >> 6) & 1,
                        (Value >> 5) & 1,
                        (Value >> 2) & 7,
                        Value & 3);
            }
            return;
        case 0xDF:
            return;
		default:
			fprintf (stderr,"Erreur de port ....");
			fprintf(stderr,
				"[OUT] PC=%04X PORT=%02X VALUE=%02X  F2=%02X F3=%02X F4=%02X F5=%02X\n",
				Reg_Xo7.PC.W,
				Port,
				Value,
				Port_FX.W.F2,
				Port_FX.W.F3,
				Port_FX.W.F4,
				Port_FX.W.F5
			);
			fflush(stderr);
			break;
	}	
	return;
}

byte InZ80(register word Port) {
	Port &= 0xFF;
	#if AFF_IN
	fprintf (stderr,"InZ80 (%04X) Out %02X\n",Reg_Xo7.PC.W,Port);
	#endif

	byte Value=0;
	switch (Port) {
        case 0x90:
        case 0x91:
        case 0x92:
        case 0x93:
        case 0x94:
        case 0x95:
        case 0x96:
        case 0x97:
        {
            byte ret = 0x00;

            if (Trace_X720 && TRACE_X720_IN) {
                static unsigned long x720_in_count = 0;
                x720_in_count++;

                /* Trace échantillonnée uniquement si activée. */
                if ((x720_in_count % 1000) == 0) {
                    fprintf(stderr,
                            "[X720 IN ] PC=%04X PORT=%02X RET=%02X "
                            "A=%02X BC=%04X DE=%04X HL=%04X CTRL=%02X count=%lu\n",
                            Reg_Xo7.PC.W,
                            Port & 0xFF,
                            ret,
                            Reg_Xo7.AF.B.h,
                            Reg_Xo7.BC.W,
                            Reg_Xo7.DE.W,
                            Reg_Xo7.HL.W,
                            X720_CTRL,
                            x720_in_count);
                }
            }

            return ret;
        }
		case 0xF0 : /* Controle des interruptions */
			Value = Port_FX.R.F0;
			break;
		case 0xF1 : /* XBRR : Stockage des Informations venant du CCU */
			Value = Port_FX.R.F1;
			/*Port_FX.R.F2 &= 0xFE;*/
			break;
		case 0xF2 : /* Flags sur l'etat du Canon XO7 */
			/*if (Port_FX.W.F5 & 0x08) Port_FX.R.F2 |=0x01;
			else                     Port_FX.R.F2 &=0xFE;*/
			if (Port_FX.W.F5 & 0x04)
				Port_FX.R.F2 |=0x02;
			else
				Port_FX.R.F2 &=0xFD;
			Value = Port_FX.R.F2 | 2;
			break;
		case 0xF3 : /* ~~~Vide~~~ */
			Value = Port_FX.R.F3;
			break;
		case 0xF4 : /* Status modes / buzzer */
            /*
             * Attention :
             * F4.W contient les bits écrits par la ROM.
             * Mais F4.R ne doit pas forcément retourner tous ces bits.
             *
             * La ROM teste F4 & 0x0C pour savoir si le buzzer est occupé.
             * Si on retourne CE, alors 0x0C reste actif et les BEEP suivants
             * sont bloqués.
             */
            Value = Port_FX.R.F4 & ~0x0C;

            if (Reg_Xo7.PC.W >= 0xC37B && Reg_Xo7.PC.W <= 0xC393) {
                /*
                fprintf(stderr,
                    "[IN F4 SOUND WAIT] PC=%04X R.F4=%02X returned=%02X W.F4=%02X\n",
                    Reg_Xo7.PC.W,
                    Port_FX.R.F4,
                    Value,
                    Port_FX.W.F4
                );
                fflush(stderr);
                */
            }

            break;
		case 0xF5 : /* ~~~Vide~~~ */
			Value = Port_FX.R.F5;
			break;
        case 0xF6 : /* Status de l'UART / K7 */
            /*
             * Si un fichier CAS est ouvert et que la ROM a consomme
             * l'octet precedent, on prepare l'octet suivant.
             * Ne pas tester F4 ici : pendant CLOAD la ROM change F4
             * et sinon on ne lit qu'un seul octet.
             */
            if (K7_Allow_Start && K7_Read_Enabled && Presence_k7 && Fichier_k7 && !(Port_FX.R.F6 & 0x02))
                Receive_from_K7(&Port_FX);

            Value = Port_FX.R.F6 | 0x05;
            break;
        case 0xF7 : /* Donnees recues par l'UART / K7 */
            Value = Port_FX.R.F7;

            /* La ROM vient de consommer l'octet CAS. */
            if (K7_Allow_Start && K7_Read_Enabled && Presence_k7 && Fichier_k7)
                Port_FX.R.F6 &= ~0x02;

            break;
	}
	return (Value);
}

/** PatchZ80() ***********************************************/
/** Z80 emulation calls this function when it encounters a  **/
/** special patch command (ED FE) provided for user needs.  **/
/** For example, it can be called to emulate BIOS calls,    **/
/** such as disk and tape access. Replace it with an empty  **/
/** macro for no patching.                                  **/
/************************************ TO BE WRITTEN BY USER **/
void PatchZ80(register Z80 *R) {
 fprintf (stderr,"PatchZ80\n");
 return;
}

/** LoopZ80() ************************************************/
/** Z80 emulation calls this function periodically to check **/
/** if the system hardware requires any interrupts. This    **/
/** function must return an address of the interrupt vector **/
/** (0x0038, 0x0066, etc.) or INT_NONE for no interrupt.    **/
/** Return INT_QUIT to exit the emulation loop.             **/
/************************************ TO BE WRITTEN BY USER **/

static unsigned long now_ms(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);

	return (unsigned long)(tv.tv_sec * 1000UL) + (unsigned long)(tv.tv_usec / 1000UL);
}


static void Auto_CLOAD_Pump(void)
{
	static unsigned long last_key_ms = 0;
	unsigned long t;
	static const char cmd[] = "CLOAD\r";

	if (!Auto_CLOAD_Pending)
		return;

	t = now_ms();

	/* Laisser la ROM afficher le prompt avant de taper CLOAD. */
	if ((t - Auto_CLOAD_StartMs) < 2500)
		return;

	/* Injecter lentement, comme le clavier, pas tout d'un coup. */
	if ((t - last_key_ms) < 80)
		return;

	if (Auto_CLOAD_Pos < (int)(sizeof(cmd) - 1)) {
		Add_Key((byte)cmd[Auto_CLOAD_Pos]);
		fprintf(stderr, "[AUTO KEY] %02X '%c'\n",
				(byte)cmd[Auto_CLOAD_Pos],
				(cmd[Auto_CLOAD_Pos] >= 32 && cmd[Auto_CLOAD_Pos] <= 126) ? cmd[Auto_CLOAD_Pos] : '.');
		fflush(stderr);
		Auto_CLOAD_Pos++;
		last_key_ms = t;
		return;
	}

	/*
	 * Maintenant seulement la cassette est autorisee.
	 * Dans la ROM, le OUT F5 bit 0x04 peut avoir ete fait juste avant
	 * que notre temporisation autorise la K7. Dans ce cas, il ne sera pas
	 * forcement repete : on lance donc le premier octet ici.
	 */
	K7_Allow_Start = 1;
	Auto_CLOAD_Pending = 0;

	if (Presence_k7 && Fichier_k7 && !K7_EOF) {
		Mode_K7 = 1;
		K7_Read_Enabled = 1;
		Receive_from_K7(&Port_FX);
	}
	fprintf(stderr, "[K7] CLOAD injecte, demarrage lecture CAS\n");
	fflush(stderr);
}

word LoopZ80(register Z80 *R)
{
	static int Nb = 0;
	static int Lec_K7 = 0;
	int Event;

    sync_emulation();

	Event = Voir_Xevent();
	X720_Video_Service();

	if (Event == 27)
		return INT_QUIT;

	if (Power_Off) {
		while (Power_Off) {
			Event = Voir_Xevent();
			X720_Video_Service();
			if (Event == 27)
				return INT_QUIT;

			usleep(20000);
		}
	return INT_NONE;
	}
		
	if (Event > 0 && Event < 256) {
		Add_Key((byte)Event);
	}

	BasicText_Pump();
	Auto_CLOAD_Pump();
	CSave_Pump();

	{
		static unsigned long last_cursor_ms = 0;
		unsigned long t = now_ms();

		if ((t - last_cursor_ms) >= 500) {
			Aff_Curseur();
			last_cursor_ms = t;
		}
	}

	if (R->IFF & 0x01) {
		if (Clavier.Nb_Key) {
			Clavier.Nb_Key--;

			Port_FX.R.F0 = 0x00;
			Port_FX.R.F1 = Clavier.Buff_Key[Clavier.Pt_Lec];
			/*			
			fprintf(stderr,
				"[KEY_TO_T6834] F1=0x%02X '%c' Nb=%d Pt_Lec=%d Pt_Ecr=%d IFF=0x%02X\n",
				Port_FX.R.F1,
				(Port_FX.R.F1 >= 32 && Port_FX.R.F1 <= 126) ? Port_FX.R.F1 : '.',
				Clavier.Nb_Key,
				Clavier.Pt_Lec,
				Clavier.Pt_Ecr,
				R->IFF);
			*/
			Port_FX.R.F2 |= 0x01;

			Clavier.Pt_Lec++;
			if (Clavier.Pt_Lec >= 20)
				Clavier.Pt_Lec = 0;

			IT_T6834 = 0;
			return IT_RST_A;
		}

		if (General_Info.Break == 1) {
			Port_FX.R.F0 = 0x80;
			Port_FX.R.F1 = 0x05;
			Port_FX.R.F2 |= 0x01;
			IT_T6834 = 0;
			General_Info.Break = 0;
			fprintf(stderr, "Break\n");
			return IT_RST_A;
		}

		if (IT_T6834) {
			switch (IT_T6834) {
				case 1:
					Receive_from_T6834(LEC_T6834, &Port_FX);
					IT_T6834 = 0;
					return IT_RST_A;

				case 2:
					Port_FX.R.F0 = 0x80;
					Port_FX.R.F1 = Nb;
					Port_FX.R.F2 |= 0x01;

					Nb++;
					if ((Nb == 4) || (Nb == 7))
						Nb++;
					if (Nb > 255)
						Nb = 0;

					IT_T6834 = 0;
					return IT_RST_A;

				case 3:
					/*
					 * IRQ K7 seulement si un vrai octet est disponible.
					 * Si EOF ou cassette fermee, on annule.
					 */
					if (!K7_Allow_Start || !K7_Read_Enabled || !Presence_k7 || !Fichier_k7 || K7_EOF || !(Port_FX.R.F6 & 0x02)) {
						IT_T6834 = 0;
						Lec_K7 = 0;
						break;
					}

					if (Lec_K7 >= 5) {
						IT_T6834 = 0;
						Lec_K7 = 0;
						return IT_RST_B;
					} else {
						Lec_K7++;
					}
					break;
			}
		}
	}

	return INT_NONE;
}

/*---------------------------------------------------------------------------*/
/*                                                                           */
/*                                                                           */
/*---------------------------------------------------------------------------*/
void Active_Debugger (int a) {
	fprintf (stderr,"Interruption!!!\n");
	Reg_Xo7.Trace=1;
	signal (a,Active_Debugger);
	DisplayReg(&Reg_Xo7);
	exit(0);
}

/*---------------------------------------------------------------------------*/
/*                                                                           */
/*                                                                           */
/*---------------------------------------------------------------------------*/
void Active_Int (int a) {
	static int Type = 2;
	fprintf (stderr,"Int T6834!!! 0X020F = %d   0X026D: %d %d\n",RAM[0x020f],RAM[0x026d],Type);
	signal (a,Active_Int);
	IT_T6834 = Type;
}
/*---------------------------------------------------------------------------*/
/*                                                                           */
/*                                                                           */
/*---------------------------------------------------------------------------*/
int Save_Ram_Temp(void)
{
	FILE *f;
	size_t n;
	unsigned int taille = X07_RAM_SAVE_SIZE;

	f = fopen(X07_RAM_SAVE_FILE, "wb");
	if (!f) {
		perror("Save_Ram_Temp fopen");
		return RC_KO;
	}

	/*
	 * Entête simple :
	 * - magic
	 * - taille réellement sauvegardée
	 * - RAM utile
	 */
	if (fwrite(X07_RAM_MAGIC, 1, X07_RAM_MAGIC_LEN, f) != X07_RAM_MAGIC_LEN) {
		perror("Save_Ram_Temp fwrite magic");
		fclose(f);
		return RC_KO;
	}

	if (fwrite(&taille, 1, sizeof(taille), f) != sizeof(taille)) {
		perror("Save_Ram_Temp fwrite taille");
		fclose(f);
		return RC_KO;
	}

	n = fwrite(RAM, 1, X07_RAM_SAVE_SIZE, f);

	if (fflush(f) != 0) {
		perror("Save_Ram_Temp fflush");
		fclose(f);
		return RC_KO;
	}

	fclose(f);

	if (n != X07_RAM_SAVE_SIZE) {
		fprintf(stderr,
		        "Save_Ram_Temp: sauvegarde RAM incomplète %lu/%lu octets\n",
		        (unsigned long)n,
		        (unsigned long)X07_RAM_SAVE_SIZE);
		return RC_KO;
	}

	fprintf(stderr,
	        "RAM sauvegardée dans %s (%lu octets)\n",
	        X07_RAM_SAVE_FILE,
	        (unsigned long)X07_RAM_SAVE_SIZE);

	return RC_OK;
}

int Load_Ram_Temp(void)
{
	FILE *f;
	char magic[8];
	unsigned int taille = 0;
	size_t n;

	f = fopen(X07_RAM_SAVE_FILE, "rb");
	if (!f)
		return RC_KO;

	memset(magic, 0, sizeof(magic));

	if (fread(magic, 1, X07_RAM_MAGIC_LEN, f) != X07_RAM_MAGIC_LEN) {
		fclose(f);
		return RC_KO;
	}

	if (memcmp(magic, X07_RAM_MAGIC, X07_RAM_MAGIC_LEN) != 0) {
		fclose(f);
		fprintf(stderr, "Load_Ram_Temp: mauvais fichier RAM\n");
		return RC_KO;
	}

	if (fread(&taille, 1, sizeof(taille), f) != sizeof(taille)) {
		fclose(f);
		return RC_KO;
	}

	if (taille != X07_RAM_SAVE_SIZE) {
		fclose(f);
		fprintf(stderr,
		        "Load_Ram_Temp: taille incompatible %u/%lu\n",
		        taille,
		        (unsigned long)X07_RAM_SAVE_SIZE);
		return RC_KO;
	}

	n = fread(RAM, 1, X07_RAM_SAVE_SIZE, f);
	fclose(f);

	if (n != X07_RAM_SAVE_SIZE) {
		fprintf(stderr,
		        "Load_Ram_Temp: lecture RAM incomplète %lu/%lu octets\n",
		        (unsigned long)n,
		        (unsigned long)X07_RAM_SAVE_SIZE);
		return RC_KO;
	}

	fprintf(stderr,
	        "RAM restaurée depuis %s (%lu octets)\n",
	        X07_RAM_SAVE_FILE,
	        (unsigned long)X07_RAM_SAVE_SIZE);

	return RC_OK;
}


