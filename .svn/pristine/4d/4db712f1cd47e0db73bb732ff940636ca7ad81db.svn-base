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
/* 10/12/26 | L.VALTER  |  0002  | Suite                                    */
/*----------+-----------+--------+------------------------------------------*/

#ifdef VAR_GLOB
#define LIEU
#else
#define LIEU extern
#endif

#define X07_VIDEO_SIZE_H
#define MAX_X 120
#define MAX_Y 32

/*----------------------------------------------------------------------*/
/*                 Declaration des registres du Z80                     */
/*----------------------------------------------------------------------*/
LIEU Z80     Reg_Xo7;

/*----------------------------------------------------------------------*/
/*    Declaration des registres de L'EPLD de dialogue avec le T6834     */
/*----------------------------------------------------------------------*/
LIEU PorT_FX Port_FX;

/*----------------------------------------------------------------------*/
/*                 Declaration des variables globales                   */
/*----------------------------------------------------------------------*/
LIEU byte *RAM;
LIEU byte Int_nsc800_BB;
LIEU byte X07_OnStat;
LIEU unsigned int Tmp;
LIEU int IT_T6834;
LIEU CLAVIER Clavier;
LIEU GENERAL_INFO General_Info;
LIEU byte TestChr_KeyDown[256];
LIEU byte X07_Keyb_Enable;
LIEU byte X07_Click_Enable;
LIEU byte X07_Repeat_Enable;
LIEU byte X07_UDK_Enable;
LIEU byte X07_Screen_Enable;
LIEU byte X07_Repeat_Enable;
