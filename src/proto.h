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
/* 16/02/04 | J.BRIGAUD |  0002  | Ajout prototypes gestion K7              */
/* 10/05/26 | L.VALTER  |  0003  | Suite                                    */
/*----------+-----------+--------+------------------------------------------*/

void DisplayReg      (register Z80 *R);
void Load_Rom        (void);
void Active_Debugger (int a);
void Active_Int      (int a);

/* XO7.C */
/*-------*/
void Print            (byte Cmd, PorT_FX *Port);
void Send_to_K7       (PorT_FX *Port);
void Receive_from_K7  (PorT_FX *Port);
void Init_Etat_Xo7    (void);
void init_k7          (void);
int  open_k7          (char *K7_Name);
void close_k7         (void);
void display_info_k7  (void);
void Reset_Xo7        (void);
int Save_Ram_Temp     (void);
int Load_Ram_Temp     (void);
void Power_Off_Xo7    (void);
void Power_OnBreak_Xo7(void);
void X720_Dump_Text   (void);

/* T6834.c */
/*---------*/
void Init_T6834           (void);
void Send_to_T6834        (PorT_FX *Port);
void Receive_from_T6834   (byte Cmd, PorT_FX *Port);
int Init_Reponse_T6834    (byte Ordre, byte *Rsp, PorT_FX *Port);
void AffUdk_ON            (byte State);

/* Video.c */
/*---------*/
int  Voir_Xevent		(void);
void Add_Key			(byte Key);
void X07_Key_Monitor	(byte Key);
void Add_FKey			(byte F_Key);
void Clear_KeyBuffer	(void);
void iniscreen    		();
void stopintr			();
void startintr			();
void new_timer    (int a);
void Pset         (byte x, byte y);
void Preset       (byte x, byte y);
void AffCar       (byte x, byte y, byte Car);
void LineClear    (byte y);
void ClrScr       (void);
void Scroll_Video (void);
void Line         (byte x1,byte y1,byte x2,byte y2);
void Circle       (byte x1, byte y1, byte r);
void Aff_Curseur  (void);
void BasicText_Open(char *filename);
void BasicText_Pump(void);
byte TestChr_IsDown(byte chr);
void TestChr_Clear(byte chr);
void X07_LCD_Off(void);
void X07_LCD_On(void);

/*
 * video_x720.c
 */
int  X720_Video_Init(void);
void X720_Video_Close(void);
void X720_Video_SetEnabled(int enabled);
void X720_Video_MarkDirty(void);
int  X720_Video_IsReady(void);
void X720_Video_Update(void);
void X720_Video_Service(void);
void X720_Video_DebugDump(void);

/*
 * Rendu graphique logique X-720
 */
void X720_GfxClear(byte color);
void X720_GfxSetPixel(int x, int y, byte color);
void X720_GfxWriteFromRom(word logical_addr,
                          word phys_addr,
                          byte old_value,
                          byte new_value,
                          int x,
                          int y,
                          byte fg,
                          byte bg,
                          byte ctrl);

/*
 * Etat X-720 exposé par x07.c
 */
byte X720_GetCtrl(void);
/* Sound.c */
/*---------*/
void Sound_Init(void);
void Sound_Close(void);
void Sound_Beep(int freq_hz, int duration_ms);
void Sound_Beep_X07(unsigned char tone_x07, unsigned char length_x07);
int Sound_X07_Period_To_Freq(unsigned int period);
void X07_KeyClick(void);
