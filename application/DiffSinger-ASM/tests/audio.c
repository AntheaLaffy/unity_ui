#define _POSIX_C_SOURCE 200809L
#include "audio.h"
#include <assert.h>
#include <math.h>
#include <sndfile.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
void dse_paint_note(AsmUiCanvas* canvas, const DseNote* note, const DseRect* rect) {
    (void)canvas;
    (void)note;
    (void)rect;
}

static void near(float actual, float expected, float tolerance) {
    if (fabsf(actual - expected) > tolerance) {
        fprintf(stderr, "Audio sample %.9g != %.9g (tolerance %.9g)\n", actual, expected,
                tolerance);
        abort();
    }
}
static void kernel(void) {
    float src[20], dst[20];
    for (unsigned i = 0; i < 20; i++) {
        src[i] = 1;
        dst[i] = 10;
    }
    const float ramp[] = {0, .25f, .5f, .75f, .75f, .5f, .25f, 0};
    dse_mix_stereo(dst, src, 8, 0, 8, 4, 4, 2, -3);
    for (unsigned i = 0; i < 8; i++) {
        near(dst[i * 2], 10 + ramp[i] * 2, 0);
        near(dst[i * 2 + 1], 10 - ramp[i] * 3, 0);
    }
    for (unsigned i = 16; i < 20; i++)
        assert(dst[i] == 10);
    memset(dst, 0, sizeof(dst));
    dse_mix_stereo(dst, src, 2, 1, 4, 8, 8, 1, 1);
    near(dst[0], 1.f / 32, 0);
    near(dst[2], 1.f / 32, 0);
    dse_mix_stereo(NULL, NULL, 0, 0, 0, 0, 0, 1, 1);
}
static void make_file(const char* path, int rate, int channels, int format, int seconds) {
    SF_INFO info = {.samplerate = rate, .channels = channels, .format = format};
    SNDFILE* f = sf_open(path, SFM_WRITE, &info);
    assert(f);
    float* pcm = malloc((size_t)rate * seconds * channels * sizeof(float));
    assert(pcm);
    for (int i = 0; i < rate * seconds; i++)
        if (channels == 1)
            pcm[i] = (float)(.25 * sin(2 * 3.14159265358979323846 * 1000 * i / rate));
        else {
            pcm[i * 2] = .25f;
            pcm[i * 2 + 1] = -.5f;
        }
    assert(sf_writef_float(f, pcm, rate * seconds) == rate * seconds);
    assert(!sf_close(f));
    free(pcm);
}
static void init(DseRender* r) {
    memset(r, 0, sizeof(*r));
    dse_score_init(&r->score, 0);
    r->audio_only = 1;
    atomic_init(&r->cancel, 0);
    atomic_init(&r->done, 0);
    atomic_init(&r->progress, 0);
    r->score.wave_count = 1;
}
int main(void) {
    kernel();
    char directory[] = "/tmp/diffsinger-audio-check-XXXXXX", wav[4096], flac[4096],
         saved[4096], msg[512];
    assert(mkdtemp(directory));
    snprintf(wav, sizeof(wav), "%s/stereo.wav", directory);
    snprintf(flac, sizeof(flac), "%s/mono.flac", directory);
    snprintf(saved, sizeof(saved), "%s/song.dse", directory);
    make_file(wav, 44100, 2, SF_FORMAT_WAV | SF_FORMAT_FLOAT, 2);
    make_file(flac, 48000, 1, SF_FORMAT_FLAC | SF_FORMAT_PCM_24, 1);
    DseRender* r = calloc(1, sizeof(*r));
    DseScore* reload = malloc(sizeof(*reload));
    assert(r && reload);
    init(r);
    DseWave* wave = &r->score.waves[0];
    assert(dse_audio_probe(wave, wav, msg, sizeof(msg)));
    wave->skip = wave->trim = 480;
    wave->pos = wave->fade_in = wave->fade_out = 240;
    r->score.tracks[0].gain = -6.020599913f;
    r->score.tracks[0].pan = .25f;
    assert(dse_score_save(&r->score, saved, msg, sizeof(msg)));
    dse_score_init(reload, 0);
    assert(dse_score_load(reload, saved, msg, sizeof(msg)));
    assert(reload->wave_count == 1 &&
           !memcmp(reload->waves, r->score.waves, sizeof(DseWave)));
    assert(dse_render_run(r) && r->samples == 55125);
    for (unsigned i = 0; i < 11025; i++)
        assert(r->pcm[i * 2] == 0 && r->pcm[i * 2 + 1] == 0);
    near(r->pcm[11025 * 2], 0, 0);
    near(r->pcm[20000 * 2], .09375f * (8975.f / 11025), 2e-7f);
    near(r->pcm[30000 * 2], .09375f, 2e-7f);
    near(r->pcm[30000 * 2 + 1], -.25f, 2e-7f);
    near(r->pcm[(r->samples - 1) * 2], 0, 0);
    near(r->peaks[0].high[200], .25f, 0);
    near(r->peaks[0].low[200], -.5f, 0);
    free(r->pcm);
    init(r);
    wave = &r->score.waves[0];
    assert(dse_audio_probe(wave, flac, msg, sizeof(msg)));
    wave->skip = wave->trim = 240;
    wave->pos = 120;
    assert(dse_render_run(r) && r->samples == 27563);
    for (unsigned i = 100; i < 21900; i += 83) {
        float expected = (float)(.25 * sin(2 * 3.14159265358979323846 * 1000 * i / 44100));
        near(r->pcm[(5513 + i) * 2], expected, 4e-5f);
        near(r->pcm[(5513 + i) * 2 + 1], expected, 4e-5f);
    }
    free(r->pcm);
    const int rates[] = {8000, 16000, 22050, 32000, 88200, 96000, 192000, 384000};
    for (unsigned rate = 0; rate < sizeof(rates) / sizeof(*rates); rate++) {
        make_file(wav, rates[rate], 1, SF_FORMAT_WAV | SF_FORMAT_FLOAT, 1);
        init(r);
        assert(dse_audio_probe(&r->score.waves[0], wav, msg, sizeof(msg)));
        assert(dse_render_run(r) && r->samples == 44100);
        for (unsigned i = 1500; i < 42500; i += 173) {
            float expected = (float)(.25 * sin(2 * 3.14159265358979323846 * 1000 * i / 44100));
            near(r->pcm[i * 2], expected, 4e-5f);
            near(r->pcm[i * 2 + 1], expected, 4e-5f);
        }
        free(r->pcm);
    }
    init(r);
    assert(dse_audio_probe(&r->score.waves[0], wav, msg, sizeof(msg)));
    r->score.tracks[0].mute = 1;
    assert(!dse_render_run(r) && !r->pcm);
    init(r);
    assert(dse_audio_probe(&r->score.waves[0], wav, msg, sizeof(msg)));
    atomic_store(&r->cancel, 1);
    assert(!dse_render_run(r) && !r->pcm);
    SF_INFO huge_info = {.samplerate = 44100, .channels = 1,
                         .format = SF_FORMAT_WAV | SF_FORMAT_FLOAT};
    SNDFILE* huge = sf_open(wav, SFM_WRITE, &huge_info);
    float huge_sample = 1e38f;
    assert(huge && sf_writef_float(huge, &huge_sample, 1) == 1 && !sf_close(huge));
    init(r);
    assert(dse_audio_probe(&r->score.waves[0], wav, msg, sizeof(msg)));
    r->score.tracks[0].gain = r->score.waves[0].gain = 60;
    assert(!dse_render_run(r) && !r->pcm);
    init(r);
    assert(dse_audio_probe(&r->score.waves[0], wav, msg, sizeof(msg)));
    strcpy(r->score.waves[0].path, "/tmp/definitely-missing-dse-media.wav");
    assert(!dse_render_run(r) && !r->pcm);
    free(reload);
    free(r);
    unlink(wav);
    unlink(flac);
    unlink(saved);
    rmdir(directory);
    puts("Audio: ASM gains/fades, WAV/FLAC, resampling, timing, crop, peaks, save/reopen, "
         "mute and cancellation OK");
    return 0;
}
