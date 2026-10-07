#define _POSIX_C_SOURCE 200809L
#include "frontend.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
void dse_paint_note(AsmUiCanvas* c, const DseNote* n, const DseRect* r) {
    (void)c;
    (void)n;
    (void)r;
}
static void write_file(const char* root, const char* name, const char* text) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", root, name);
    FILE* f = fopen(path, "wb");
    assert(f && fputs(text, f) >= 0 && !fclose(f));
}
static DseNotePlan plan(DseFrontend* f, DseScore* s, const char* lyric) {
    strcpy(s->notes[0].lyric, lyric);
    s->notes[0].phonemes[0] = 0;
    s->notes[0].timing_slot = 0;
    DseNotePlan p;
    char error[512] = "";
    if (!dse_frontend_note(f, s, 0, &p, error, sizeof(error))) {
        fprintf(stderr, "%s: %s\n", lyric, error);
        abort();
    }
    int sum = 0;
    for (unsigned i = 0; i < p.count; i++) {
        assert(p.phones[i].frames > 0);
        sum += p.phones[i].frames;
    }
    assert(sum == p.stop - p.start);
    return p;
}
int main(void) {
    char root[] = "/tmp/dse-frontend-XXXXXX", acoustic[512], message[512];
    assert(mkdtemp(root));
    snprintf(acoustic, sizeof(acoustic), "%s/acoustic", root);
    assert(!mkdir(acoustic, 0700));
    write_file(root, "acoustic/model.conf", "feature_flags=1\nnum_languages=4\n");
    write_file(root, "acoustic/languages.json", "{\"en\":1,\"ja\":2,\"zh\":3}");
    write_file(root, "acoustic/phonemes.json",
               "{\"PAD\":0,\"SP\":1,\"AP\":2,\"ja/s\":3,\"ja/e\":4,\"ja/k\":5,"
               "\"ja/a\":6,\"ja/i\":7,\"ja/ky\":8,\"ja/cl\":9,\"ja/p\":10,\"ja/u\":11,"
               "\"ja/N\":12,\"ja/o\":13,\"ja/h\":14,\"zh/sh\":15,\"zh/ir\":16,"
               "\"en/w\":17,\"en/er\":18,\"en/l\":19,\"en/d\":20,\"zh/n\":21,\"zh/v\":22}");
    const char* dict = "symbols:\n"
                       "- {symbol: ja/a, type: vowel}\n- {symbol: ja/e, type: vowel}\n"
                       "- {symbol: ja/i, type: vowel}\n- {symbol: ja/o, type: vowel}\n"
                       "- {symbol: ja/u, type: vowel}\n- {symbol: ja/N, type: vowel}\n"
                       "- {symbol: zh/ir, type: vowel}\n- {symbol: zh/v, type: vowel}\n- {symbol: "
                       "en/er, type: vowel}\n"
                       "entries:\n"
                       "- {grapheme: se, phonemes: [ja/s, ja/e]}\n"
                       "- {grapheme: ka, phonemes: [ja/k, ja/a]}\n"
                       "- {grapheme: i, phonemes: [ja/i]}\n"
                       "- {grapheme: a, phonemes: [ja/a]}\n"
                       "- {grapheme: o, phonemes: [ja/o]}\n"
                       "- {grapheme: u, phonemes: [ja/u]}\n"
                       "- {grapheme: kya, phonemes: [ja/ky, ja/a]}\n"
                       "- {grapheme: pu, phonemes: [ja/p, ja/u]}\n"
                       "- {grapheme: cl, phonemes: [ja/cl]}\n"
                       "- {grapheme: n, phonemes: [ja/N]}\n"
                       "- {grapheme: ko, phonemes: [ja/k, ja/o]}\n"
                       "- {grapheme: hi, phonemes: [ja/h, ja/i]}\n"
                       "- {grapheme: 'zh/shi', phonemes: [zh/sh, zh/ir]}\n"
                       "- {grapheme: 'zh/nv', phonemes: [zh/n, zh/v]}\n"
                       "- {grapheme: 'en/world', phonemes: [en/w, en/er, en/l, en/d]}\n";
    write_file(root, "dsdict-ja.yaml", dict);
    write_file(root, "dsdict-zh.yaml", dict);
    write_file(root, "dsdict-en.yaml", dict);
    DseEngineConfig c = {0};
    strcpy(c.bundle, root);
    assert(dse_engine_language(&c, "ja", message, sizeof(message)) && c.language == 2);
    DseFrontend* f = dse_frontend_open(&c, message, sizeof(message));
    assert(f);
    atomic_int cancel;
    atomic_init(&cancel, 1);
    assert(!dse_frontend_open_cancel(&c, &cancel, message, sizeof(message)) &&
           strstr(message, "已取消"));
    DseScore* s = calloc(1, sizeof(*s));
    assert(s);
    dse_score_init(s, 0);
    assert(dse_add_note(s, 0, 0, 960, 69) == 0);
    const char* spellings[] = {"せかい", "セカイ", "世界", "sekai", "ＳＥＫＡＩ"};
    uint64_t expected = 0;
    for (unsigned i = 0; i < sizeof(spellings) / sizeof(*spellings); i++) {
        DseNotePlan p = plan(f, s, spellings[i]);
        assert(p.count == 5 && !strcmp(p.phones[0].symbol, "ja/s") &&
               !strcmp(p.phones[4].symbol, "ja/i"));
        for (unsigned k = 0; k < p.count; k++)
            assert(p.phones[k].language == 2);
        if (!i)
            expected = p.signature;
        assert(p.signature == expected);
        assert(p.phones[1].frames > 20 && p.phones[3].frames > 20 && p.phones[4].frames > 20);
    }
    DseNotePlan p = plan(f, s, "きゃっぷ");
    assert(p.count == 5 && !strcmp(p.phones[2].symbol, "ja/cl"));
    DseNotePlan roman = plan(f, s, "kyappu");
    assert(roman.signature == p.signature);
    p = plan(f, s, "コーヒー");
    assert(p.count == 6 && !strcmp(p.phones[2].symbol, "ja/o") &&
           !strcmp(p.phones[5].symbol, "ja/i"));
    p = plan(f, s, "ん");
    assert(p.count == 1 && !strcmp(p.phones[0].symbol, "ja/N"));
    p = plan(f, s, "en/world zh/shi");
    assert(p.count == 6 && p.phones[0].language == 1 && p.phones[4].language == 3);
    p = plan(f, s, "世界 [k a]");
    assert(p.count == 2 && !strcmp(p.phones[0].symbol, "ja/k"));
    p = plan(f, s, "[ SP ]");
    assert(p.count == 1 && p.phones[0].language == 0);

    p = plan(f, s, "ka");
    DseTiming timing;
    assert(dse_timing_parse("80 *", s, 0, &p, &timing, message, sizeof(message)));
    assert(timing.count == 2 && timing.ticks[0] == 77);
    assert(dse_timing_set(s, 0, &timing));
    assert(dse_frontend_note(f, s, 0, &p, message, sizeof(message)) && p.phones[0].frames == 7);
    assert(!dse_timing_parse("nan *", s, 0, &p, &timing, message, sizeof(message)));
    assert(!dse_timing_parse("1 *", s, 0, &p, &timing, message, sizeof(message)));
    assert(!dse_timing_parse("1200 *", s, 0, &p, &timing, message, sizeof(message)));
    assert(!dse_timing_parse("20 30", s, 0, &p, &timing, message, sizeof(message)));
    strcpy(s->notes[0].lyric, "se");
    assert(!dse_frontend_note(f, s, 0, &p, message, sizeof(message)) && strstr(message, "不匹配"));
    strcpy(s->notes[0].lyric, "ka");
    assert(dse_frontend_note(f, s, 0, &p, message, sizeof(message)));
    char saved[512];
    snprintf(saved, sizeof(saved), "%s/saved.dse", root);
    assert(dse_score_save(s, saved, message, sizeof(message)));
    DseScore* loaded = calloc(1, sizeof(*loaded));
    assert(loaded && dse_score_load(loaded, saved, message, sizeof(message)));
    assert(dse_frontend_note(f, loaded, 0, &p, message, sizeof(message)) &&
           p.phones[0].frames == 7);
    free(loaded);
    assert(dse_add_note(s, 0, 960, 480, 72) == 1);
    strcpy(s->notes[1].lyric, "+");
    s->notes[1].phonemes[0] = 0;
    DsePartPlan part;
    assert(dse_frontend_part(f, s, 0, &part, message, sizeof(message)));
    assert(part.count == 2 && part.frames == (unsigned)dse_frontend_frame(1440, s->bpm) &&
           part.phones[0].frames == 7 && part.phones[1].frames > 100);
    dse_part_plan_clear(&part);
    assert(dse_frontend_note(f, s, 1, &p, message, sizeof(message)) && p.tied &&
           p.source_index == 0);
    strcpy(s->notes[1].lyric, "+~");
    assert(dse_frontend_note(f, s, 1, &p, message, sizeof(message)) && p.tied);
    strcpy(s->notes[1].lyric, "+*");
    assert(dse_frontend_note(f, s, 1, &p, message, sizeof(message)) && p.tied);
    strcpy(s->notes[1].lyric, "+");
    assert(!dse_timing_parse("80 *", s, 1, &p, &timing, message, sizeof(message)));
    s->notes[1].pos++;
    assert(!dse_frontend_part(f, s, 0, &part, message, sizeof(message)) && strstr(message, "延音"));
    s->notes[1].pos--;
    s->notes[0].timing_slot = 0;
    strcpy(s->notes[0].lyric, "en/world");
    assert(dse_frontend_part(f, s, 0, &part, message, sizeof(message)));
    assert(part.count == 4 && part.phones[1].frames > 100 && part.phones[2].frames == 3 &&
           part.phones[3].frames == 3);
    dse_part_plan_clear(&part);
    strcpy(s->notes[0].lyric, "SP");
    assert(!dse_frontend_part(f, s, 0, &part, message, sizeof(message)) &&
           strstr(message, "可延长"));
    strcpy(s->notes[0].lyric, "+");
    assert(!dse_frontend_part(f, s, 0, &part, message, sizeof(message)));
    strcpy(s->notes[0].lyric, "ka");
    s->notes[1].pos = 900;
    assert(!dse_frontend_part(f, s, 0, &part, message, sizeof(message)) && strstr(message, "重叠"));

    s->notes[1].pos = 960;
    strcpy(s->notes[1].lyric, "ka");
    assert(dse_frontend_note(f, s, 0, &p, message, sizeof(message)));
    assert(dse_timing_parse("80 *", s, 0, &p, &timing, message, sizeof(message)));
    assert(dse_timing_set(s, 0, &timing));
    s->notes[1].timing_slot = s->notes[0].timing_slot;
    timing.ticks[0] = 50;
    assert(dse_timing_set(s, 1, &timing));
    assert(s->notes[0].timing_slot != s->notes[1].timing_slot &&
           dse_timing_get(s, &s->notes[0])->ticks[0] == 77);
    assert(dse_score_validate(s, message, sizeof(message)));
    dse_score_init(s, 0);
    assert(dse_add_note(s, 0, 0, 480, 69) == 0);
    assert(dse_add_note(s, 0, 480, 480, 72) == 1);
    assert(dse_add_note(s, 0, 960, 480, 74) == 2);
    strcpy(s->notes[0].lyric, "せかい");
    strcpy(s->notes[1].lyric, "+");
    strcpy(s->notes[2].lyric, "+");
    for (unsigned i = 0; i < 3; i++)
        s->notes[i].phonemes[0] = 0;
    assert(dse_frontend_note(f, s, 0, &p, message, sizeof(message)) && p.progression);
    assert(p.count == 2 && p.owner_index == 0 && p.source_index == 0 && p.phones[0].frames == 3 &&
           p.phones[1].frames == 40);
    assert(dse_frontend_note(f, s, 1, &p, message, sizeof(message)) && p.progression && !p.tied);
    assert(p.count == 2 && p.owner_index == 1 && p.source_index == 0 &&
           !strcmp(p.phones[0].symbol, "ja/k"));
    assert(dse_timing_parse("80 *", s, 1, &p, &timing, message, sizeof(message)));
    assert(dse_timing_set(s, 1, &timing));
    assert(dse_frontend_part(f, s, 0, &part, message, sizeof(message)));
    assert(part.count == 5 && part.phones[0].frames == 3 && part.phones[1].frames == 40 &&
           part.phones[2].frames == 7 && part.phones[3].frames == 36 &&
           part.phones[4].frames == 43);
    dse_part_plan_clear(&part);
    assert(dse_frontend_note(f, s, 2, &p, message, sizeof(message)) && !p.tied);
    assert(p.count == 1 && p.owner_index == 2 && !strcmp(p.phones[0].symbol, "ja/i"));
    assert(dse_timing_set(s, 1, NULL));
    strcpy(s->notes[2].lyric, "+~");
    assert(dse_frontend_note(f, s, 1, &p, message, sizeof(message)) && p.progression);
    assert(p.count == 3 && p.phones[1].frames == 20 && p.phones[2].frames == 63);
    assert(dse_timing_parse("80 180 *", s, 1, &p, &timing, message, sizeof(message)));
    assert(dse_timing_set(s, 1, &timing));
    assert(dse_frontend_note(f, s, 2, &p, message, sizeof(message)) && p.tied &&
           p.owner_index == 1 && p.source_index == 0);
    assert(p.count == 3 && p.phones[0].frames == 7 && p.phones[1].frames == 15 &&
           p.phones[2].frames == 64);
    assert(!dse_timing_parse("80 180 *", s, 2, &p, &timing, message, sizeof(message)));
    assert(dse_score_save(s, saved, message, sizeof(message)));
    loaded = calloc(1, sizeof(*loaded));
    assert(loaded && dse_score_load(loaded, saved, message, sizeof(message)));
    assert(dse_frontend_part(f, loaded, 0, &part, message, sizeof(message)));
    assert(part.count == 5 && part.phones[2].frames == 7 && part.phones[3].frames == 15 &&
           part.phones[4].frames == 64);
    dse_part_plan_clear(&part);
    free(loaded);
    strcpy(s->notes[2].lyric, "+");
    assert(!dse_frontend_note(f, s, 1, &p, message, sizeof(message)) && strstr(message, "不匹配"));
    assert(dse_timing_set(s, 1, NULL));
    strcpy(s->notes[2].lyric, "+~");
    strcpy(s->notes[1].lyric, "+~");
    assert(dse_frontend_note(f, s, 0, &p, message, sizeof(message)) && !p.progression);
    assert(p.phones[1].frames == 13 && p.phones[3].frames == 12 && p.phones[4].frames == 98);
    /* The minimum frame allocation is checked per syllable, not against every
     * phoneme of the whole word in its first short note. */
    s->notes[0].len = 30;
    s->notes[1].pos = 30;
    s->notes[1].len = 30;
    strcpy(s->notes[1].lyric, "+");
    s->notes[2].pos = 60;
    s->notes[2].len = 30;
    strcpy(s->notes[2].lyric, "+");
    assert(dse_frontend_part(f, s, 0, &part, message, sizeof(message)));
    assert(part.count == 5 && part.frames == 8);
    dse_part_plan_clear(&part);
    s->notes[0].len = 960;
    dse_frontend_close(f);
    assert(dse_engine_cycle_language(&c, message, sizeof(message)) &&
           !strcmp(c.language_code, "zh"));
    f = dse_frontend_open(&c, message, sizeof(message));
    assert(f);
    s->count = 1;
    p = plan(f, s, "女");
    assert(p.count == 2 && !strcmp(p.phones[1].symbol, "zh/v"));
    DseNotePlan spelled = plan(f, s, "nü");
    assert(spelled.signature == p.signature);
    dse_frontend_close(f);
    assert(dse_engine_cycle_language(&c, message, sizeof(message)) &&
           !strcmp(c.language_code, "en"));
    f = dse_frontend_open(&c, message, sizeof(message));
    assert(f);
    s->count = 1;
    p = plan(f, s, "world");
    assert(p.count == 4 && p.phones[0].language == 1);
    dse_frontend_close(f);
    write_file(root, "acoustic/languages.json", "{\"en\":9}");
    assert(!dse_frontend_open(&c, message, sizeof(message)));
    free(s);
    const char* files[] = {"acoustic/model.conf",
                           "acoustic/languages.json",
                           "acoustic/phonemes.json",
                           "dsdict-ja.yaml",
                           "dsdict-zh.yaml",
                           "dsdict-en.yaml",
                           "saved.dse"};
    for (unsigned i = 0; i < sizeof(files) / sizeof(*files); i++) {
        snprintf(saved, sizeof(saved), "%s/%s", root, files[i]);
        assert(!unlink(saved));
    }
    assert(!rmdir(acoustic) && !rmdir(root));
    puts("Frontend: kana/kanji/romaji, language IDs, multi-mora timing, ties/codas, manual "
         "durations and persistence OK");
}
