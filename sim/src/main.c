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
 * the same sim time always reproduces the exact same state.
 */
#include "convoy_geo.h"
#include "convoy_proto.h"
#include "frame_dump.h"
#include "neighbor_table.h"
#include "radar_scene.h"
#include "scenario.h"

#include <SDL2/SDL.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define SIM_TICK_MS 200u /* 5 Hz, docs/06 */
#define MAX_EVENTS 4096

/* ---- tiny deterministic PRNG (xorshift32) --------------------------- */

typedef struct {
    uint32_t s;
} prng_t;

static void prng_seed(prng_t *p, uint32_t seed)
{
    p->s = (seed != 0) ? seed : 1u;
}

static uint32_t prng_next(prng_t *p)
{
    uint32_t x = p->s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    p->s = x;
    return x;
}

/* True with probability `p` (0..1). */
static bool prng_chance(prng_t *p, float prob)
{
    if (prob <= 0.0f) {
        return false;
    }
    if (prob >= 1.0f) {
        return true;
    }
    return ((float)(prng_next(p) & 0xFFFFFFu) / (float)0xFFFFFFu) < prob;
}

/* ---- CLI -------------------------------------------------------------- */

typedef struct {
    const char *scenario_path;
    int pov;
    float speed;
    float range_m;
    float loss;
    const char *dump_dir;
    bool headless;
    uint32_t seed;
} cli_args_t;

static void print_usage(const char *prog)
{
    fprintf(stderr,
           "usage: %s <scenario.csv> [--pov N] [--speed X] [--range M]\n"
           "       [--loss P] [--dump DIR] [--headless] [--seed S]\n",
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

/* ---- beacon replay ----------------------------------------------------- */

typedef struct {
    uint32_t t_ms;
    uint8_t uid;
} beacon_event_t;

static int collect_events(const scenario_t *scn, uint32_t up_to_ms,
                          beacon_event_t *out, int max_out)
{
    int n = 0;
    for (int u = 0; u < SCN_MAX_UNITS; u++) {
        const scn_track_t *tr = &scn->units[u];
        if (!tr->used) {
            continue;
        }
        uint32_t birth = tr->wp[0].t_ms;
        for (uint32_t t = birth; t <= up_to_ms && n < max_out;
            t += CL_BEACON_PERIOD_MS) {
            out[n].t_ms = t;
            out[n].uid = (uint8_t)u;
            n++;
        }
    }
    /* stable sort by (t_ms, uid); n is small (a few hundred at most) */
    for (int i = 1; i < n; i++) {
        beacon_event_t cur = out[i];
        int j = i - 1;
        while (j >= 0 && (out[j].t_ms > cur.t_ms ||
                          (out[j].t_ms == cur.t_ms && out[j].uid > cur.uid))) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = cur;
    }
    return n;
}

static uint16_t course_to_cdeg(float speed_mps, float course_deg)
{
    if (speed_mps < 1.5f) {
        return CL_COURSE_INVALID; /* docs/05 own-course rule */
    }
    return (uint16_t)((int)(course_deg * 100.0f + 0.5f) % 36000);
}

/* Rebuilds every unit's neighbour table from t=0 up to sim_time_ms. */
static void replay_to(const scenario_t *scn, uint32_t sim_time_ms,
                      uint32_t seed, float range_m, float loss_p,
                      nt_t nt_out[SCN_MAX_UNITS])
{
    for (int i = 0; i < SCN_MAX_UNITS; i++) {
        nt_init(&nt_out[i], (uint8_t)i);
    }

    static beacon_event_t events[MAX_EVENTS];
    int n = collect_events(scn, sim_time_ms, events, MAX_EVENTS);

    uint16_t seq[SCN_MAX_UNITS] = {0};
    prng_t prng;
    prng_seed(&prng, seed);

    for (int e = 0; e < n; e++) {
        uint8_t x = events[e].uid;
        uint32_t t = events[e].t_ms;

        scn_sample_t sx;
        scenario_sample(scn, x, t, &sx);
        if (!sx.present) {
            continue;
        }

        cl_beacon_t beacon;
        cl_make_beacon(&beacon, x, seq[x]++, scn->units[x].initials, sx.lat_e7,
                       sx.lon_e7, (uint16_t)(sx.speed_mps * 10.0f + 0.5f),
                       course_to_cdeg(sx.speed_mps, sx.course_deg), 3, 9, 0);

        for (int r = 0; r < SCN_MAX_UNITS; r++) {
            if (r == x) {
                continue;
            }
            scn_sample_t sr;
            scenario_sample(scn, (uint8_t)r, t, &sr);
            if (!sr.present) {
                continue;
            }

            float dist_xr =
                geo_dist_m(sx.lat_e7, sx.lon_e7, sr.lat_e7, sr.lon_e7);
            bool delivered = false;
            if (dist_xr <= range_m && !prng_chance(&prng, loss_p)) {
                nt_update_from_beacon(&nt_out[r], &beacon, t);
                delivered = true;
            }
            if (delivered) {
                continue;
            }

            /* single-hop relay: any third present unit in range of both */
            bool relay_ok = false;
            for (int y = 0; y < SCN_MAX_UNITS && !relay_ok; y++) {
                if (y == x || y == r) {
                    continue;
                }
                scn_sample_t sy;
                scenario_sample(scn, (uint8_t)y, t, &sy);
                if (!sy.present) {
                    continue;
                }
                float d_xy =
                    geo_dist_m(sx.lat_e7, sx.lon_e7, sy.lat_e7, sy.lon_e7);
                float d_yr =
                    geo_dist_m(sy.lat_e7, sy.lon_e7, sr.lat_e7, sr.lon_e7);
                relay_ok = (d_xy <= range_m && d_yr <= range_m);
            }
            if (relay_ok && !prng_chance(&prng, loss_p)) {
                cl_beacon_t relay = beacon;
                cl_beacon_to_relay(&relay);
                nt_update_from_beacon(&nt_out[r], &relay, t);
            }
        }
    }
}

/* ---- scene building ----------------------------------------------------- */

static void build_scene(const scenario_t *scn, nt_t nt[SCN_MAX_UNITS],
                        uint8_t pov, uint32_t sim_time_ms,
                        rr_zoom_t zoom_mode, rr_scene_t *sc)
{
    memset(sc, 0, sizeof(*sc));
    sc->provisioned = true;
    sc->self_uid = pov;
    if (scn->units[pov].used) {
        sc->self_initials[0] = scn->units[pov].initials[0];
        sc->self_initials[1] = scn->units[pov].initials[1];
    } else {
        sc->self_initials[0] = '?';
        sc->self_initials[1] = '?';
    }

    scn_sample_t own;
    scenario_sample(scn, pov, sim_time_ms, &own);
    sc->own_fix = own.present;
    sc->own_course_cdeg = CL_COURSE_INVALID;
    if (own.present) {
        sc->own_lat_e7 = own.lat_e7;
        sc->own_lon_e7 = own.lon_e7;
        sc->own_sats = 9;
        sc->own_course_cdeg = course_to_cdeg(own.speed_mps, own.course_deg);
    }

    sc->now_ms = sim_time_ms;
    sc->rx_talker_uid = -1;

    sc->n_neighbors = nt_snapshot(&nt[pov], sc->neighbors, sim_time_ms);

    sc->zoom_mode = zoom_mode;
    if (zoom_mode == RR_ZOOM_AUTO) {
        float max_dist = 0.0f;
        if (sc->own_fix) {
            for (int i = 0; i < sc->n_neighbors; i++) {
                nt_tier_t tier = nt_tier(&sc->neighbors[i], sim_time_ms);
                if ((tier == NT_LIVE || tier == NT_STALE) &&
                    sc->neighbors[i].last.fix_quality != 0) {
                    float d = geo_dist_m(sc->own_lat_e7, sc->own_lon_e7,
                                        sc->neighbors[i].last.lat_e7,
                                        sc->neighbors[i].last.lon_e7);
                    if (d > max_dist) {
                        max_dist = d;
                    }
                }
            }
        }
        sc->zoom_scale_m = rr_pick_zoom(max_dist);
    } else {
        static const uint16_t fixed_scale[] = {0, 250, 500, 1000, 2000, 4000};
        sc->zoom_scale_m = fixed_scale[zoom_mode];
    }
}

static void rgb565_to_rgb888(const uint16_t *px, uint8_t *out, int n)
{
    for (int i = 0; i < n; i++) {
        uint16_t v = px[i];
        uint8_t r5 = (uint8_t)((v >> 11) & 0x1Fu);
        uint8_t g6 = (uint8_t)((v >> 5) & 0x3Fu);
        uint8_t b5 = (uint8_t)(v & 0x1Fu);
        out[i * 3 + 0] = (uint8_t)((r5 * 255 + 15) / 31);
        out[i * 3 + 1] = (uint8_t)((g6 * 255 + 31) / 63);
        out[i * 3 + 2] = (uint8_t)((b5 * 255 + 15) / 31);
    }
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
