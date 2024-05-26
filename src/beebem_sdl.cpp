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

unsigned int ScalingTable[1024];

bool maintain_4_3_aspect = true;

///* Rendering scale 1 = 100%, 0.5 = 50%
// */
// static float scale = 1;


// When the user switches to the menu, use it as an excuse to flush the buffer.
void FlushSoundBuffer(void) { }

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
// 800x600x3 bytes (24-bit)
static uint8_t *video24_output = NULL;
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

void CreateScalingTable(void) {
  for (int i = 0; i < 1024; i++) {
    ScalingTable[i] = (int)(i * 0.94);
  }
}
int GetScaledScanline(int y) {
  if (y < 0 || y >= 1024)
    return 0;

  return ScalingTable[y];
}

int Create_Screen(void) {

    sdl_window = SDL_CreateWindow(
        "Beebem SDL2",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        640, 480,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);


    sdl_renderer = SDL_CreateRenderer(sdl_window, -1, SDL_RENDERER_ACCELERATED);
    beeb_tex = SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, 800, 600);
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
  video24_output = new uint8_t[800*600*24];

  // Create scaling table to convert 512/256 to 480/240
  CreateScalingTable();

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
  delete[] video24_output;
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

/* A delay function. BeebEm will 'sleep' for uiTicks milliseconds.

#define MINIMUM_DELAYTIME_INMILLISECONDS 4
#define MINIMUM_BREATHINGROOM_INMILLISECONDS 0
//#define MINIMUM_WAIT_TIME 20

void SaferSleep2(unsigned int uiTicks)
{
//	static unsigned int delta = 0;
        Uint32 u32StartTickCount, u32TimeShouldWait, u32AjustedTime;

        // Now maybe it's me going insane, but I think the code below is far
        // safer than SDL_Delay on it's own..
        if (uiTicks<1){
                pERROR(dL"Asked to wait for 0 milliseconds.. Assuming this is
bogus!", dR); return;
        }

//	if (uiTicks < MINIMUM_WAIT_TIME && cfg_WantBusyWait == 0){
//		delta += uiTicks;
//
//		if (delta >= MINIMUM_WAIT_TIME){
//			uiTicks = MINIMUM_WAIT_TIME;
//			delta -= MINIMUM_WAIT_TIME;
//		}else
//			return;
//	}


//	if (cfg_WantBusyWait == 0){
//		SDL_Delay(uiTicks);
//		return;
//	}
//
//	printf("Using busywait\n");

//	printf("%d\n", uiTicks);

        u32StartTickCount = SDL_GetTicks();
        u32TimeShouldWait = (Uint32) uiTicks;

        // Only sleep if we're sure the OS can honnor it:
        if(uiTicks > MINIMUM_DELAYTIME_INMILLISECONDS && cfg_WantBusyWait == 0){
                SDL_Delay(uiTicks - MINIMUM_BREATHINGROOM_INMILLISECONDS);
        }

        // Busy wait for any remaining time:
        do{
                u32AjustedTime = SDL_GetTicks();

                // Handle wrap around after ~47 days of continued execution
                if (u32AjustedTime < u32StartTickCount)
                        u32AjustedTime = u32AjustedTime
                         + ( ((Uint32) 0xffffffff) - u32StartTickCount);
                else
                        u32AjustedTime = u32AjustedTime - u32StartTickCount;

        }while(u32AjustedTime < u32TimeShouldWait);
}
*/

// Clear video window
void ClearVideoWindow(void) {
  SDL_RenderClear(sdl_renderer);
  SDL_RenderPresent(sdl_renderer);
#if 0
  Uint32 col = SDL_MapRGB(screen_ptr->format, 0x00, 0x00, 0x00);

  SDL_FillRect(screen_ptr, NULL, col);
  SDL_UpdateRect(screen_ptr, 0, 0, screen_ptr->w, screen_ptr->h);
#endif /* 0 */
}

void RenderLine(int line, int isTeletext, int xoffset) {
  if (line == 32) {
    for (int i=0; i<800*600; i++) {
      uint8_t p = video_output[i];
      video24_output[3*i + 0] = p & 1 ? 255 : 0;
      video24_output[3*i + 1] = p & 2 ? 255 : 0;
      video24_output[3*i + 2] = p & 4 ? 255 : 0;
    }
    // if line is zero do a buffer flip
    SDL_UpdateTexture(beeb_tex, NULL, video24_output, 800*3);
    // grey background on screen borders
    SDL_SetRenderDrawColor(sdl_renderer, 15, 15, 15, 0);
    SDL_RenderClear(sdl_renderer);

    int wx, wy;
    SDL_GetRendererOutputSize(sdl_renderer, &wx, &wy);
    SDL_Rect srcrect = { 0, isTeletext ? 0 : 32, 640, isTeletext ? 500 : 258 };
    SDL_Rect dstrect;
    // enforce 4:3 screen aspect ratio
    const int ASPECT_X = 4, ASPECT_Y = 3;
    if (!maintain_4_3_aspect) {
      dstrect = SDL_Rect { 0, 0, wx, wy };
    } else if (wx > ASPECT_X * wy / ASPECT_Y) {
        dstrect = SDL_Rect { ((wx - ASPECT_X * wy / ASPECT_Y) >> 1), 0, ASPECT_X * wy / ASPECT_Y, wy };
    } else {
        dstrect = SDL_Rect { 0, ((wy - ASPECT_Y * wx / ASPECT_X) >> 1), wx, ASPECT_Y * wx / ASPECT_X };
    }
    SDL_RenderCopy(sdl_renderer, beeb_tex, &srcrect, &dstrect);
    SDL_RenderPresent(sdl_renderer);
  }

#if 0
  static int last_isTeletext = 1, last_xoffset = 0, last_mode_graphics = 0,
             last_mode_text = 0;

  bool fullscreen_val = false;

  int scan_double = 0;
  int disable_grille_for_teletext = 0;

  if (mainWin != NULL)
    fullscreen_val = mainWin->IsFullScreen();

  // If graphics rendering mode has changed, clear whole screen.
  if (cfg_EmulateCrtGraphics != last_mode_graphics) {
    ClearVideoWindow();
    last_mode_graphics = cfg_EmulateCrtGraphics;
  }

  // if text rendering mode has changed, clear whole screen.
  if (cfg_EmulateCrtTeletext != last_mode_text) {
    ClearVideoWindow();
    last_mode_text = cfg_EmulateCrtTeletext;
  }

  // Don't bother to render if not active.
#if 0
  if ((SDL_GetAppState() & SDL_APPACTIVE) == 0)
    return;
#endif

  // printf("%d\n", xoffset);

  // If mode changes between teletext and graphics clear the screen.
  // *** this could really be nasty with split gfx res stuff.  Fuck it..
  if (last_isTeletext != isTeletext || last_xoffset != xoffset) {

    //		Uint32 col = SDL_MapRGB(screen_ptr->format, 0x00, 0x00, 0x00);
    //
    //		SDL_FillRect(screen_ptr, NULL, col);
    //		SDL_UpdateRect(screen_ptr,0,0,SDL_WINDOW_WIDTH,SDL_WINDOW_HEIGHT);
    ClearVideoWindow();

    // fprintf(stderr, "*** CLEARING WHOLE SCREEN ***\n");
    // SDL_Delay(3000);
    last_isTeletext = isTeletext;
    last_xoffset = xoffset;
  }

  // Make sure we're trying to draw within a sane part of the bitmap
  if (video_output != NULL && screen_ptr != NULL) {
    SDL_Rect src, dst;

    // Render a teletext line
    if (isTeletext) {

      if (line < 0 || line > 511)
        return;

      int window_y = line;
      // Fix height for some resolutions:
      //			switch (
      // fullscreen?cfg_Fullscreen_Resolution:cfg_Windowed_Resolution) {
      switch (fullscreen_val ? cfg_Fullscreen_Resolution
                             : cfg_Windowed_Resolution) {
      case RESOLUTION_640X512:
        break;
      case RESOLUTION_640X480_S:
        // window_y = (window_y * 0.94);
        window_y = GetScaledScanline(window_y);
        break;
      case RESOLUTION_640X480_V:
        // window_y = (window_y * 0.94);
        window_y = GetScaledScanline(window_y);
        break;
      case RESOLUTION_320X240_S:
        // window_y = (window_y * 0.94);
        window_y = GetScaledScanline(window_y);
        disable_grille_for_teletext = 1;
        break;
      case RESOLUTION_320X240_V:
        // window_y = (window_y * 0.94);
        window_y = GetScaledScanline(window_y);
        disable_grille_for_teletext = 1;
        break;
      case RESOLUTION_320X256:
        disable_grille_for_teletext = 1;
        break;
      default:
        break;
      }

      //#ifdef EMULATE_CRT
      if (cfg_EmulateCrtTeletext == 0 || (line & 1) == 1 ||
          disable_grille_for_teletext == 1) {
        //#endif
        //				src.x=36; src.y=line;
        //src.w=SDL_WINDOW_WIDTH
        //-
        //(36 + 124); src.h=1; 				dst.x= (36+124) / 2;
        // dst.y=line +6; dst.w=SDL_WINDOW_WIDTH - (36 + 124); dst.h=1;

        //				src.x=0; src.y=line;
        // src.w=SDL_WINDOW_WIDTH; src.h=1; 				dst.x=0;
        // dst.y=line; dst.w=SDL_WINDOW_WIDTH; dst.h=1;
        src.x = 0;
        src.y = line;
        src.w = screen_ptr->w;
        src.h = 1;
        dst.x = 0;
        dst.y = window_y;
        dst.w = screen_ptr->w;
        dst.h = 1;

        // dst.x +=40; dst.w -=40; dst.y+=8;

        if (dst.y < screen_ptr->h) {
          SDL_BlitSurface(video_output, &src, screen_ptr, &dst);
          SDL_UpdateRect(screen_ptr, dst.x, dst.y, dst.w, dst.h);
        }
        //#ifdef EMULATE_CRT
      }
      //#endif

      // render a graphics mode line.
    } else {
      int window_y;
      window_y = (line - 32);

      // Line may have moved off the top, so check here.
      if (window_y < 0 || window_y > 255)
        return;

      //			switch (
      // fullscreen?cfg_Fullscreen_Resolution:cfg_Windowed_Resolution) {
      switch (fullscreen_val ? cfg_Fullscreen_Resolution
                             : cfg_Windowed_Resolution) {
      case RESOLUTION_640X512:
        window_y = window_y * 2;
        scan_double = 1;
        break;
      case RESOLUTION_640X480_S:
        // window_y = (window_y * 2 * 0.94);
        window_y = GetScaledScanline(window_y * 2);
        scan_double = 1;
        break;
      case RESOLUTION_640X480_V:
        window_y = window_y * 2;
        window_y -= cfg_VerticalOffset;
        scan_double = 1;
        break;
      case RESOLUTION_320X240_S:
        // window_y = ((window_y+1) * 0.94);
        window_y = GetScaledScanline(window_y + 1);
        scan_double = 0;
        break;
      case RESOLUTION_320X240_V:
        window_y -= cfg_VerticalOffset >> 1;
        scan_double = 0;
        break;
      case RESOLUTION_320X256:
        scan_double = 0;
        break;
      default:
        scan_double = 0;
        break;
      }
      // Only do this if 512.
      // window_y = window_y * 2;

      // Line may be unwanted.
      if (window_y < 0)
        return;

      src.x = 0;
      src.y = line;

      //			src.w=SDL_WINDOW_WIDTH;
      src.w = screen_ptr->w;

      //			printf("kjlfkjdflfjd\n");

      src.h = 1;

      dst.x = 0;

      dst.y = window_y;

      //			dst.w=SDL_WINDOW_WIDTH;
      dst.w = screen_ptr->w;

      dst.h = 1;

      if (dst.h < screen_ptr->h) {
        SDL_BlitSurface(video_output, &src, screen_ptr, &dst);
        SDL_UpdateRect(screen_ptr, 0, window_y, screen_ptr->w, 1);
      }

      //			printf("Line: %d %d\n", (int) dst.y, (int)
      // dst.h);

      // Graphics mode is never more than 256 scanlines so
      // double up the lines
      src = dst;
      dst.y += 1;

      //#ifndef EMULATE_CRT
      if (scan_double && (!cfg_EmulateCrtGraphics)) {
        //				printf("Doing scan double\n");
        SDL_BlitSurface(screen_ptr, &src, screen_ptr, &dst);
        SDL_UpdateRect(screen_ptr, 0, window_y + 1, screen_ptr->w, 1);
      }
      //#endif
      //			SDL_UpdateRect(screen_ptr, 0, window_y ,
      // SDL_WINDOW_WIDTH 			 , 2);
    }
  }
#endif /* 0 */
}

/*
void RenderLine(int line, int isTeletext, int xoffset)
{
        static int last_isTeletext = 1, last_xoffset = 0, last_mode_graphics =
0, last_mode_text = 0;

        if (cfg_EmulateCrtGraphics != last_mode_graphics){
                ClearVideoWindow();
                last_mode_graphics = cfg_EmulateCrtGraphics;
        }

        if (cfg_EmulateCrtTeletext != last_mode_text){
                ClearVideoWindow();
                last_mode_text = cfg_EmulateCrtTeletext;
        }

        if ( (SDL_GetAppState() & SDL_APPACTIVE) == 0)
                return;

        if (last_isTeletext != isTeletext || last_xoffset != xoffset){
                ClearVideoWindow();

                last_isTeletext = isTeletext;
                last_xoffset = xoffset;
        }

        if (video_output != NULL && screen_ptr != NULL && line>=0 && line<512){
                SDL_Rect src, dst;

                if (isTeletext){
                        if (line <0 || line > 511)
                                return;

                        if ( cfg_EmulateCrtTeletext == 0 || (line & 1) == 1){
                                src.x=0; src.y=line; src.w=SDL_WINDOW_WIDTH;
src.h=1; dst.x=0; dst.y=line; dst.w=SDL_WINDOW_WIDTH; dst.h=1;
                                SDL_BlitSurface(video_output, &src, screen_ptr,
&dst); SDL_UpdateRect(screen_ptr, dst.x, dst.y, dst.w, dst.h);
                        }

                }else{
                        int window_y;
                        window_y = (line -32);

                        if (window_y < 0 || window_y > 255)
                                return;

                        window_y = window_y * 2;

                        src.x=0; src.y=line;

                        src.w=SDL_WINDOW_WIDTH;

                        src.h=1;

                        dst.x=0;

                        dst.y=window_y;

                        dst.w=SDL_WINDOW_WIDTH;

                        dst.h=1;

                        SDL_BlitSurface(video_output, &src, screen_ptr, &dst);
//                        SDL_UpdateRect(screen_ptr, 0, window_y ,
SDL_WINDOW_WIDTH, 1); SDL_UpdateRect(screen_ptr, 0, window_y ,screen_ptr->w,1);

                        src = dst;
                        dst.y +=1;
                        if (! cfg_EmulateCrtGraphics){
                                SDL_BlitSurface(screen_ptr, &src, screen_ptr,
&dst);
                                //SDL_UpdateRect(screen_ptr, 0, window_y+1 ,
SDL_WINDOW_WIDTH, 1); SDL_UpdateRect(screen_ptr, 0, window_y+1 , screen_ptr->w,
1);
                        }
                }
        }
}
*/

void RenderFullscreenFPS(const char *str, int y) {
#if 0
  SDL_Color col = {127 + 64, 127 + 64, 127 + 64, 0};
  SDL_Rect rect = {0, 0, 7 * 10, 16};

  // Does not work in 320 width mode, will fix later.
  if (EG_Draw_GetScale() != 1)
    return;

  rect.x = 640 - rect.w;
  rect.y = y;

  SDL_FillRect(video_output, &rect,
               SDL_MapRGB(video_output->format, col.r, col.g, col.b));
  EG_Draw_String(video_output, &col, EG_FALSE, &rect, 0, (char *)str);
#endif
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

//--/* Initialize GUI. Must be called before any other EG function call.
//-- */
//--EG_BOOL EG_Initialize(void)
//--{
//--//	/* Initialize event handler:
//--//	 */
//--//	if (EG_Event_InitializeQueue() == EG_FALSE)
//--//		return(EG_FALSE);
//--//
//--//	/* Load label fonts:
//--//	 */
//--
//--	EG_Draw_FlushEventQueue();
//--
//--	if (EG_DrawString_Initialise() == EG_FALSE)
//--		return(EG_FALSE);
//--
//--
//--	return(EG_TRUE);
//--}
//--
//--void EG_Quit(void)
//--{
//--//	EG_Event_FreeQueue();
//--	EG_DrawString_Free();
//--}
//--
//--
//--/* Rendering scale (1 = 100%, 0.5 = 50%)
//-- */
//--void EG_Draw_SetToLowResolution(void)
//--{
//--        scale=0.5;
//--}
//--
//--void EG_Draw_SetToHighResolution(void)
//--{
//--        scale=1.0;
//--}
//--
//--/* Get scale (divide coordinates by this value to calculate the actual size
//-- * of anything physically drawn on the surface).
//-- */
//--float EG_Draw_GetScale(void)
//--{
//--        return(scale);
//--}
//--
//--
//--Uint32 EG_Draw_GetCurrentTime(void)
//--{
//--	return(SDL_GetTicks());
//--}
//--
//--Uint32 EG_Draw_GetTimePassed(Uint32 time_start)
//--{
//--	Uint32 time_end = SDL_GetTicks();
//--
//--        if (time_end < time_start)
//--                return(time_end + ( ((Uint32) 0xffffffff) - time_start) );
//--        else
//--                return(time_end - time_start);
//--}
//--
//--void EG_Draw_FlushEventQueue(void)
//--{
//--	SDL_Event event;
//--	while ( SDL_PollEvent(&event) ){}
//--}
//--
//--
//--/* Wrapper for SDL surface update
//-- *
//-- * Should call a user callback so program knows the GUI has updated
// something
//-- */
//--void EG_Draw_UpdateSurface(SDL_Surface *surface, Sint32 x, Sint32 y, Sint32
// w
//-- , Sint32 h)
//--{
//--	SDL_UpdateRect(surface, x, y, w, h);
//--}
//--
//--
//--
//--/* Text functions */
//--static SDL_Surface *label_low = NULL, *label_high = NULL;
//--
//--static EG_BOOL EG_DrawString_Initialise(void)
//--{	//int i;
//--	char largefont[1024];
//--        char smallfont[1024];
//--
//--        char cpath[]={DATA_DIR};
//--
//--        strcpy(largefont, cpath);
//--        if (cpath[strlen(cpath)] != '/')
//--                        strcat(largefont, "/");
//--
//--        strcpy(smallfont, largefont);
//--
//--        strcat(largefont, "resources/font10x16.bmp");
//--        strcat(smallfont, "resources/font5x8.bmp");
//--
//--	printf("'%s' '%s'\n", largefont, smallfont);
//--
//--        label_low = SDL_LoadBMP(smallfont);
//--        label_high = SDL_LoadBMP(largefont);
//--
//--        if (label_low == NULL || label_high == NULL){
//--                printf("Failed to load font..\n");
//--                return(EG_FALSE);
//--        }
//--
//--        // Set color key
//--	SDL_SetColorKey(label_low, SDL_SRCCOLORKEY
//--	 , SDL_MapRGB(label_low->format, 255,255,255));
//--	SDL_SetColorKey(label_high, SDL_SRCCOLORKEY
//--	 , SDL_MapRGB(label_high->format, 255,255,255));
//--
//--	return(EG_TRUE);
//--}
//--
//--static void EG_DrawString_Free(void)
//--{
//--        SDL_FreeSurface(label_low);
//--        SDL_FreeSurface(label_high);
//--}
//--
//--// 0 = center, -1 = left. 1 = right
//--void EG_Draw_String(SDL_Surface *surface, SDL_Color *color, EG_BOOL bold,
// SDL_Rect *area_ptr
//-- , int justify, char *string)
//--{
//--	SDL_Rect drawing_area; // SDL_DW_Draw_CalcDrawingArea(surface, &area);
//--
//--        int i, w, h, x, y, len;
//--
//--	drawing_area = EG_Draw_CalcDrawingArea(surface, area_ptr);
//--
//--	w = (int) (10 * EG_Draw_GetScale() );
//--	h = (int) (16 * EG_Draw_GetScale() );
//--
//--	drawing_area.x = (int) (drawing_area.x * EG_Draw_GetScale() );
//--	drawing_area.y = (int) (drawing_area.y * EG_Draw_GetScale() );
//--	drawing_area.w = (int) (drawing_area.w * EG_Draw_GetScale() );
//--	drawing_area.h = (int) (drawing_area.h * EG_Draw_GetScale() );
//--
//--        // Calc. number of letters we can actually draw.
//--        len = strlen(string);
//--
//--        //SDL_FillRect(surface, &drawing_area, SDL_MapRGB(surface->format,
// 255
//--        // , 255, 255));
//--
//--        x = drawing_area.x;
//--        y = drawing_area.y + ((drawing_area.h - h)/2);
//--
//--        // Don't bother rendering anything if area width is less than one
//--	// character
//--        if ( w > drawing_area.w)
//--                return;
//--
//--        // We will need to clip it:
//--        if ( (len * w) > drawing_area.w){
//--                len = (drawing_area.w / w) -1;
//--                x += (drawing_area.w % w) /2;
//--
//--                for(i=0; i<len;i++){
//--                        EG_Draw_Char(surface, color, bold, x, y, string[i]);
//--                                x += w;
//--                }
//--                EG_Draw_Char(surface, color, bold, x, y, (char) 8);
//--
//--        }else{
//--        // We don't need to clip it. But may need to center it:
//--                if (justify == 0)
//--                        x += (drawing_area.w - len * w) /2;
//--                else if (justify == 1)
//--                        x += (drawing_area.w - len * w);
//--
//--                for(i=0; i<len;i++){
//--                        EG_Draw_Char(surface, color, bold, x, y, string[i]);
//--                                x += w;
//--                }
//--        }
//--        EG_Draw_UpdateSurface(surface, drawing_area.x, drawing_area.y
//--	 , drawing_area.w, drawing_area.h);
//--}
//--
//--void EG_Draw_Char(SDL_Surface *surface, SDL_Color *color, EG_BOOL bold,
// Uint16 x, Uint16 y
//-- , char c)
//--{
//--        SDL_Rect src, dst;
//--        SDL_Surface *source_surface;
//--
//--	SDL_Color *tmp;		// Dump compiler warning
//--	tmp = color;
//--
//--	src.x = c & 15;
//--	src.y = ((unsigned char) c) >> 4;
//--
//--        if (EG_Draw_GetScale() >= 1){
//--                source_surface = label_high;
//--                src.w = 10; src.h = 16;
//--        }else{
//--                source_surface = label_low;
//--                src.w = 5; src.h = 8;
//--        }
//--        src.x *= src.w; src.y *= src.h;
//--
//--
//--        dst.x = x; dst.y = y;
//--
//--        SDL_BlitSurface(source_surface, &src, surface, &dst);
//--	if (bold == EG_TRUE){
//--		dst.x++;
//--		SDL_BlitSurface(source_surface, &src, surface, &dst);
//--	}
//--}
//--
//--/* If surface == NULL, returns an SDL_Rect of 0,0,0,0.  If update == NULL,
//-- * returns whole area of surface.  Otherwise returns update.
//-- */
//--SDL_Rect EG_Draw_CalcDrawingArea(SDL_Surface *surface, SDL_Rect *update)
//--{
//--        SDL_Rect area = {0,0,0,0};
//--
//--        if (surface == NULL)
//--                return(area);
//--
//--        if (update == NULL){
//--                area.w = surface->w;
//--                area.h = surface->h;
//--                area.x = 0;
//--                area.y = 0;
//--        }else{
//--                area = *(update);
//--        }
//--
//--        return(area);
//--}
//--
//--void EG_Draw_Box(SDL_Surface *surface, SDL_Rect *area, SDL_Color *color)
//--{
//--	SDL_Rect drawing_area = EG_Draw_CalcDrawingArea(surface, area);
//--
//--        if (surface == NULL)
//--                return;
//--
//--	drawing_area.x = (int) (drawing_area.x * EG_Draw_GetScale() );
//--	drawing_area.y = (int) (drawing_area.y * EG_Draw_GetScale() );
//--	drawing_area.w = (int) (drawing_area.w * EG_Draw_GetScale() );
//--	drawing_area.h = (int) (drawing_area.h * EG_Draw_GetScale() );
//--
//--        SDL_FillRect(surface, &drawing_area, SDL_MapRGB(surface->format
//--	 , color->r, color->g, color->b));
//--
//--	EG_Draw_UpdateSurface(surface, drawing_area.x, drawing_area.y
//--	 , drawing_area.w, drawing_area.h);
//--}
//--
//--
//--void EG_Draw_TabBorder(SDL_Surface *surface, SDL_Rect *area, SDL_Color
//*color
//-- , int type)
//--{
//--	SDL_Rect drawing_area = EG_Draw_CalcDrawingArea(surface, area);
//--        SDL_Rect line = {0,0,0,0};
//--        Uint32 bright_col = 0, dull_col = 0;
//--
//--        if (surface == NULL)
//--                return;
//--
//--        drawing_area.x = (int) (drawing_area.x * EG_Draw_GetScale() );
//--        drawing_area.y = (int) (drawing_area.y * EG_Draw_GetScale() );
//--        drawing_area.w = (int) (drawing_area.w * EG_Draw_GetScale() );
//--        drawing_area.h = (int) (drawing_area.h * EG_Draw_GetScale() );
//--
//--	switch (type){
//--
//--		case EG_Draw_Border_Normal:
//--			bright_col = SDL_MapRGB(surface->format
//--			 , (Uint8) color->r
//--			 , (Uint8) color->g
//--			 , (Uint8) color->b );
//--
//--			dull_col = bright_col;
//--		break;
//--
//--		case EG_Draw_Border_BoxHigh:
//--			bright_col = SDL_MapRGB(surface->format
//--			 , (int) (1.3333*color->r >255.0 ? 255.0
//: 1.3333*color->r)
//--			 , (int) (1.3333*color->g >255.0 ? 255.0
//: 1.3333*color->g)
//--			 , (int) (1.3333*color->b >255.0 ? 255.0
//: 1.3333*color->b) );
//--
//--			dull_col = SDL_MapRGB(surface->format
//--			 , (int) (color->r * 0.6666)
//--			 , (int) (color->g * 0.6666)
//--			 , (int) (color->b * 0.6666) );
//--		break;
//--
//--		case EG_Draw_Border_BoxLow:
//--			dull_col = SDL_MapRGB(surface->format
//--			 , (int) (1.3333*color->r >255.0 ? 255.0
//: 1.3333*color->r)
//--			 , (int) (1.3333*color->g >255.0 ? 255.0
//: 1.3333*color->g)
//--			 , (int) (1.3333*color->b >255.0 ? 255.0
//: 1.3333*color->b) );
//--
//--			bright_col = SDL_MapRGB(surface->format
//--			 , (int) (color->r * 0.6666)
//--			 , (int) (color->g * 0.6666)
//--			 , (int) (color->b * 0.6666) );
//--		break;
//--
//--		case EG_Draw_Border_Focused:
//--
//--                        dull_col = SDL_MapRGB(surface->format
//--                         , (int) (color->r * 0.4)
//--                         , (int) (color->g * 0.4)
//--                         , (int) (color->b * 0.4) );
//--
//--			bright_col = dull_col;
//--		break;
//--
//--	}
//--
//--        // Top line:
//--        line.x = drawing_area.x  +1  ; line.y = drawing_area.y;
//--	line.w = drawing_area.w  -2  ; line.h = 1;
//--        SDL_FillRect(surface, &line, bright_col);
//--
//--        // Bottom line:
//--        line.x = drawing_area.x  +1  ; line.y = drawing_area.y +
// drawing_area.h-1;
//--	line.w = drawing_area.w  -2  ;
//--	line.h = 1;
//-- //       SDL_FillRect(surface, &line, dull_col);
//--
//--        // Left line:
//--        line.x=drawing_area.x; line.y=drawing_area.y  +1  ;
// line.h=drawing_area.h  -2;
//--	line.w = 1;
//--  	line.h+=1;
//--        SDL_FillRect(surface, &line, bright_col);
//--	line.h-=1;
//--
//--        // Right line:
//--        line.x=drawing_area.x+drawing_area.w-1; line.y=drawing_area.y  +1  ;
//--	line.w=1; line.h=drawing_area.h  -2  ;
//--
//--//	line.h++;
//--        SDL_FillRect(surface, &line, dull_col);
//--//	line.h--;
//--
//--	// Update: (if I was smart, I'd only draw the actual lines..)
//--        EG_Draw_UpdateSurface(surface, drawing_area.x, drawing_area.y
//--         , drawing_area.w, drawing_area.h+2);
//--}
//--
//--
//--
//--
//--void EG_Draw_Border(SDL_Surface *surface, SDL_Rect *area, SDL_Color *color
//-- , int type)
//--{
//--	SDL_Rect drawing_area = EG_Draw_CalcDrawingArea(surface, area);
//--        SDL_Rect line = {0,0,0,0};
//--        Uint32 bright_col = 0, dull_col = 0;
//--
//--        if (surface == NULL)
//--                return;
//--
//--        drawing_area.x = (int) ( drawing_area.x * EG_Draw_GetScale() );
//--        drawing_area.y = (int) ( drawing_area.y * EG_Draw_GetScale() );
//--        drawing_area.w = (int) ( drawing_area.w * EG_Draw_GetScale() );
//--        drawing_area.h = (int) ( drawing_area.h * EG_Draw_GetScale() );
//--
//--	switch (type){
//--
//--		case EG_Draw_Border_Normal:
//--			bright_col = SDL_MapRGB(surface->format
//--			 , (Uint8) color->r
//--			 , (Uint8) color->g
//--			 , (Uint8) color->b );
//--
//--			dull_col = bright_col;
//--		break;
//--
//--		case EG_Draw_Border_BoxHigh:
//--			bright_col = SDL_MapRGB(surface->format
//--			 , (int) (1.3333*color->r >255.0 ? 255.0
//: 1.3333*color->r)
//--			 , (int) (1.3333*color->g >255.0 ? 255.0
//: 1.3333*color->g)
//--			 , (int) (1.3333*color->b >255.0 ? 255.0
//: 1.3333*color->b) );
//--
//--			dull_col = SDL_MapRGB(surface->format
//--			 , (int) (color->r * 0.6666)
//--			 , (int) (color->g * 0.6666)
//--			 , (int) (color->b * 0.6666) );
//--		break;
//--
//--		case EG_Draw_Border_BoxLow:
//--			dull_col = SDL_MapRGB(surface->format
//--			 , (int) (1.3333*color->r >255.0 ? 255.0
//: 1.3333*color->r)
//--			 , (int) (1.3333*color->g >255.0 ? 255.0
//: 1.3333*color->g)
//--			 , (int) (1.3333*color->b >255.0 ? 255.0
//: 1.3333*color->b) );
//--
//--			bright_col = SDL_MapRGB(surface->format
//--			 , (int) (color->r * 0.6666)
//--			 , (int) (color->g * 0.6666)
//--			 , (int) (color->b * 0.6666) );
//--		break;
//--
//--		case EG_Draw_Border_Focused:
//--                        dull_col = SDL_MapRGB(surface->format
//--                         , (int) (color->r * 0.7)
//--                         , (int) (color->g * 0.7)
//--                         , (int) (color->b * 0.7) );
//--
//--                        bright_col = dull_col;
//--		break;
//--
//--	}
//--
//--        // Top line:
//--        line.x = drawing_area.x  +1  ; line.y = drawing_area.y;
//--	line.w = drawing_area.w  -2  ; line.h = 1;
//--        SDL_FillRect(surface, &line, bright_col);
//--
//--        // Bottom line:
//--        line.x = drawing_area.x  +1  ; line.y = drawing_area.y +
// drawing_area.h-1;
//--	line.w = drawing_area.w  -2  ;
//--	line.h = 1;
//--        SDL_FillRect(surface, &line, dull_col);
//--
//--        // Left line:
//--        line.x=drawing_area.x; line.y=drawing_area.y  +1  ;
// line.h=drawing_area.h  -2  ;
//--	line.w = 1;
//--        SDL_FillRect(surface, &line, bright_col);
//--
//--        // Right line:
//--        line.x=drawing_area.x+drawing_area.w-1; line.y=drawing_area.y  +1  ;
//--	line.w=1; line.h=drawing_area.h  -2  ;
//--        SDL_FillRect(surface, &line, dull_col);
//--
//--	// Update: (if I was smart, I'd only draw the actual lines..)
//--        EG_Draw_UpdateSurface(surface, drawing_area.x, drawing_area.y
//--         , drawing_area.w, drawing_area.h);
//--}
//--
//--Uint32 EG_Draw_CalcTimePassed(Uint32 starttime, Uint32 endtime)
//--{
//--        if (starttime > endtime)
//--                return(0xffffffff - starttime + endtime);
//--        else
//--                return(endtime - starttime);
//--}
//--
