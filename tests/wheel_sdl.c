/* Linux-only DOSBox-X test adapter: real SDL wheel events and screen capture.
 * No guest interrupt handler is replaced. Wheel-to-key mapping must be off. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

typedef struct { uint32_t flags; void *format; int w, h, pitch; void *pixels; } Surface;
static double began;
static unsigned frame, stage;
static uint64_t previous;
static double now(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}
static double elapsed(void) { if (!began) began = now(); return now() - began; }
int SDL_PollEvent(void *event) {
    int (*real)(void *) = dlsym(RTLD_NEXT, "SDL_PollEvent");
    double t = elapsed();
    if (stage < 4 && t > 6.0 + stage * 2.0) {
        /* SDL2's ABI: event union is 56 bytes; wheel y is at offset 20.
         * The final event is Escape (scancode 41 / keycode 27), not SDL_QUIT,
         * so MicroWeb restores text mode and DOSBox's autoexec exits normally. */
        uint32_t *p = event; memset(event, 0, 56);
        if (stage < 3) { p[0] = 0x403; p[5] = stage == 0 ? -2 : 1; }
        else { p[0] = 0x300; p[4] = 41; p[5] = 27; }
        fprintf(stderr, "WHEELTEST stage=%u t=%.3f\n", stage, t);
        ++stage; return 1;
    }
    return real(event);
}
static void capture(void *window) {
    Surface *(*get)(void *) = dlsym(RTLD_NEXT, "SDL_GetWindowSurface");
    void *(*rw)(const char *, const char *) = dlsym(RTLD_NEXT, "SDL_RWFromFile");
    int (*save)(void *, void *, int) = dlsym(RTLD_NEXT, "SDL_SaveBMP_RW");
    Surface *surface = get(window);
    if (!surface || !surface->pixels) return;
    uint64_t hash = 1469598103934665603ULL;
    unsigned char *bytes = surface->pixels;
    for (int i = 0; i < surface->pitch * surface->h; ++i) hash = (hash ^ bytes[i]) * 1099511628211ULL;
    if (hash == previous) return;
    previous = hash;
    const char *directory = getenv("MW_CAPTURE_DIR"); if (!directory) return;
    char path[1024];
    snprintf(path, sizeof(path), "%s/frame-%04u-%09.3f.bmp", directory, frame++, elapsed());
    void *file = rw(path, "wb"); if (file) save(surface, file, 1);
}
int SDL_UpdateWindowSurface(void *window) {
    int (*real)(void *) = dlsym(RTLD_NEXT, "SDL_UpdateWindowSurface");
    int result = real(window); capture(window); return result;
}
int SDL_UpdateWindowSurfaceRects(void *window, const void *rects, int count) {
    int (*real)(void *, const void *, int) = dlsym(RTLD_NEXT, "SDL_UpdateWindowSurfaceRects");
    int result = real(window, rects, count); capture(window); return result;
}
