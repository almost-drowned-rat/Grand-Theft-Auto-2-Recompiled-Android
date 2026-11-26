/*
 * GTA 2 recomp - the MIXTAPE radio station: the player's own music (M4c,
 * maintainer 2026-11-26).
 *
 * Music: every folder below media/ that holds music is one album (winrecomp
 * media_scan: MP3 / FLAC / WAV / Ogg, .m3u order). SOUND options: SOURCE =
 * ALL ALBUMS or one album, PLAYBACK MODE = IN ORDER / SHUFFLE
 * (saves/settings.ini mixtape_source = all | <album path>, mixtape_mode =
 * order | shuffle).
 * Like the game's own stations it plays on while nobody listens: leaving it
 * keeps the song and its position with the game tick; coming back skips the
 * game time since then (song lengths from the files), so it is somewhere
 * later, maybe in another song. Game time, not wall time: the pause stops it.
 * The radio hook (which station, when it is audible) is gta2_mixtape_listen.
 */
#include "wr.h"
#include "audio/wr_audio.h"
#include "media/media_decode.h"
#include "media/media_playlist.h"
#include "media/media_scan.h"
#include "gta2_mixtape.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static MediaLibrary g_lib;
static MediaPlaylist g_pl;
static int g_ready, g_have;
static uint32_t *g_len_ms;          /* per playlist entry, 0 = not probed yet */

/* the virtual play head: song g_pl.pos at g_pos_ms when the game tick was g_at_tick */
static uint32_t g_pos_ms, g_at_tick;
static int g_listening;
static MediaDecoder *g_dec;         /* the song playing (audio thread reads it) */
static uint64_t g_dec_frames;       /* frames played of it */

static uint32_t g_clock;            /* radio ticks: the station's time */
static int g_held;                  /* paused while listening: the song plays on silently */

static const char *song_path(int i)
{
    const MediaSong *s = &g_pl.items[i];
    return g_lib.albums[s->album].tracks[s->track].path;
}

static uint32_t song_len(int i)
{
    if (!g_len_ms[i]) {
        uint32_t l = media_probe_length_ms(song_path(i));
        g_len_ms[i] = l ? l : 180000;   /* unknown (an MP3 without a header): 3 minutes */
    }
    return g_len_ms[i];
}

static void build(void)
{
    media_playlist_free(&g_pl);
    free(g_len_ms);
    g_len_ms = NULL;
    g_have = 0;
    if (!g_lib.nalbums)
        return;
    const char *src = wr_settings_get("mixtape_source", "all");
    int shuffle = !strcmp(wr_settings_get("mixtape_mode", "order"), "shuffle");
    int sel = strcmp(src, "all") ? media_find_album(&g_lib, src) : -1;
    MediaPlayMode m = sel >= 0 ? (shuffle ? MEDIA_ALBUM_SHUFFLE : MEDIA_ALBUM_ORDER)
                               : (shuffle ? MEDIA_CUSTOM_SHUFFLE : MEDIA_CUSTOM_ORDER);
    if (media_playlist_build(&g_pl, &g_lib, 0, m, sel, (uint32_t)wr_time_ns()) <= 0 || !g_pl.n)
        return;
    g_len_ms = calloc((size_t)g_pl.n, sizeof *g_len_ms);
    g_have = g_len_ms != NULL;
    g_pos_ms = 0;
    g_at_tick = g_clock;
}

void gta2_mixtape_init(void)
{
    if (g_ready)
        return;
    g_ready = 1;
    char dir[1100];
    snprintf(dir, sizeof dir, "%s/media", g_wr_paths.root);
    media_scan(dir, &g_lib);
    wr_log("mixtape: %d album(s) in %s", g_lib.nalbums, dir);
    build();
}

int gta2_mixtape_available(void) { return g_have; }
int gta2_mixtape_album_count(void) { return g_lib.nalbums; }
const char *gta2_mixtape_album_name(int i) { return i >= 0 && i < g_lib.nalbums ? g_lib.albums[i].name : ""; }
const char *gta2_mixtape_album_key(int i) { return i >= 0 && i < g_lib.nalbums ? g_lib.albums[i].rel : ""; }

/* ---- the audio thread's side ---- */
static long feed(int16_t *out, long frames)
{
    long got = 0;
    while (got < frames && g_dec) {
        long n = media_decoder_read(g_dec, out + 2 * got, frames - got);
        if (n > 0) {
            got += n;
            g_dec_frames += (uint64_t)n;
            continue;
        }
        /* the song is over: the next one (opening is quick; no seek here) */
        media_decoder_close(g_dec);
        g_dec = NULL;
        media_playlist_next(&g_pl);
        g_dec_frames = 0;
        for (int tries = 0; tries < g_pl.n && !g_dec; tries++) {
            g_dec = media_decoder_open(song_path(g_pl.pos));
            if (!g_dec)
                media_playlist_next(&g_pl);
        }
    }
    return got;
}

/* the play head now: the song and ms into it, after the game time since the last mark */
static void advance_to_now(void)
{
    uint32_t t = g_clock;
    uint64_t ms = (uint64_t)g_pos_ms + (uint64_t)(t - g_at_tick) * 33u;
    for (int guard = 0; guard < 10000 && ms >= song_len(g_pl.pos); guard++) {
        ms -= song_len(g_pl.pos);
        media_playlist_next(&g_pl);
    }
    g_pos_ms = (uint32_t)ms;
    g_at_tick = t;
}

/* Audible or not: the radio is on MIXTAPE in a car (and the game is not paused). */
void gta2_mixtape_listen(int on)
{
    on = on && g_have;
    if (on == g_listening)
        return;
    if (!on)
        g_held = 0;
    g_listening = on;
    if (on) {
        advance_to_now();
        MediaDecoder *d = NULL;
        for (int tries = 0; tries < g_pl.n && !d; tries++) {
            d = media_decoder_open(song_path(g_pl.pos));
            if (!d) {
                media_playlist_next(&g_pl);
                g_pos_ms = 0;
            }
        }
        if (d && g_pos_ms)
            media_decoder_seek_ms(d, g_pos_ms);   /* off the audio thread */
        wr_audio_music(NULL);
        g_dec = d;
        g_dec_frames = (uint64_t)g_pos_ms * 44100u / 1000u;
        wr_audio_music(feed);
        wr_log("mixtape: on, %s at %u s", d ? song_path(g_pl.pos) : "-", g_pos_ms / 1000);
    } else {
        wr_audio_music(NULL);   /* the mixer lets go of the decoder */
        g_pos_ms = (uint32_t)(g_dec_frames * 1000u / 44100u);
        g_at_tick = g_clock;
        media_decoder_close(g_dec);
        g_dec = NULL;
        wr_log("mixtape: off at song %d, %u s", g_pl.pos, g_pos_ms / 1000);
    }
}

void gta2_mixtape_volume(float v) { wr_audio_music_volume(v); }

/* the pause while MIXTAPE plays: the song plays on, silent (maintainer 2026-11-26) */
static void hold(int on)
{
    if (on == g_held || !g_listening)
        return;
    g_held = on;
    if (on)
        wr_audio_music_volume(0);
}

/* SOURCE or PLAYBACK MODE changed: a new list (from its start) */
void gta2_mixtape_rebuild(void)
{
    int was = g_listening;
    gta2_mixtape_listen(0);
    build();
    gta2_mixtape_listen(was);
}

/* the song's name for the HUD line, "" when none */
const char *gta2_mixtape_song(void)
{
    if (!g_have)
        return "";
    const MediaSong *s = &g_pl.items[g_pl.pos];
    return g_lib.albums[s->album].tracks[s->track].name;
}

/* ==== the radio cycle (docs/re/radio.md section 7) ====================================
 * mgr = 0x5DCBC8: +0x54F7 the station slot (0..4, 0x65 police scanner, 0x66 off),
 * +0x5504 the change request (1 next, 2 previous), +0x5506 static ticks after a change,
 * +0x54F6 signal, +0x54FC full flag, +0x54F4 / +0x54F5 in a radio car last / this tick,
 * +0x25 CDVol, +0x01 paused. MIXTAPE sits after the last station: 1 5 7 6 2 MIXTAPE 1 ...
 * (Downtown). While it plays the game station stays "on" but muted, so its play head
 * and the car's remembered station stay right. */
#define MGR 0x005DCBC8u
static int g_mix_on, g_key_dir, g_audible;
static uint32_t g_station;
/* GTA2_RADIO_TEST=1 (maintainer 2026-11-26): Downtown's stations 1 5 7 6 play the files of 8 9 10 11, so the
 * names of the Residential / Industrial gang stations can be told by ear at once; the label shows the file. */
static int radio_test(void)
{
    static int t = -1;
    if (t < 0) {
        const char *e = getenv("GTA2_RADIO_TEST");
        t = e && *e == '1';
    }
    return t;
}

static int test_map(uint32_t n) { return n == 1 ? 8 : n == 5 ? 9 : n == 7 ? 10 : n == 6 ? 11 : (int)n; }

static const char *radio_test_map(const char *rel)
{
    /* data/gtaudio/<n>.wav or <n>a.wav, any case */
    const char *f = strrchr(rel, '/');
    f = f ? f + 1 : rel;
    if (strncasecmp(rel, "data/gtaudio/", 13) != 0)
        return NULL;
    char *end;
    long n = strtol(f, &end, 10);
    if (end == f || test_map((uint32_t)n) == n)
        return NULL;
    static char out[64];
    snprintf(out, sizeof out, "data/GTAudio/%d%s", test_map((uint32_t)n), end);
    return out;
}

void gta2_radio_test_init(void)
{
    if (radio_test()) {
        g_wr_read_map = radio_test_map;
        wr_log("radio test: Downtown 1 5 7 6 play 8 9 10 11");
    }
}

uint32_t gta2_radio_station(void) { return radio_test() ? (uint32_t)test_map(g_station) : g_station; }   /* 1..11, 0 none */
static uint32_t g_mix_car;

int gta2_mixtape_on(void) { return g_mix_on; }
int gta2_mixtape_audible(void) { return g_audible; }

/* after the key handler (sub_004B2CD0): a request made by a key, not by auto seek */
void gta2_radio_after_key(void)
{
    uint8_t r = wr_rd8(MGR + 0x5504);
    if (r)
        g_key_dir = r;
}

/* around the station change (sub_004B2420); gen runs it. Returns nothing: it decides itself. */
void gta2_radio_cycle(X86Ctx *ctx, void (*gen)(X86Ctx *, uint8_t *), uint8_t *base)
{
    uint32_t m = MGR;
    uint8_t dir = wr_rd8(m + 0x5504), old = wr_rd8(m + 0x54F7);
    int key = g_key_dir != 0;
    g_key_dir = 0;
    if (g_mix_on) {
        if (!key || !g_have) {
            if (!g_have)
                g_mix_on = 0;
            gen(ctx, base);   /* auto seek under MIXTAPE: the game re-tunes its muted station */
            return;
        }
        g_mix_on = 0;
        wr_wr8(m + 0x54F7, dir == 1 ? 4 : 0);   /* next: the first station (from slot 4), previous: the last */
        uint8_t keep = wr_rd8(m + 0x5506);
        wr_wr8(m + 0x5506, 0);
        gen(ctx, base);
        if (!wr_rd8(m + 0x5506)) {   /* nothing found */
            wr_wr8(m + 0x54F7, old);
            wr_wr8(m + 0x5506, keep);
        }
        return;
    }
    if (!key || old >= 5 || !g_have) {
        gen(ctx, base);
        return;
    }
    uint8_t s6 = wr_rd8(m + 0x54F6);
    uint32_t sfc = wr_rd32(m + 0x54FC);
    wr_wr8(m + 0x5506, 0);
    gen(ctx, base);
    uint8_t now = wr_rd8(m + 0x54F7);
    int found = wr_rd8(m + 0x5506) != 0;
    int wrapped = !found || (dir == 1 ? now <= old : now >= old);
    if (wrapped) {   /* past the last (or before the first) station: MIXTAPE */
        wr_wr8(m + 0x54F7, old);
        wr_wr8(m + 0x54F6, s6);
        wr_wr32(m + 0x54FC, sfc);
        wr_wr8(m + 0x5506, 0);
        g_mix_on = 1;
        wr_log("radio: MIXTAPE (after station slot %u)", old);
    }
}

/* before the volume / static step (sub_004B1E40): no static while MIXTAPE plays */
void gta2_radio_before_volume(void)
{
    if (g_audible) {
        wr_wr8(MGR + 0x5506, 0);
        wr_wr8(MGR + 0x54F6, 0x7F);
    }
}

/* after the radio tick (sub_004B2D50): is MIXTAPE audible now; a car keeps it */
void gta2_radio_after_tick(void)
{
    uint32_t m = MGR;
    uint8_t was = wr_rd8(m + 0x54F4), in = wr_rd8(m + 0x54F5);
    uint32_t car = wr_rd32(0x0066C274u);
    if (was == 1 && in != 1)   /* left the car: it remembers MIXTAPE */
        g_mix_car = g_mix_on ? car : (car == g_mix_car ? 0 : g_mix_car);
    if (was != 1 && in == 1)   /* got in: MIXTAPE again only in that car */
        g_mix_on = g_have && car && car == g_mix_car;
    int paused = wr_rd8(m + 0x01) != 0;
    int tuned = g_mix_on && in == 1 && wr_rd8(m + 0x54F7) < 5 && wr_rd8(0x0066BEECu);
    g_clock++;   /* the station's time runs in the pause too */
    if (tuned && paused && g_listening) {   /* pause: the song plays on silently, the game station stays silent */
        hold(1);
        g_audible = 1;
        return;
    }
    hold(0);
    int listen = tuned && !paused;
    {   /* the game station now (1..11), 0 = none / MIXTAPE: for its info label */
        uint8_t slot = wr_rd8(m + 0x54F7);
        g_station = !g_mix_on && in == 1 && slot < 5 ? wr_rd32(m + 0x5468 + slot * 0x1Cu + 0x0C) : 0;
    }
    if (listen)
        gta2_mixtape_volume((float)wr_rd8(m + 0x25) / 127.0f);
    gta2_mixtape_listen(listen);
    g_audible = listen;
}

/* the radio stopped (sub_004B1D80: level end, frontend) */
void gta2_radio_stopped(void)
{
    g_mix_on = 0;
    g_audible = 0;
    g_mix_car = 0;
    gta2_mixtape_listen(0);
}
