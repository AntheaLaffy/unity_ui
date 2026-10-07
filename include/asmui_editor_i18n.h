#ifndef ASMUI_EDITOR_I18N_H
#define ASMUI_EDITOR_I18N_H
#include <stddef.h>

typedef enum EditorLocale { EDITOR_ZH_CN, EDITOR_EN_US, EDITOR_LOCALE_COUNT } EditorLocale;
/* English source messages are stable message IDs, as in gettext. Asset paths
   and numeric values remain data; only registered UI captions are translated. */
const char *editor_localize(EditorLocale locale, const char *message);
int editor_locale_parse(const char *name, EditorLocale *out);
const char *editor_locale_name(EditorLocale locale);
const char *const *editor_language_samples(size_t *count);
#endif
