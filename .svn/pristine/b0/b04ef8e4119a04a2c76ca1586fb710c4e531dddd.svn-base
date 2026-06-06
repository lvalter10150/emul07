#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    int statut = EXIT_FAILURE;

    if(0 != SDL_Init(SDL_INIT_VIDEO))
    {
        fprintf(stderr, "Erreur SDL_Init : %s", SDL_GetError());
        goto Quit;
    }
    window = SDL_CreateWindow("Canon X07", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              1081, 289, SDL_WINDOW_SHOWN);
    if(NULL == window)
    {
        fprintf(stderr, "Erreur SDL_CreateWindow : %s", SDL_GetError());
        goto Quit;
    }
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if(NULL == renderer)
    {
        fprintf(stderr, "Erreur SDL_CreateRenderer : %s", SDL_GetError());
        goto Quit;
    }

    SDL_SetRenderDrawColor(renderer, 211, 211, 211, 255);

	/*
    for (int i=1;i<960;i=i+9) {
      for (int j=1;j<256;j=j+9) {
        SDL_Rect rect = {i, j, 8, 8};
    	SDL_RenderFillRect(renderer, &rect);
      }
    }
    SDL_RenderPresent(renderer);
    */
    
    // Test Pixel
    unsigned char pixel[120][32];
    
    for (int i=0;i<120;i++) {
		for (int j=0;j<32;j++) {
			pixel[i][j] = 0;
		}
	}
	pixel[0][0] = 1;
	pixel[119][31] = 1;
	
	
	
    for (int i=0;i<120;i++) {
		for (int j=0;j<32;j++) {
			if (pixel[i][j] == 0) {
				SDL_SetRenderDrawColor(renderer, 211, 211, 211, 255);
			} else {
				SDL_SetRenderDrawColor(renderer, 48, 48, 48, 255);
			}
			SDL_Rect rect = {(i*8)+1+i, (j*8)+1+j, 8,8};
			SDL_RenderFillRect(renderer, &rect);
		}
	}
    SDL_RenderPresent(renderer);
    
    
    
    statut = EXIT_SUCCESS;
    SDL_Event event;
    while (SDL_WaitEventTimeout(&event,5000)) {
        // check for messages
        switch (event.type) {
            // exit if the window is closed
        case SDL_QUIT:
			goto Quit;
            break;
            // check for keypresses
        case SDL_KEYDOWN:
            break;
        case SDL_KEYUP:
            break;
        default:
            break;
        }
    }

Quit:
    if(NULL != renderer)
        SDL_DestroyRenderer(renderer);
    if(NULL != window)
        SDL_DestroyWindow(window);
    SDL_Quit();
    return statut;
}
