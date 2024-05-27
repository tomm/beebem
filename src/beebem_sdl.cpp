/****************************************************************************/
/*              Beebem - (c) David Alan Gilbert 1994                        */
/*              ------------------------------------                        */
/* This program may be distributed freely within the following restrictions:*/
/*                                                                          */
/* 1) You may not charge for this program or for any part of it.            */
/* 2) This copyright message must be distributed with all copies.           */
/* 3) This program must be distributed complete with source code.  Binary   */
/*    only distribution is not permitted.                                   */
/* 4) The author offers no warrenties, or guarentees etc. - you use it at   */
/*    your own risk.  If it messes something up or destroys your computer   */
/*    thats YOUR problem.                                                   */
/* 5) You may use small sections of code from this program in your own      */
/*    applications - but you must acknowledge its use.  If you plan to use  */
/*    large sections then please ask the author.                            */
/*                                                                          */
/* If you do not agree with any of the above then please do not use this    */
/* program.                                                                 */
/* Please report any problems to the author at beebem@treblig.org           */
/****************************************************************************/

#include "SDL_pixels.h"
#include "SDL_render.h"
#if HAVE_CONFIG_H
#include "config.h"
#endif

#include "beebem_sdl.h"

#include "beebwin.h"
#include "line.h"
#include "log.h"
#include "main.h" // Remove this once command line stuff fixed
#include "types.h"

#include <string.h>

static int audioDeviceId;
#define AUDIO_BUFFER_LEN 1024

bool maintain_4_3_aspect = true;

// The BeebEm emulator core (the Windows code) calls this when it wants to
// play some samples.  We place those samples in our bloody huge buffer
// instead.
void AddBytesToSDLSoundBuffer(void *p, int len) {
  if (SDL_QueueAudio(audioDeviceId, p, len) != 0) {
    printf("error queueing audio: %s\n", SDL_GetError());
  }
}

/* Globals:
 *	-	-	-	-	-	-	-
 */

SDL_Surface *icon = NULL;

// 800x600 bytes
static uint8_t *video_output = NULL;
// 800x600x4 bytes (abgr)
static uint32_t *video_rgba_output = NULL;
static SDL_Window *sdl_window = NULL;
static SDL_Renderer* sdl_renderer = NULL;
static SDL_Texture* beeb_tex = NULL;

/* If we're using X11, we need to release the Caps Lock key ourselves.
 */
int cfg_HaveX11 = 0;

/* Emulate a CRT display (odd scanlines are always dark. Will become an
 * option later on).
 */
//#define EMULATE_CRT
int cfg_EmulateCrtGraphics = 1;
int cfg_EmulateCrtTeletext = 0;

int cfg_Fullscreen_Resolution = RESOLUTION_640X480_S; // -1;
int cfg_Windowed_Resolution = RESOLUTION_640X480_S;   // -1;
int cfg_VerticalOffset = ((512 - 480) / 2);

/* If this is defined then the sound code will dump samples (causing distortion)
 * whenever the buffer becomes too large (i.e.: over 5 lots of samples or some
 * such).  If I don't do this then the sound effects in games will happen
 * longer and longer after the event.
 *
 * This is coursed by other processes slowing down BeebEm and the timing of the
 * emulator becoming wrong.  BeebEm will try to compensate, by creating new
 * sound data for the missing time that's then dumped into my sound buffer.
 *
 * It should all work nicely, the emulator core will catchup to the current time
 * and I'll get lots of new sound to play, but the timings off somewhere
 * so this missing sound is converted from a catchup into a latency problem.
 * Unfortunely the more and more interruptions BeebEm has to handle the greater
 * the sound latency problem becomes..  So for I'm going to dump samples when
 * the latency is too great..  If you'd rather have nicer sound (with the
 * latency problem) then remove the definition below.
 */
//#define WANT_LOW_LATENCY_SOUND
int cfg_WantLowLatencySound = 1;

/* Wait type for 'sleep'.
 */
int cfg_WaitType = OPT_SLEEP_OS;

/*	-	-	-	-	-	-	-
 */

SDL_AudioSpec wanted;

int InitializeSDLSound(int soundfrequency) {
  wanted.freq = soundfrequency;
  wanted.format = AUDIO_U8;
  wanted.channels = 1;
  wanted.samples = AUDIO_BUFFER_LEN;
  wanted.callback = NULL;
  wanted.userdata = NULL;

  /* Open the audio device, forcing the desired format */
  audioDeviceId = SDL_OpenAudioDevice(NULL, 0, &wanted, NULL, 0);
  if (!audioDeviceId) {
    fprintf(stderr, "Couldn't open audio: %s\n", SDL_GetError());
    return (0);
  }

  SDL_PauseAudioDevice(audioDeviceId, 0);
  //SDL_Delay(500);

  return (1);
}

void FreeSDLSound(void) { SDL_CloseAudio(); }

/* Setup palette.
 */
void SetBeebEmEmulatorCoresPalette(unsigned char *cols, int palette_type) {
  SDL_Color colors[8];

  /* BeebEm video.cpp needs to use colors 0 to 7.
   */
  for (int i = 0; i < 8; i++)
    *(cols++) = (unsigned char)i;

  if (sdl_window == NULL) {
    fprintf(stderr, "Trying to read palette before window is"
                    " opened!\nYou will need to fix this..");
    exit(1);
  }

  /* Set the palette:
   */
  for (int i = 0; i < 8; ++i) {
    float r, g, b;

    r = (float)(i & 1) * 255;
    g = (float)((i & 2) >> 1) * 255;
    b = (float)((i & 4) >> 2) * 255;

    if (palette_type != BeebWin::RGB) {
      r = g = b = (float)(0.299 * r + 0.587 * g + 0.114 * b);

      switch (palette_type) {
      case BeebWin::AMBER:
        r *= (float)1.0;
        g *= (float)0.8;
        b *= (float)0.1;
        break;
      case BeebWin::GREEN:
        r *= (float)0.2;
        g *= (float)0.9;
        b *= (float)0.1;
        break;
      }
    }

    colors[i].r = (int)r;
    colors[i].g = (int)g;
    colors[i].b = (int)b;
  }

  /* Set bitmaps palette.
   */
  //XXX SDL_SetColors(video_output, colors, 0, 8);

  /* Force X Servers palette to change to our colors.
   */
  //#ifdef WITH_FORCED_CM
  //XXX SDL_SetColors(screen_ptr, colors, 0, 8);
  //#endif

  /* Set LED colors.
   */
  colors[0].r = 127;
  colors[0].g = 0;
  colors[0].b = 0;
  colors[1].r = 255;
  colors[1].g = 0;
  colors[1].b = 0;
  colors[2].r = 0;
  colors[2].g = 127;
  colors[2].b = 0;
  colors[3].r = 0;
  colors[3].g = 255;
  colors[3].b = 0;
  //XXX SDL_SetColors(video_output, colors, 64, 4);

  //#ifdef WITH_FORCED_CM
  //XXX SDL_SetColors(screen_ptr, colors, 64, 4);
  //#endif

  /* Menu colors.
   */
  colors[0].r = 127 + 64;
  colors[0].g = 127 + 64;
  colors[0].b = 127 + 64;

  colors[1].r = (int)(colors[0].r * 0.6666);
  colors[1].g = (int)(colors[0].g * 0.6666);
  colors[1].b = (int)(colors[0].b * 0.6666);

  colors[2].r = (int)(colors[0].r * 1.3333);
  colors[2].g = (int)(colors[0].g * 1.3333);
  colors[2].b = (int)(colors[0].b * 1.3333);

  colors[3].r = (int)(colors[0].r * 0.9);
  colors[3].g = (int)(colors[0].g * 0.9);
  colors[3].b = (int)(colors[0].b * 0.9);

  //XXX SDL_SetColors(video_output, colors, 68, 4);

  //#ifdef WITH_FORCED_CM
  //XXX SDL_SetColors(screen_ptr, colors, 68, 4);
  //#endif
}

int Create_Screen(void) {

    sdl_window = SDL_CreateWindow(
        "Beebem SDL2",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        640, 480,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

    SDL_ShowCursor(0);

    sdl_renderer = SDL_CreateRenderer(sdl_window, -1, SDL_RENDERER_ACCELERATED);
    beeb_tex = SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, 800, 600);
    SDL_SetTextureScaleMode(beeb_tex, SDL_ScaleModeBest);

  ClearVideoWindow();

  // printf("5: ClearVideoWindow called - now returning with true\n");

  return true;
}

void Destroy_Screen(void) {
#if 0
  if (screen_ptr != NULL)
    SDL_FreeSurface(screen_ptr);
#endif /* 0 */
}

int InitialiseSDL(int argc, char *argv[]) {
  // int tmp_argc;
  // char **tmp_argv;

  // tmp_argv=argv;
  // tmp_argc = argc;

  /* Initialize SDL and handle failures.
   */
  if (SDL_Init(SDL_INIT_VIDEO  | SDL_INIT_AUDIO ) < 0) {
    fprintf(stderr, "Unable to initialise SDL: %s\n", SDL_GetError());
    return false;
  }

  /* Cleanup SDL when exiting.
   */
  atexit(SDL_Quit);

  /* If we are using X11 set Caps lock so it's immediately released.
   */
#if 0
  if (SDL_VideoDriverName(video_hardware, 1024) != NULL) {
    if (strncasecmp(video_hardware, "x11", 1024) == 0)
      cfg_HaveX11 = 1;
  }
#endif

  icon = SDL_LoadBMP(DATA_DIR "/resources/icon.bmp");
#if 0
  if (icon != NULL) {
    SDL_SetColorKey(icon, SDL_SRCCOLORKEY,
                    SDL_MapRGB(icon->format, 0xff, 0x0, 0xff));
    SDL_WM_SetIcon(icon, NULL);
  }
#endif

  // [HERE] Create Screen.

  //	SDL_ShowCursor(SDL_DISABLE);		// SDL_ENABLE

  /* Create an area the BeebEm emulator core (the Windows code)
   * can draw on.  It's hardwired to an 800x600 8bit byte per pixel
   * bitmap.
   */
  video_output = new uint8_t[800*600];
  video_rgba_output = new uint32_t[800*600];

  // Create the default screen.
  int r = Create_Screen();

  // Setup colors so we at least have something. The emulator core will
  // changes these later when the fake registry is read, but we want
  // enough colors set so the GUI (the message box) will be rendered
  // correctly.
  unsigned char cols[8];
  SetBeebEmEmulatorCoresPalette(cols, BeebWin::RGB);

  return r;

  //	InitializeSDLSound(22050);		// Fix hardwiring later..
  //	SDL_Delay(500);				// Give sound some time to init
  //	return true;
}

void UninitialiseSDL(void) {

  /* If mouse is not visible, make visible.
   */
  if (SDL_ShowCursor(SDL_QUERY) == SDL_DISABLE)
    SDL_ShowCursor(SDL_ENABLE);

  SDL_CloseAudio();
  SDL_ShowCursor(SDL_ENABLE);
  delete[] video_output;
  delete[] video_rgba_output;
}

/* Timing:
 *
 * The functions below replace the Windows 'sleep' command.  It's a bit more
 * involved here and hopefully this approach will give varied systems more of
 * a chance to execute the emulator properly.
 *
 * The definitions below set the type of wait to use when the emulator wants to
 * sleep.  It's set via the user interface on the 'Screen' page.
 */

//#define TIMING_OS 	0
//#define TIMING_FAST 	1
//#define TIMING_FASTER 	2
//#define TIMING_FASTEST	3
//#define TIMING_BUSYWAIT 4

/* Busy-wait a specific amount of time, the second arg is the start time. This
 * is more likely to be acurate on some Operating Systems and slower hardware.
 *
 * When waiting a specific period of time after calling this function use
 * SDL_Delay() as the second arg:
 *
 * ---
 *
 * BusyWait(10, SDL_Delay()); 	// Will wait 10 milliseconds.
 *
 * ---
 *
 * If you want <code> to take an absolute amount of time, set the second arg to
 * the start time:
 *
 * ---
 *
 * time = SDL_Delay();
 *
 * <code>
 *
 * BusyWait(10, time); 		// Will wait 10 ms from when 'time' var set.
 *				//
 *				// So will wait for 10 ms - amount of time
 *				// <code> took to execute.  If <code> took more
 *				// than 10 ms to execute, will wait 0 ms.
 *
 *
 * As this is a busy-wait, no CPU specific 'sleep' instructions will be issued,
 * so I don't recommend this for laptops!
 *
 * Some slow systems will benefit from this (like the Intel SA1110 ARM) as
 * it doesn't need to ask the OS to handle tiny waiting periods.
 *
 * (I DO NOT recommend this on an Intel Pentium 4! Unless you work at Air Bus
 * and are using your CPU fan for wind tunnel tests!)
 */
static void BusyWait(Uint32 u32TimeShouldWait, Uint32 u32StartTickCount) {
  Uint32 u32AjustedTime;

  do {
    u32AjustedTime = SDL_GetTicks();

    /* Handle wrap around after ~47 days of continued execution
     */
    if (u32AjustedTime < u32StartTickCount)
      u32AjustedTime =
          u32AjustedTime + (((Uint32)0xffffffff) - u32StartTickCount);
    else
      u32AjustedTime = u32AjustedTime - u32StartTickCount;

  } while (u32AjustedTime < u32TimeShouldWait);
}

/* If the waiting period in ms is greater than the minimum delay time, then the
 * wait is passed to the OS. Otherwise we busy-wait instead.
 */
static void SleepAndBusyWait(Uint32 u32TimeShouldWait, Uint16 u16MinTime) {
  Uint32 u32StartTickCount;

  u32StartTickCount = SDL_GetTicks();

  // Only sleep if we are sure the OS can honnor it:
  if (u32TimeShouldWait >= u16MinTime) {
    SDL_Delay(u32TimeShouldWait);
  } else {
    BusyWait(u32TimeShouldWait, u32StartTickCount);
  }
}

/* The windows.cpp Sleep function is a wrapper for this function.
 *
 * It currently supports five ways to wait for time to pass. You may get better
 * results with any of them depending on your OS and Hardware.
 */
void SaferSleep(unsigned int uiTicks) {
  /* Do nothing if BeebEm asked to wait 0 ms.
   */
  if (uiTicks < 1) {
    pERROR(dL "Asked to wait for 0 milliseconds.. Assuming this is"
              " bogus!",
           dR);
    return;
  }

  switch (cfg_WaitType) {

  /* Just pass all waits period to OS:
   */
  case OPT_SLEEP_OS:
    SDL_Delay(uiTicks);
    break;

  /* Only pass wait to OS if period is greater or equal to 2 ms:
   */
  case OPT_SLEEP_F1:
    SleepAndBusyWait(uiTicks, 2);
    break;

  /* Only pass wait to OS if period is greater or equal to 4 ms:
   */
  case OPT_SLEEP_F2:
    SleepAndBusyWait(uiTicks, 4);
    break;

  /* Only pass wait to OS if period is greater or equal to 6 ms:
   */
  case OPT_SLEEP_F3:
    SleepAndBusyWait(uiTicks, 6);
    break;

  /* Never pass waits to OS, use nasty Busy-wait for everything:
   */
  case OPT_SLEEP_BW:
    BusyWait(uiTicks, SDL_GetTicks());
    break;
  }
}

// Clear video window
void ClearVideoWindow(void) {
  SDL_RenderClear(sdl_renderer);
  SDL_RenderPresent(sdl_renderer);
}

static const uint32_t beeb_palette[8] = {
  0,
  0xff,
  0xff00,
  0xffff,
  0xff0000,
  0xff00ff,
  0xffff00,
  0xffffff
};

void RenderLine(int line, int isTeletext, int xoffset) {
  const SDL_Rect srcrect = { 0, isTeletext ? 0 : 32, 640, isTeletext ? 500 : 258 };

  // if vblank time, update screen
  if ((isTeletext && line == 499) || (!isTeletext && line == 287)) {
    int wx, wy;
    SDL_GetRendererOutputSize(sdl_renderer, &wx, &wy);

    // enforce 4:3 screen aspect ratio
    const int ASPECT_X = 4, ASPECT_Y = 3, BORDER = 0;
    wx -= 2*BORDER;
    wy -= 2*BORDER;

    SDL_Rect dstrect;
    if (!maintain_4_3_aspect) {
      dstrect = SDL_Rect { BORDER, BORDER, wx, wy };
    } else if (wx > ASPECT_X * wy / ASPECT_Y) {
        dstrect = SDL_Rect { BORDER + ((wx - ASPECT_X * wy / ASPECT_Y) >> 1), BORDER, ASPECT_X * wy / ASPECT_Y, wy };
    } else {
        dstrect = SDL_Rect { BORDER, BORDER + ((wy - ASPECT_Y * wx / ASPECT_X) >> 1), wx, ASPECT_Y * wx / ASPECT_X };
    }

    // convert paletted screen to rgba texture (but only active parts of screen)
    for (int y=0; y<srcrect.y+srcrect.h+1; y++) {
      const int line = y*800;
      for (int x=srcrect.x; x<srcrect.x+srcrect.w; x++) {
        const int pixel = line+x;
        video_rgba_output[pixel] = beeb_palette[video_output[pixel] & 7];
      }
    }
    SDL_UpdateTexture(beeb_tex, NULL, video_rgba_output, 800*4);
    SDL_RenderClear(sdl_renderer);
    SDL_RenderCopy(sdl_renderer, beeb_tex, &srcrect, &dstrect);
    SDL_RenderPresent(sdl_renderer);
  }
}


void RenderFullscreenFPS(const char *str, int y) {
  // not implemented
}

void SetWindowTitle(char *title) { /*XXX SDL_WM_SetCaption(title, NULL);*/ }

unsigned char *GetSDLScreenLinePtr(int line) {
  static int low = 1000, high = 0;

  if (video_output == NULL) {
    printf("ASKED TO RENDER SCANLINE BEFORE BUFFER CREATED.\n");
    exit(11);
  }

  if (line < low) {
    low = line;
  }

  if (line > high) {
    high = line;
  }

  // printf("%d %d\n", low, high);

  if (line < 0) {
    // printf("*** ASKED TO RENDER TO LINE %d [low=%d, high=%d\n", line, low,
    // high); SDL_Delay(500);
    return (unsigned char *)video_output;
  }

  if (line > 800 - 1) {
    // printf("*** ASKED TO RENDER TO LINE %d [low=%d, high=%d\n", line, low,
    // high);
    return (unsigned char *)video_output + 799 * 800;
  }

  return (unsigned char *)video_output + line * 800;
}

/* Converts an SDL key into a BBC key.
 *
 */

struct BeebKeyTrans {
  //  KeySym sym;
  int sym;
  int row;
  int col;
};

static struct BeebKeyTrans SDLtoBeebEmKeymap[] = {
    // SDL          BBC     BBC KEY NAME (see doc/keyboard.jpg)

    {SDLK_TAB, 6, 0},    // TAB
    {SDLK_RETURN, 4, 9}, // RETURN

    {SDLK_LCTRL, 0, 1}, // CONTROL
    {SDLK_RCTRL, 0, 1}, // CONTROL

    {SDLK_LSHIFT, 0, 0}, // SHIFT
    {SDLK_RSHIFT, 0, 0}, // SHIFT

    {SDLK_CAPSLOCK, 4, 0}, // CAPS LOCK (Totally fucked up in SDL..)
    //XXX {SDLK_LSUPER, 4, 0},   // CAPS LOCK (so Alt Gr is also CAPS-LOCK..)

    {SDLK_ESCAPE, 7, 0}, // ESCAPE
    {SDLK_SPACE, 6, 2},  // SPACE

    {SDLK_LEFT, 1, 9},  // LEFT
    {SDLK_UP, 3, 9},    // UP
    {SDLK_RIGHT, 7, 9}, // RIGHT
    {SDLK_DOWN, 2, 9},  // DOWN

    {SDLK_DELETE, 5, 9},    // DELETE
    {SDLK_BACKSPACE, 5, 9}, // DELETE

    {SDLK_INSERT, 6, 9}, // COPY

    {SDLK_0, 2, 7},   // 0
    {SDLK_1, 3, 0},   // 1
    {SDLK_2, 3, 1},   // 2
    {SDLK_3, 1, 1},   // 3
    {SDLK_4, 1, 2},   // 4
    {SDLK_5, 1, 3},   // 5
    {SDLK_6, 3, 4},   // 6
    {SDLK_7, 2, 4},   // 7
    {SDLK_8, 1, 5},   // 8
    {SDLK_9, 2, 6},   // 9
    {SDLK_a, 4, 1},   // A
    {SDLK_b, 6, 4},   // B
    {SDLK_c, 5, 2},   // C
    {SDLK_d, 3, 2},   // D
    {SDLK_e, 2, 2},   // E
    {SDLK_f, 4, 3},   // F
    {SDLK_g, 5, 3},   // G
    {SDLK_h, 5, 4},   // H
    {SDLK_i, 2, 5},   // I
    {SDLK_j, 4, 5},   // J
    {SDLK_k, 4, 6},   // K
    {SDLK_l, 5, 6},   // L
    {SDLK_m, 6, 5},   // M
    {SDLK_n, 5, 5},   // N
    {SDLK_o, 3, 6},   // O
    {SDLK_p, 3, 7},   // P
    {SDLK_q, 1, 0},   // Q
    {SDLK_r, 3, 3},   // R
    {SDLK_s, 5, 1},   // S
    {SDLK_t, 2, 3},   // T
    {SDLK_u, 3, 5},   // U
    {SDLK_v, 6, 3},   // V
    {SDLK_w, 2, 1},   // W
    {SDLK_x, 4, 2},   // X
    {SDLK_y, 4, 4},   // Y
    {SDLK_z, 6, 1},   // Z
    {SDLK_F10, 2, 0}, // f0
    {SDLK_F1, 7, 1},  // f1
    {SDLK_F2, 7, 2},  // f2
    {SDLK_F3, 7, 3},  // f3
    {SDLK_F4, 1, 4},  // f4
    {SDLK_F5, 7, 4},  // f5
    {SDLK_F6, 7, 5},  // f6
    {SDLK_F7, 1, 6},  // f7
    {SDLK_F8, 7, 6},  // f8
    {SDLK_F9, 7, 7},  // f9

    {SDLK_MINUS, 5, 7},     // "+" / ";"
    {SDLK_COMMA, 6, 6},     // "<" / ","
    {SDLK_EQUALS, 1, 7},    // "=" / "-"
    {SDLK_PERIOD, 6, 7},    // ">" / "."
    {SDLK_BACKQUOTE, 2, 8}, // "-" / "�"

    {SDLK_SEMICOLON, 4, 7}, // "@"
    {SDLK_QUOTE, 4, 8},     // "*" / ":"

    {SDLK_SLASH, 6, 8}, // "/" / "?"

    {SDLK_HASH, 1, 8}, // circumflex / tilde

    {SDLK_HOME, -2, -2}, // BREAK

    {SDLK_LEFTBRACKET, 3, 8},  // "[" / "{" or left arrow and 1/4 (mode 7)
    {SDLK_RIGHTBRACKET, 5, 8}, // "]" / "}" or right arrow and 3/4 (mode 7)

    {SDLK_BACKSLASH, 7, 8}, // "\" / "|" or 1/4 and || (mode 7)

    //,   -3,-3,  // ******** PAGE UP
    //,   -3,-4,  // ******** PAGE DOWN
    //,   -4,0,   // ******** KEYPAD PLUS
    //,   -4,1,   // ******** KEYPAD MINUS
    //              // The following key codes have different symbols in mode 7
    //,	1,8,	// *** an up arrow and a maths divison symbol or
    //,	3,8,	// *** a left facing arrow and a 1/4 percentage symbol or [/{
    //,	7,8,	// *** a 1/2 percentage symbol and two vertical lines or \/|
    //,	5,8,	// *** a right facing arrow and a 3/4 percentage symbol or ]/}

    {-1, -1, -1} // ** END OF LIST **
};

/* Converts 'SDL_keysym' into BeebEm's 'int col, row' format.
 *
 * return value: 0 = no key available, 1 = key available (pressed, col and row
 * have been set)
 */

int ConvertSDLKeyToBBCKey(SDL_Keysym keysym /*, int *pressed */, int *col,
                          int *row) {
  //	int bsymwaspressed;
  //	Uint8 *keystate;
  struct BeebKeyTrans *p = SDLtoBeebEmKeymap;

  // Calc the key's state.  We could probably pass this, but I'd rather
  // have this function as self contained as possible.
  //	keystate = SDL_GetKeyState(NULL);
  //	bsymwaspressed = keystate[keysym.sym];

  // Now we can convert this key into a BBC scancode:
  for (; ((p->row != -1) && (p->sym != keysym.sym)); p++)
    ;

  // Map the key pressed. If not matched sets as -1, -1
  *(row) = p->row;
  *(col) = p->col;
  //	*(pressed) = (bsymwaspressed ? 1 : 0);

  //	printf("KEY [%d][%d][%d]\n", keysym.sym, p->row, p->col);

  return (1);
}
