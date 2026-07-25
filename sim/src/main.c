/* main.c — ConvoyLink desktop radar simulator (docs/07 §Simulator, T07).
 *
 * Runs the real convoy_geo / neighbor_table / radar_render pipeline
 * against scripted GPS tracks, rendering through the exact same
 * rr_screen_draw() the firmware calls.
 *
 * Beacon delivery model: rather than incrementally stepping a live
 * neighbor_table forward in time (which cannot be cleanly "undone" for
 * the ←/→ scrub keys), every query fully REPLAYS every beacon event from
 * t=0 up to the requested sim time into fresh neighbor_table instances.
 * Scenarios are short (minutes) and beacon events sparse (5 s/unit), so
 * this is computationally trivial and makes seeking trivially correct -
 * the same sim time always reproduces the exact same state. The replay
 * and scene-building logic itself lives in sim_core.c, shared with
 * checks.c's scripted --check assertions (T08).
 */
#include "checks.h"
#include "frame_dump.h"
#include "scenario.h"
#include "sim_core.h"

#include <SDL2/SDL.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define SIM_TICK_MS 200u /* 5 Hz, docs/06 */

/* ---- CLI -------------------------------------------------------------- */

typedef struct {
    const char *scenario_path;
    int pov;
    float speed;
    float range_m;
    float loss;
    const char *dump_dir;
    bool headless;
    bool check;
    uint32_t seed;
} cli_args_t;

static void print_usage(const char *prog)
{
    fprintf(stderr,
           "usage: %s <scenario.csv> [--pov N] [--speed X] [--range M]\n"
           "       [--loss P] [--dump DIR] [--headless] [--check] [--seed S]\n",
           prog);
}

static int parse_args(int argc, char **argv, cli_args_t *a)
{
    a->scenario_path = NULL;
    a->pov = 0;
    a->speed = 1.0f;
    a->range_m = 800.0f;
    a->loss = 0.0f;
    a->dump_dir = NULL;
    a->headless = false;
    a->check = false;
    a->seed = 1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--pov") == 0 && i + 1 < argc) {
            a->pov = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--speed") == 0 && i + 1 < argc) {
            a->speed = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--range") == 0 && i + 1 < argc) {
            a->range_m = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--loss") == 0 && i + 1 < argc) {
            a->loss = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--dump") == 0 && i + 1 < argc) {
            a->dump_dir = argv[++i];
        } else if (strcmp(argv[i], "--headless") == 0) {
            a->headless = true;
        } else if (strcmp(argv[i], "--check") == 0) {
            a->check = true;
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            a->seed = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (argv[i][0] != '-' && a->scenario_path == NULL) {
            a->scenario_path = argv[i];
        } else {
            fprintf(stderr, "unknown or malformed arg: %s\n", argv[i]);
            return -1;
        }
    }
    if (a->scenario_path == NULL) {
        print_usage(argv[0]);
        return -1;
    }
    if (a->pov < 0 || a->pov >= SCN_MAX_UNITS) {
        fprintf(stderr, "--pov must be 0..%d\n", SCN_MAX_UNITS - 1);
        return -1;
    }
    return 0;
}

/* ---- main --------------------------------------------------------------- */

int main(int argc, char **argv)
{
    cli_args_t args;
    if (parse_args(argc, argv, &args) != 0) {
        return 1;
    }

    scenario_t scn;
    if (scenario_load(args.scenario_path, &scn) != 0) {
        return 1;
    }

    if (args.check) {
        return checks_run(args.scenario_path, &scn, args.seed, args.range_m,
                          args.loss) == 0 ? 0 : 1;
    }

    bool headless = args.headless;
    const char *sdl_driver_env = getenv("SDL_VIDEODRIVER");
    if (sdl_driver_env != NULL && strcmp(sdl_driver_env, "dummy") == 0) {
        headless = true;
    }

    if (args.dump_dir != NULL) {
        if (mkdir(args.dump_dir, 0755) != 0 && errno != EEXIST) {
            fprintf(stderr, "convoysim: cannot create dump dir %s: %s\n",
                   args.dump_dir, strerror(errno));
            return 1;
        }
    }

    int pov = args.pov;
    rr_zoom_t zoom_mode = RR_ZOOM_AUTO;
    uint32_t sim_time_ms = 0;
    int frame_no = 0;

    static uint16_t px565[RR_W * RR_H];
    static uint8_t px888[RR_W * RR_H * 3];

    if (headless) {
        for (;;) {
            nt_t nt[SCN_MAX_UNITS];
            replay_to(&scn, sim_time_ms, args.seed, args.range_m, args.loss,
                     nt);

            rr_scene_t sc;
            build_scene(&scn, nt, (uint8_t)pov, sim_time_ms, zoom_mode, &sc);

            rr_fb_t fb = {px565, 0, RR_H};
            rr_screen_draw(&fb, &sc);

            if (args.dump_dir != NULL) {
                rgb565_to_rgb888(px565, px888, RR_W * RR_H);
                char path[600];
                snprintf(path, sizeof path, "%s/frame_%04d.bmp",
                        args.dump_dir, frame_no);
                if (bmp_write(path, RR_W, RR_H, px888) != 0) {
                    fprintf(stderr, "convoysim: failed to write %s\n", path);
                    return 1;
                }
            }
            frame_no++;

            if (sim_time_ms >= scn.duration_ms) {
                break;
            }
            sim_time_ms += SIM_TICK_MS;
            if (sim_time_ms > scn.duration_ms) {
                sim_time_ms = scn.duration_ms;
            }
        }
        printf("convoysim: %d frames simulated (%s)\n", frame_no,
              args.dump_dir != NULL ? args.dump_dir : "no dump");
        return 0;
    }

    /* windowed/interactive mode */
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *win =
        SDL_CreateWindow("ConvoyLink sim", SDL_WINDOWPOS_CENTERED,
                         SDL_WINDOWPOS_CENTERED, RR_W * 2, RR_H * 2, 0);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    SDL_Texture *tex = SDL_CreateTexture(
        ren, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, RR_W, RR_H);

    bool running = true;
    bool paused = false;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = false;
            } else if (ev.type == SDL_KEYDOWN) {
                SDL_Keycode k = ev.key.keysym.sym;
                if (k >= SDLK_0 && k <= SDLK_4) {
                    pov = k - SDLK_0;
                } else if (k == SDLK_z) {
                    zoom_mode = (rr_zoom_t)((zoom_mode + 1) % 6);
                } else if (k == SDLK_SPACE) {
                    paused = !paused;
                } else if (k == SDLK_LEFT) {
                    sim_time_ms = (sim_time_ms > 5000u) ? sim_time_ms - 5000u : 0u;
                } else if (k == SDLK_RIGHT) {
                    sim_time_ms += 5000u;
                    if (sim_time_ms > scn.duration_ms) {
                        sim_time_ms = scn.duration_ms;
                    }
                } else if (k == SDLK_q) {
                    running = false;
                }
            }
        }
        if (!running) {
            break;
        }

        nt_t nt[SCN_MAX_UNITS];
        replay_to(&scn, sim_time_ms, args.seed, args.range_m, args.loss, nt);
        rr_scene_t sc;
        build_scene(&scn, nt, (uint8_t)pov, sim_time_ms, zoom_mode, &sc);
        rr_fb_t fb = {px565, 0, RR_H};
        rr_screen_draw(&fb, &sc);
        rgb565_to_rgb888(px565, px888, RR_W * RR_H);

        SDL_UpdateTexture(tex, NULL, px888, RR_W * 3);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);

        if (!paused && sim_time_ms < scn.duration_ms) {
            uint32_t step = (uint32_t)((float)SIM_TICK_MS * args.speed);
            if (step < 1u) {
                step = 1u;
            }
            sim_time_ms += step;
            if (sim_time_ms > scn.duration_ms) {
                sim_time_ms = scn.duration_ms;
            }
        }
        SDL_Delay(SIM_TICK_MS);
    }

    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
