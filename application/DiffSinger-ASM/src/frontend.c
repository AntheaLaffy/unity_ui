#define _POSIX_C_SOURCE 200809L
#include "frontend.h"
#include <ctype.h>
#include <errno.h>
#include <json-c/json.h>
#include <math.h>
#include <mecab.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unicode/uchar.h>
#include <unicode/ustring.h>
#include <unicode/utrans.h>
#include <unistd.h>
#include <yaml.h>

struct DseFrontend {
    DseEngineConfig config;
    json_object *vocab, *languages, *dictionary, *vowels;
    UTransliterator *chinese, *japanese, *lower, *ascii;
    mecab_t* mecab;
    unsigned flags, rows;
    const atomic_int* cancel;
};
static int cancelled(const DseFrontend* f) {
    return f->cancel && atomic_load_explicit(f->cancel, memory_order_relaxed);
}
static int fail(char* out, size_t n, const char* text) {
    snprintf(out, n, "%s", text);
    return 0;
}
static json_object* get(json_object* o, const char* key) {
    json_object* v = NULL;
    if (json_object_is_type(o, json_type_object))
        json_object_object_get_ex(o, key, &v);
    return v;
}
static int path(char* out, size_t n, const char* a, const char* b) {
    int count = snprintf(out, n, "%s/%s", a, b);
    return count >= 0 && (size_t)count < n;
}
static json_object* languages(const DseEngineConfig* c) {
    char file[DSE_PATH];
    if (!path(file, sizeof(file), c->bundle, "acoustic/languages.json"))
        return NULL;
    json_object* o = json_object_from_file(file);
    if (!json_object_is_type(o, json_type_object)) {
        if (o)
            json_object_put(o);
        return NULL;
    }
    unsigned used[1025] = {0};
    json_object_object_foreach(o, key, v) {
        int id = json_object_get_int(v);
        if (!*key || strlen(key) >= 16 || strchr(key, '/') || strchr(key, '.') ||
            !json_object_is_type(v, json_type_int) || id < 1 || id > 1024 || used[id]) {
            json_object_put(o);
            return NULL;
        }
        used[id] = 1;
    }
    return o;
}
static void bank_root(DseEngineConfig* c) {
    if (*c->bank)
        return;
    snprintf(c->bank, sizeof(c->bank), "%s", c->bundle);
    char* generations = strstr(c->bank, "/dsasm/generations/");
    if (generations)
        *generations = 0;
}
int dse_engine_language(DseEngineConfig* c, const char* code, char* msg, size_t n) {
    json_object* o = languages(c);
    json_object* id = get(o, code);
    if (!id) {
        if (o)
            json_object_put(o);
        return fail(msg, n, "声库语言元数据中没有所选语言");
    }
    DseEngineConfig next = *c;
    bank_root(&next);
    strcpy(next.language_code, code);
    next.language = (unsigned)json_object_get_int(id);
    json_object_put(o);
    char suffix[128];
    snprintf(suffix, sizeof(suffix), "dsdur/dsdict-%s.yaml", code);
    if (!path(next.dictionary, sizeof(next.dictionary), next.bank, suffix) ||
        access(next.dictionary, R_OK)) {
        snprintf(suffix, sizeof(suffix), "dsdict-%s.yaml", code);
        if (!path(next.dictionary, sizeof(next.dictionary), next.bank, suffix) ||
            access(next.dictionary, R_OK)) {
            if (!path(next.dictionary, sizeof(next.dictionary), next.bank, "dsdict.yaml") ||
                access(next.dictionary, R_OK))
                next.dictionary[0] = 0;
        }
    }
    *c = next;
    snprintf(msg, n, "已选择歌词语言 %s · 模型编号 %u", code, next.language);
    return 1;
}
int dse_engine_cycle_language(DseEngineConfig* c, char* msg, size_t n) {
    json_object* o = languages(c);
    if (!o)
        return fail(msg, n, "声库没有可用的 languages.json");
    char first[16] = "", following[16] = "";
    json_object_object_foreach(o, key, value) {
        (void)value;
        if (!*first || strcmp(key, first) < 0)
            strcpy(first, key);
        if (strcmp(key, c->language_code) > 0 && (!*following || strcmp(key, following) < 0))
            strcpy(following, key);
    }
    json_object_put(o);
    const char* next = *following ? following : first;
    if (!*next || !strcmp(next, c->language_code))
        return fail(msg, n, "声库只有一种可用歌词语言");
    return dse_engine_language(c, next, msg, n);
}
static yaml_node_t* yget(yaml_document_t* d, yaml_node_t* node, const char* key) {
    if (!node || node->type != YAML_MAPPING_NODE)
        return NULL;
    for (yaml_node_pair_t* p = node->data.mapping.pairs.start; p < node->data.mapping.pairs.top;
         p++) {
        yaml_node_t* k = yaml_document_get_node(d, p->key);
        if (k && k->type == YAML_SCALAR_NODE && !strcmp((char*)k->data.scalar.value, key))
            return yaml_document_get_node(d, p->value);
    }
    return NULL;
}
static const char* scalar(yaml_node_t* node) {
    return node && node->type == YAML_SCALAR_NODE ? (char*)node->data.scalar.value : NULL;
}
typedef struct DictionaryInput {
    FILE* file;
    const DseFrontend* frontend;
} DictionaryInput;
static int dictionary_read(void* data, unsigned char* buffer, size_t size, size_t* read) {
    DictionaryInput* input = data;
    *read = 0;
    if (cancelled(input->frontend))
        return 0;
    *read = fread(buffer, 1, size, input->file);
    return !ferror(input->file);
}
static int load_dictionary(DseFrontend* f, char* msg, size_t n) {
    if (!*f->config.dictionary)
        return 1;
    struct stat st;
    if (stat(f->config.dictionary, &st) || st.st_size > 64 * 1024 * 1024)
        return fail(msg, n, "无法读取声库词典，或词典超过 64 MiB");
    FILE* file = fopen(f->config.dictionary, "rb");
    if (!file)
        return fail(msg, n, "无法打开声库词典");
    yaml_parser_t parser;
    yaml_document_t doc;
    if (!yaml_parser_initialize(&parser)) {
        fclose(file);
        return fail(msg, n, "无法分配词典解析器");
    }
    DictionaryInput input = {file, f};
    yaml_parser_set_input(&parser, dictionary_read, &input);
    int valid = yaml_parser_load(&parser, &doc);
    yaml_parser_delete(&parser);
    fclose(file);
    if (cancelled(f)) {
        if (valid)
            yaml_document_delete(&doc);
        return fail(msg, n, "合成已取消");
    }
    if (!valid)
        return fail(msg, n, "声库词典 YAML 无效");
    yaml_node_t* root = yaml_document_get_root_node(&doc);
    yaml_node_t* symbols = yget(&doc, root, "symbols");
    if (symbols && symbols->type == YAML_SEQUENCE_NODE)
        for (yaml_node_item_t* i = symbols->data.sequence.items.start;
             i < symbols->data.sequence.items.top; i++) {
            if (cancelled(f))
                goto stopped;
            yaml_node_t* entry = yaml_document_get_node(&doc, *i);
            const char *symbol = scalar(yget(&doc, entry, "symbol")),
                       *type = scalar(yget(&doc, entry, "type"));
            if (symbol && type && !strcmp(type, "vowel"))
                json_object_object_add(f->vowels, symbol, json_object_new_boolean(1));
        }
    yaml_node_t* entries = yget(&doc, root, "entries");
    valid = entries && entries->type == YAML_SEQUENCE_NODE;
    if (valid)
        for (yaml_node_item_t* i = entries->data.sequence.items.start;
             i < entries->data.sequence.items.top; i++) {
            if (cancelled(f))
                goto stopped;
            yaml_node_t* entry = yaml_document_get_node(&doc, *i);
            const char* word = scalar(yget(&doc, entry, "grapheme"));
            yaml_node_t* phones = yget(&doc, entry, "phonemes");
            if (!word || !*word || !phones || phones->type != YAML_SEQUENCE_NODE)
                continue;
            char text[4096];
            size_t used = 0;
            for (yaml_node_item_t* p = phones->data.sequence.items.start;
                 p < phones->data.sequence.items.top; p++) {
                const char* s = scalar(yaml_document_get_node(&doc, *p));
                if (!s || !*s) {
                    used = 0;
                    break;
                }
                int count = snprintf(text + used, sizeof(text) - used, "%s%s", used ? " " : "", s);
                if (count < 0 || (size_t)count >= sizeof(text) - used) {
                    used = 0;
                    break;
                }
                used += (size_t)count;
            }
            if (used && !get(f->dictionary, word))
                json_object_object_add(f->dictionary, word, json_object_new_string(text));
        }
    yaml_document_delete(&doc);
    return valid || fail(msg, n, "声库词典缺少 entries 音素映射");
stopped:
    yaml_document_delete(&doc);
    return fail(msg, n, "合成已取消");
}
static UTransliterator* transliterator(const char* name) {
    UErrorCode status = U_ZERO_ERROR;
    UChar id[256];
    int32_t length = 0;
    u_strFromUTF8(id, 256, &length, name, -1, &status);
    UTransliterator* t = utrans_openU(id, length, UTRANS_FORWARD, NULL, 0, NULL, &status);
    return U_SUCCESS(status) ? t : NULL;
}
static int transliterate(UTransliterator* t, const char* text, char* out, size_t n) {
    if (!t)
        return 0;
    UErrorCode status = U_ZERO_ERROR;
    UChar buffer[2048];
    int32_t length = 0;
    u_strFromUTF8(buffer, 2048, &length, text, -1, &status);
    int32_t limit = length;
    utrans_transUChars(t, buffer, &length, 2048, 0, &limit, &status);
    u_strToUTF8(out, (int32_t)n, NULL, buffer, length, &status);
    return U_SUCCESS(status);
}
void dse_frontend_close(DseFrontend* f) {
    if (!f)
        return;
    if (f->mecab)
        mecab_destroy(f->mecab);
    if (f->chinese)
        utrans_close(f->chinese);
    if (f->japanese)
        utrans_close(f->japanese);
    if (f->lower)
        utrans_close(f->lower);
    if (f->ascii)
        utrans_close(f->ascii);
    json_object_put(f->vocab);
    json_object_put(f->languages);
    json_object_put(f->dictionary);
    json_object_put(f->vowels);
    free(f);
}
DseFrontend* dse_frontend_open_cancel(const DseEngineConfig* c, const atomic_int* cancel, char* msg,
                                      size_t n) {
    DseFrontend* f = calloc(1, sizeof(*f));
    if (!f) {
        fail(msg, n, "无法分配音素前端");
        return NULL;
    }
    f->config = *c;
    f->cancel = cancel;
    if (cancelled(f)) {
        fail(msg, n, "合成已取消");
        goto error;
    }
    f->dictionary = json_object_new_object();
    f->vowels = json_object_new_object();
    char file[DSE_PATH];
    if (path(file, sizeof(file), c->bundle, "acoustic/model.conf")) {
        FILE* conf = fopen(file, "r");
        if (conf) {
            char line[512];
            while (fgets(line, sizeof(line), conf)) {
                sscanf(line, "feature_flags=%u", &f->flags);
                sscanf(line, "num_languages=%u", &f->rows);
            }
            fclose(conf);
        }
    }
    if (path(file, sizeof(file), c->bundle, "acoustic/phonemes.json"))
        f->vocab = json_object_from_file(file);
    f->languages = languages(c);
    if (!json_object_is_type(f->vocab, json_type_object) ||
        !json_object_is_type(get(f->vocab, "SP"), json_type_int) ||
        json_object_get_int(get(f->vocab, "SP")) <= 0) {
        fail(msg, n, "声库音素词表无效或缺少 SP");
        goto error;
    }
    json_object_object_foreach(f->vocab, symbol, id) {
        if (!*symbol || strlen(symbol) >= sizeof(((DsePhone*)0)->symbol) ||
            !json_object_is_type(id, json_type_int) || json_object_get_int64(id) < 0 ||
            json_object_get_int64(id) > 1000000) {
            fail(msg, n, "声库音素词表包含无效符号或编号");
            goto error;
        }
    }
    if ((f->flags & 1u) && (!f->languages || !f->rows)) {
        fail(msg, n, "多语言模型缺少有效 languages.json 或 num_languages");
        goto error;
    }
    if (f->languages && (f->flags & 1u)) {
        json_object_object_foreach(f->languages, code, id) {
            (void)code;
            if ((unsigned)json_object_get_int(id) >= f->rows) {
                fail(msg, n, "语言元数据编号超出模型语言表");
                goto error;
            }
        }
    }
    if (!*f->config.language_code) {
        const char* guess = strstr(c->dictionary, "dsdict-ja") ? "ja" : "zh";
        json_object* id = get(f->languages, guess);
        if (id) {
            strcpy(f->config.language_code, guess);
            f->config.language = (unsigned)json_object_get_int(id);
        }
    }
    if ((f->flags & 1u) && (!f->config.language || f->config.language >= f->rows)) {
        fail(msg, n, "请选择有效歌词语言；语言编号 0 为模型填充位");
        goto error;
    }
    if (*f->config.language_code && f->languages) {
        json_object* id = get(f->languages, f->config.language_code);
        if (!id || (unsigned)json_object_get_int(id) != f->config.language) {
            fail(msg, n, "歌词语言与声库语言编号不一致，请重新选择语言");
            goto error;
        }
    }
    if (!load_dictionary(f, msg, n))
        goto error;
    f->chinese = transliterator("Han-Latin; NFD; Lower()");
    f->japanese = transliterator("NFKC; Katakana-Hiragana; Hiragana-Latin; Latin-ASCII; Lower()");
    f->lower = transliterator("NFKC; Lower()");
    f->ascii = transliterator("Latin-ASCII");
    if (!f->chinese || !f->japanese || !f->lower || !f->ascii) {
        fail(msg, n, "ICU 读音转换器不可用");
        goto error;
    }
    if (cancelled(f)) {
        fail(msg, n, "合成已取消");
        goto error;
    }
    return f;
error:
    dse_frontend_close(f);
    return NULL;
}
DseFrontend* dse_frontend_open(const DseEngineConfig* c, char* msg, size_t n) {
    return dse_frontend_open_cancel(c, NULL, msg, n);
}
int dse_frontend_frame(int tick, float bpm) {
    return (int)lround((double)tick * 60 * 44100 / (bpm * DSE_PPQ * 512));
}
static int silent(const char* s) {
    return !strcmp(s, "SP") || !strcmp(s, "AP") || !strcmp(s, "EP") || !strcmp(s, "GS") ||
           !strcmp(s, "um");
}
static int add_phone(DseFrontend* f, DseNotePlan* p, const char* symbol, char* msg, size_t n) {
    json_object* id = get(f->vocab, symbol);
    if (!id || json_object_get_int(id) <= 0) {
        snprintf(msg, n, "声库不支持音素 %s", symbol);
        return 0;
    }
    if (p->count == DSE_MAX_PHONEMES)
        return fail(msg, n, "单个歌词最多支持 64 个音素，请拆分歌词");
    DsePhone* phone = &p->phones[p->count++];
    strcpy(phone->symbol, symbol);
    phone->token = json_object_get_int(id);
    phone->vowel = !silent(symbol) && get(f->vowels, symbol) != NULL;
    phone->language = silent(symbol) ? 0 : (int)f->config.language;
    const char* slash = strchr(symbol, '/');
    if (slash) {
        char code[64];
        size_t bytes = (size_t)(slash - symbol);
        memcpy(code, symbol, bytes);
        code[bytes] = 0;
        json_object* language = get(f->languages, code);
        if (!language && (f->flags & 1u)) {
            snprintf(msg, n, "音素 %s 没有对应语言编号", symbol);
            return 0;
        }
        if (language)
            phone->language = json_object_get_int(language);
    }
    if (!silent(symbol) && get(f->vowels, symbol))
        p->sustain = (int)p->count - 1;
    return 1;
}
static int add_symbols(DseFrontend* f, DseNotePlan* p, const char* text, char* msg, size_t n) {
    char buffer[4096];
    if (strlen(text) >= sizeof(buffer))
        return fail(msg, n, "词典音素过长");
    strcpy(buffer, text);
    char* save = NULL;
    for (char* s = strtok_r(buffer, " ,\t\r\n", &save); s; s = strtok_r(NULL, " ,\t\r\n", &save))
        if (!add_phone(f, p, s, msg, n))
            return 0;
    return 1;
}
static const char* lookup(DseFrontend* f, const char* word) {
    json_object* v = NULL;
    if (!strchr(word, '/') && *f->config.language_code) {
        char key[1024];
        int size = snprintf(key, sizeof(key), "%s/%s", f->config.language_code, word);
        if (size > 0 && (size_t)size < sizeof(key))
            v = get(f->dictionary, key);
    }
    if (!v)
        v = get(f->dictionary, word);
    return v ? json_object_get_string(v) : NULL;
}
static int alias(DseFrontend* f, DseNotePlan* p, const char* word, char* msg, size_t n) {
    const char* phones = lookup(f, word);
    return phones ? add_symbols(f, p, phones, msg, n) : add_phone(f, p, word, msg, n);
}
static int has_han(const char* text) {
    UErrorCode status = U_ZERO_ERROR;
    UChar u[1024];
    int32_t length = 0;
    u_strFromUTF8(u, 1024, &length, text, -1, &status);
    if (U_FAILURE(status))
        return -1;
    for (int32_t i = 0; i < length;) {
        UChar32 c;
        U16_NEXT(u, i, length, c);
        if (u_hasBinaryProperty(c, UCHAR_IDEOGRAPHIC))
            return 1;
    }
    return 0;
}
static int japanese_reading(DseFrontend* f, const char* text, char* out, size_t n, char* msg,
                            size_t m) {
    int han = has_han(text);
    if (han < 0)
        return fail(msg, m, "歌词不是有效 UTF-8");
    if (!han) {
        snprintf(out, n, "%s", text);
        return 1;
    }
    if (!f->mecab) {
        /* IPA's pronunciation column includes particle readings and long
         * vowels. Require that exact schema rather than guessing another dic. */
        f->mecab = mecab_new2("-d /usr/lib/mecab/dic/ipadic");
        const mecab_dictionary_info_t* info = f->mecab ? mecab_dictionary_info(f->mecab) : NULL;
        if (!info || (strcmp(info->charset, "utf-8") && strcmp(info->charset, "UTF-8")))
            return fail(msg, m, "日文汉字读音需要 UTF-8 MeCab IPA 词典");
    }
    const mecab_node_t* node = mecab_sparse_tonode(f->mecab, text);
    if (!node)
        return fail(msg, m, "MeCab 无法解析歌词，请填入假名读音");
    size_t used = 0;
    for (; node; node = node->next) {
        if (node->stat != MECAB_NOR_NODE && node->stat != MECAB_UNK_NODE)
            continue;
        const char* reading = NULL;
        size_t bytes = 0;
        const char* field = node->feature;
        for (int i = 0; field && i <= 8; i++) {
            const char* end = strchr(field, ',');
            if (i == 8) {
                bytes = end ? (size_t)(end - field) : strlen(field);
                if (bytes && !(bytes == 1 && *field == '*'))
                    reading = field;
                break;
            }
            field = end ? end + 1 : NULL;
        }
        /* The IPA pronunciation for the object particle を is written ヲ. */
        if (!strncmp(node->feature, "助詞,", strlen("助詞,")) && node->length == strlen("を") &&
            !memcmp(node->surface, "を", node->length)) {
            reading = "オ";
            bytes = strlen(reading);
        }
        char surface[256];
        if (!reading) {
            if (node->length >= sizeof(surface))
                return fail(msg, m, "日文读音片段过长");
            memcpy(surface, node->surface, node->length);
            surface[node->length] = 0;
            if (has_han(surface)) {
                snprintf(msg, m, "日文词“%s”没有确定读音，请在音素栏填入假名或罗马字", surface);
                return 0;
            }
            reading = surface;
            bytes = node->length;
        }
        if (bytes >= n - used)
            return fail(msg, m, "日文读音过长，请拆分歌词");
        memcpy(out + used, reading, bytes);
        used += bytes;
    }
    out[used] = 0;
    return used > 0 || fail(msg, m, "歌词没有可发音内容");
}
static int japanese(DseFrontend* f, DseNotePlan* p, const char* text, char* msg, size_t n) {
    char reading[1024], roman[2048];
    if (!japanese_reading(f, text, reading, sizeof(reading), msg, n))
        return 0;
    if (!transliterate(f->japanese, reading, roman, sizeof(roman)))
        return fail(msg, n, "日文假名转换失败");
    for (size_t i = 0; roman[i];) {
        unsigned char c = (unsigned char)roman[i];
        if (isspace(c) || c == '\'' || c == ',' || c == '.' || c == '-') {
            i++;
            continue;
        }
        if (!strncmp(roman + i, "~tsu", 4)) {
            if (!alias(f, p, "cl", msg, n))
                return 0;
            i += 4;
            continue;
        }
        if (c != 'n' && c != 'a' && c != 'e' && c != 'i' && c != 'o' && c != 'u' &&
            (roman[i + 1] == (char)c || (c == 't' && roman[i + 1] == 'c'))) {
            if (!alias(f, p, "cl", msg, n))
                return 0;
            i++;
            continue;
        }
        int found = 0;
        for (size_t bytes = 4; bytes; bytes--) {
            if (strlen(roman + i) < bytes)
                continue;
            char mora[5];
            memcpy(mora, roman + i, bytes);
            mora[bytes] = 0;
            const char* symbols = lookup(f, mora);
            if (!symbols && !strcmp(mora, "n"))
                symbols = lookup(f, "N");
            if (symbols) {
                if (!add_symbols(f, p, symbols, msg, n))
                    return 0;
                i += bytes;
                found = 1;
                break;
            }
        }
        if (!found) {
            snprintf(msg, n, "日文读音“%s”在声库词典中没有映射", roman + i);
            return 0;
        }
    }
    return p->count > 0 || fail(msg, n, "歌词没有可发音内容");
}
static int resolve(DseFrontend* f, const DseNote* note, DseNotePlan* p, char* msg, size_t n) {
    const char* text = *note->phonemes ? note->phonemes : note->lyric;
    const char *begin = *note->phonemes ? NULL : strchr(text, '['),
               *end = begin ? strrchr(begin + 1, ']') : NULL;
    if (begin && end) {
        char hint[1024];
        size_t bytes = (size_t)(end - begin - 1);
        if (!bytes || bytes >= sizeof(hint))
            return fail(msg, n, "歌词方括号中的音素提示为空或过长");
        memcpy(hint, begin + 1, bytes);
        hint[bytes] = 0;
        char* save = NULL;
        for (char* token = strtok_r(hint, " \t\r\n", &save); token;
             token = strtok_r(NULL, " \t\r\n", &save)) {
            char prefixed[128];
            const char* symbol = token;
            if (!get(f->vocab, symbol) && !strchr(token, '/')) {
                snprintf(prefixed, sizeof(prefixed), "%s/%s", f->config.language_code, token);
                symbol = prefixed;
            }
            if (!add_phone(f, p, symbol, msg, n))
                return 0;
        }
        return p->count > 0 || fail(msg, n, "歌词方括号中没有音素");
    }
    const char* symbols = lookup(f, text);
    if (symbols)
        return add_symbols(f, p, symbols, msg, n);
    if (get(f->vocab, text))
        return add_phone(f, p, text, msg, n);
    if (strchr(text, '/') || (*note->phonemes && strchr(text, ' '))) {
        char buffer[1024];
        snprintf(buffer, sizeof(buffer), "%s", text);
        char* save = NULL;
        for (char* token = strtok_r(buffer, " ,\t\r\n", &save); token;
             token = strtok_r(NULL, " ,\t\r\n", &save))
            if (!alias(f, p, token, msg, n))
                return 0;
        return p->count > 0;
    }
    if (!strcmp(f->config.language_code, "ja"))
        return japanese(f, p, text, msg, n);
    char roman[2048];
    UTransliterator* trans =
        !strcmp(f->config.language_code, "zh") || !*f->config.language_code ? f->chinese : f->lower;
    if (!transliterate(trans, text, roman, sizeof(roman)))
        return fail(msg, n, "歌词读音转换失败");
    if (trans == f->chinese) {
        /* The bank convention spells ü as v. Do this before accent removal,
         * which would collapse 女/绿 into the different nu/lu syllables. */
        for (char* at = strstr(roman, "u\xcc\x88"); at; at = strstr(at + 1, "u\xcc\x88")) {
            *at = 'v';
            memmove(at + 1, at + 3, strlen(at + 3) + 1);
        }
        char ascii[2048];
        if (!transliterate(f->ascii, roman, ascii, sizeof(ascii)))
            return fail(msg, n, "中文拼音去调失败");
        strcpy(roman, ascii);
    }
    char* save = NULL;
    for (char* word = strtok_r(roman, " ,\t\r\n", &save); word;
         word = strtok_r(NULL, " ,\t\r\n", &save))
        if (!alias(f, p, word, msg, n))
            return 0;
    return p->count > 0 || fail(msg, n, "歌词没有可发音内容");
}
static uint64_t signature(const DseNotePlan* p) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (unsigned i = 0; i < p->count; i++) {
        const unsigned char* s = (const unsigned char*)p->phones[i].symbol;
        do {
            hash = (hash ^ *s) * UINT64_C(1099511628211);
        } while (*s++);
    }
    return hash;
}
static int is_tie(const DseNote* n) {
    return !*n->phonemes && n->lyric[0] == '+' && !strchr(n->lyric, '[');
}
static int before(const DseScore* s, unsigned index) {
    const DseNote* note = &s->notes[index];
    int previous = -1;
    for (unsigned i = 0; i < s->count; i++)
        if (s->notes[i].part == note->part && s->notes[i].pos < note->pos &&
            (previous < 0 || s->notes[i].pos > s->notes[previous].pos))
            previous = (int)i;
    return previous;
}
static int after(const DseScore* s, unsigned index) {
    const DseNote* note = &s->notes[index];
    int next = -1;
    for (unsigned i = 0; i < s->count; i++)
        if (s->notes[i].part == note->part && s->notes[i].pos > note->pos &&
            (next < 0 || s->notes[i].pos < s->notes[next].pos))
            next = (int)i;
    return next;
}
void dse_timing_clear_word(DseScore* s, unsigned index) {
    if (index >= s->count)
        return;
    unsigned source = index;
    while (is_tie(&s->notes[source])) {
        int previous = before(s, source);
        if (previous < 0 || s->notes[previous].pos + s->notes[previous].len != s->notes[source].pos)
            break;
        source = (unsigned)previous;
    }
    for (;;) {
        s->notes[source].timing_slot = 0;
        int next = after(s, source);
        if (next < 0 || !is_tie(&s->notes[next]) ||
            s->notes[source].pos + s->notes[source].len != s->notes[next].pos)
            break;
        source = (unsigned)next;
    }
}
static int allocate(DseFrontend* f, DseNotePlan* p, unsigned begin, unsigned end, int length,
                    char* msg, size_t n) {
    unsigned count = end - begin, vowels = 0;
    if (!count || length < (int)count)
        return fail(msg, n, "音符太短，无法分配至少一帧给每个音素");
    for (unsigned i = begin; i < end; i++)
        vowels += get(f->vowels, p->phones[i].symbol) != NULL;
    unsigned consonants = count - vowels;
    if (vowels) {
        int consonant = consonants ? (length - (int)vowels) / (int)consonants : 0;
        if (consonant > 3)
            consonant = 3;
        int budget = length - consonant * (int)consonants;
        unsigned ordinal = 0;
        for (unsigned i = begin; i < end; i++)
            p->phones[i].frames =
                get(f->vowels, p->phones[i].symbol)
                    ? budget / (int)vowels + ((int)ordinal++ < budget % (int)vowels)
                    : consonant;
    } else
        for (unsigned i = begin; i < end; i++)
            p->phones[i].frames = length / (int)count + ((int)(i - begin) < length % (int)count);
    return 1;
}
typedef struct DseSegment {
    unsigned owner, begin, end;
    int start, base_stop, stop, sustain;
} DseSegment;
typedef struct DseGroup {
    DseNotePlan plan;
    DseSegment segments[DSE_MAX_PHONEMES];
    unsigned count;
} DseGroup;
static void segment_plan(const DseGroup* g, unsigned segment, unsigned index, DseNotePlan* out) {
    const DseSegment* s = &g->segments[segment];
    memset(out, 0, sizeof(*out));
    out->source_index = g->plan.source_index;
    out->owner_index = s->owner;
    out->tied = index != s->owner;
    out->progression = g->count > 1;
    out->start = s->start;
    out->base_stop = s->base_stop;
    out->stop = s->stop;
    out->sustain = s->sustain < 0 ? -1 : s->sustain - (int)s->begin;
    out->count = s->end - s->begin;
    memcpy(out->phones, g->plan.phones + s->begin, out->count * sizeof(*out->phones));
    out->signature = signature(out);
}
static int make_group(DseFrontend* f, const DseScore* s, unsigned index, DseGroup* g, char* msg,
                      size_t n) {
    memset(g, 0, sizeof(*g));
    DseNotePlan* p = &g->plan;
    p->sustain = -1;
    if (!f || index >= s->count)
        return fail(msg, n, "音符或音素前端不可用");
    unsigned source = index;
    while (is_tie(&s->notes[source])) {
        int previous = before(s, source);
        if (previous < 0 || s->notes[previous].pos + s->notes[previous].len != s->notes[source].pos)
            return fail(msg, n, "“+”延音必须紧接同片段的前一个音符");
        source = (unsigned)previous;
    }
    const DseNote* note = &s->notes[source];
    p->source_index = p->owner_index = source;
    if (!resolve(f, note, p, msg, n))
        return 0;
    p->signature = signature(p);
    p->start = dse_frontend_frame(note->pos, s->bpm);
    p->base_stop = dse_frontend_frame(note->pos + note->len, s->bpm);
    unsigned starts[DSE_MAX_PHONEMES] = {0}, targets[DSE_MAX_PHONEMES] = {source};
    unsigned syllables = 0, segments = 1;
    int previous_vowel = -1;
    for (unsigned i = 0; i < p->count; i++)
        if (!silent(p->phones[i].symbol) && get(f->vowels, p->phones[i].symbol)) {
            starts[syllables++] = previous_vowel < 0 ? 0 : (unsigned)previous_vowel + 1;
            previous_vowel = (int)i;
        }
    unsigned tail = source;
    for (;;) {
        int next = after(s, tail);
        if (next < 0 || !is_tie(&s->notes[next]) ||
            s->notes[tail].pos + s->notes[tail].len != s->notes[next].pos)
            break;
        if (p->sustain < 0)
            return fail(msg, n, "前一个歌词没有可延长的元音；请检查声库 symbols 类型或音素覆盖");
        const char* lyric = s->notes[next].lyric;
        /* Each advancing note owns its syllable. Vowel holds share that owner
         * so one timing record governs the articulation and the held nucleus. */
        if (lyric[1] != '~' && lyric[1] != '*' && segments < syllables)
            targets[segments++] = (unsigned)next;
        else if (s->notes[next].timing_slot)
            return fail(msg, n, "元音延音继承所属音节时长，请调整该音节的起始音符");
        tail = (unsigned)next;
    }
    p->stop = dse_frontend_frame(s->notes[tail].pos + s->notes[tail].len, s->bpm);
    p->progression = segments > 1;
    g->count = segments;
    for (unsigned j = 0; j < segments; j++) {
        DseSegment* segment = &g->segments[j];
        segment->owner = targets[j];
        segment->begin = starts[j];
        segment->end = j + 1 < segments ? starts[j + 1] : p->count;
        const DseNote* target = &s->notes[segment->owner];
        segment->start = dse_frontend_frame(target->pos, s->bpm);
        segment->base_stop = dse_frontend_frame(target->pos + target->len, s->bpm);
        segment->stop =
            j + 1 < segments ? dse_frontend_frame(s->notes[targets[j + 1]].pos, s->bpm) : p->stop;
        segment->sustain = -1;
        for (unsigned k = segment->begin; k < segment->end; k++)
            if (!silent(p->phones[k].symbol) && get(f->vowels, p->phones[k].symbol))
                segment->sustain = (int)k;
        const DseTiming* timing = dse_timing_get(s, target);
        if (timing) {
            DseNotePlan current;
            segment_plan(g, j, segment->owner, &current);
            if (!dse_timing_valid(timing, target->len) || timing->count != current.count ||
                timing->signature != current.signature) {
                snprintf(msg, n, "音符 %u 的手工时长与当前音节不匹配，请重置时长或恢复原读音",
                         segment->owner + 1);
                return 0;
            }
            int sum = 0, cursor = 0, last = segment->start;
            for (unsigned k = 0; k < timing->count; k++)
                sum += timing->ticks[k];
            for (unsigned k = 0; k < timing->count; k++) {
                cursor += timing->ticks[k] ? timing->ticks[k] : target->len - sum;
                int stop = dse_frontend_frame(target->pos + cursor, s->bpm);
                p->phones[segment->begin + k].frames = stop - last;
                if (stop <= last) {
                    snprintf(msg, n, "音符 %u 的手工音素时长不足一帧，请加长对应音素",
                             segment->owner + 1);
                    return 0;
                }
                last = stop;
            }
        } else if (!allocate(f, p, segment->begin, segment->end,
                             segment->base_stop - segment->start, msg, n))
            return 0;
        if (segment->stop > segment->base_stop) {
            if (segment->sustain < 0)
                return fail(msg, n, "连音音节没有可延长的元音");
            p->phones[segment->sustain].frames += segment->stop - segment->base_stop;
        }
    }
    return 1;
}
int dse_frontend_note(DseFrontend* f, const DseScore* s, unsigned index, DseNotePlan* p, char* msg,
                      size_t n) {
    memset(p, 0, sizeof(*p));
    p->sustain = -1;
    DseGroup group;
    if (!make_group(f, s, index, &group, msg, n))
        return 0;
    unsigned segment = 0;
    for (unsigned j = 1; j < group.count; j++)
        if (s->notes[group.segments[j].owner].pos <= s->notes[index].pos)
            segment = j;
    segment_plan(&group, segment, index, p);
    return 1;
}
typedef struct NoteRef {
    unsigned index;
    int pos;
} NoteRef;
static int compare(const void* a, const void* b) {
    const NoteRef *x = a, *y = b;
    return (x->pos > y->pos) - (x->pos < y->pos);
}
void dse_part_plan_clear(DsePartPlan* p) {
    free(p->phones);
    memset(p, 0, sizeof(*p));
}
int dse_frontend_part(DseFrontend* f, const DseScore* s, unsigned part, DsePartPlan* out, char* msg,
                      size_t n) {
    memset(out, 0, sizeof(*out));
    NoteRef notes[DSE_MAX_NOTES];
    unsigned count = 0;
    for (unsigned i = 0; i < s->count; i++)
        if (s->notes[i].part == (int)part)
            notes[count++] = (NoteRef){i, s->notes[i].pos};
    if (!count)
        return 1;
    qsort(notes, count, sizeof(*notes), compare);
    int end = 0;
    for (unsigned i = 0; i < count; i++) {
        const DseNote* note = &s->notes[notes[i].index];
        if (note->pos < end)
            return fail(msg, n, "同一歌声片段存在重叠音符，请先分开音符或移至另一轨");
        end = note->pos + note->len;
    }
    out->frames = (unsigned)dse_frontend_frame(end, s->bpm);
    if (!out->frames || out->frames > 51680)
        return fail(msg, n, "单个歌声片段最长支持 10 分钟，请拆分片段");
    out->phones = calloc((size_t)count * (DSE_MAX_PHONEMES + 1), sizeof(*out->phones));
    if (!out->phones)
        return fail(msg, n, "音素计划分配失败");
    int cursor = 0;
    for (unsigned i = 0; i < count; i++) {
        if (cancelled(f)) {
            dse_part_plan_clear(out);
            return fail(msg, n, "合成已取消");
        }
        unsigned index = notes[i].index;
        if (is_tie(&s->notes[index])) {
            if (!i || s->notes[notes[i - 1].index].pos + s->notes[notes[i - 1].index].len !=
                          s->notes[index].pos) {
                snprintf(msg, n, "片段 %u · 第 %u 个音符：“+”延音必须紧接前一个音符", part + 1,
                         i + 1);
                dse_part_plan_clear(out);
                return 0;
            }
            continue;
        }
        DseGroup group;
        if (!make_group(f, s, index, &group, msg, n)) {
            char detail[512];
            snprintf(detail, sizeof(detail), "%s", msg);
            snprintf(msg, n, "片段 %u · 第 %u 个音符“%s”：%s", part + 1, i + 1,
                     s->notes[index].lyric, detail);
            dse_part_plan_clear(out);
            return 0;
        }
        const DseNotePlan* p = &group.plan;
        if (p->start > cursor) {
            DsePhone* silence = &out->phones[out->count++];
            strcpy(silence->symbol, "SP");
            silence->token = json_object_get_int(get(f->vocab, "SP"));
            silence->frames = p->start - cursor;
        }
        for (unsigned j = 0; j < p->count; j++)
            out->phones[out->count++] = p->phones[j];
        cursor = p->stop;
    }
    return 1;
}
int dse_timing_parse(const char* text, const DseScore* s, unsigned index, const DseNotePlan* p,
                     DseTiming* out, char* msg, size_t n) {
    memset(out, 0, sizeof(*out));
    if (!*text)
        return 1;
    if (p->tied || p->owner_index != index)
        return fail(msg, n, "元音延音继承所属音节，请选择该音节起始音符调整时长");
    char buffer[1024];
    if (strlen(text) >= sizeof(buffer))
        return fail(msg, n, "时长输入过长");
    strcpy(buffer, text);
    char* save = NULL;
    for (char* token = strtok_r(buffer, " ,\t\r\n", &save); token;
         token = strtok_r(NULL, " ,\t\r\n", &save)) {
        if (out->count == DSE_MAX_PHONEMES)
            return fail(msg, n, "音素时长数量超过限制");
        int ticks = 0;
        if (strcmp(token, "*")) {
            char* end;
            errno = 0;
            double value = strtod(token, &end);
            double score_ticks = value * s->bpm * DSE_PPQ / 60000;
            if (!*token || *end || errno || !isfinite(value) || value <= 0 || score_ticks < .5 ||
                score_ticks > DSE_TICK_LIMIT)
                return fail(msg, n, "时长需为正数毫秒，并用一个 * 自动填满余下时长");
            ticks = (int)lround(score_ticks);
        }
        out->ticks[out->count++] = ticks;
    }
    out->signature = p->signature;
    if (out->count != p->count || !dse_timing_valid(out, s->notes[index].len))
        return fail(msg, n, "依次填写每个音素的毫秒时长，保留一个 *；合计须小于音符时长");
    int sum = 0, cursor = 0, last = p->start;
    for (unsigned i = 0; i < out->count; i++)
        sum += out->ticks[i];
    for (unsigned i = 0; i < out->count; i++) {
        cursor += out->ticks[i] ? out->ticks[i] : s->notes[index].len - sum;
        int stop = dse_frontend_frame(s->notes[index].pos + cursor, s->bpm);
        if (stop <= last)
            return fail(msg, n, "每个音素至少需要一帧（约 11.61 毫秒）");
        last = stop;
    }
    return 1;
}
void dse_timing_text(const DseScore* s, unsigned index, const DseNotePlan* p, char* out, size_t n) {
    if (n)
        out[0] = 0;
    if (index >= s->count || p->tied || p->owner_index != index || !p->count)
        return;
    const DseTiming* timing = dse_timing_get(s, &s->notes[index]);
    size_t used = 0;
    for (unsigned i = 0; i < p->count; i++) {
        int automatic = timing ? !timing->ticks[i]
                               : (int)i == (p->sustain < 0 ? (int)p->count - 1 : p->sustain);
        double ms = timing ? timing->ticks[i] * 60000.0 / (s->bpm * DSE_PPQ)
                           : p->phones[i].frames * 512000.0 / 44100;
        int count = automatic ? snprintf(out + used, n - used, "%s*", used ? " " : "")
                              : snprintf(out + used, n - used, "%s%.3f", used ? " " : "", ms);
        if (count < 0 || (size_t)count >= n - used)
            return;
        used += (size_t)count;
    }
}
