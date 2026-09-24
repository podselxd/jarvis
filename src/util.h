#ifndef SOKARI_UTIL_H
#define SOKARI_UTIL_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} StrBuf;

void sb_init(StrBuf *sb);
void sb_free(StrBuf *sb);
void sb_reserve(StrBuf *sb, size_t extra);
void sb_append(StrBuf *sb, const char *s);
void sb_append_n(StrBuf *sb, const char *s, size_t n);
void sb_append_char(StrBuf *sb, char c);
void sb_appendf(StrBuf *sb, const char *fmt, ...) __attribute__((format(gnu_printf, 2, 3)));
void sb_vappendf(StrBuf *sb, const char *fmt, va_list ap);
char *sb_steal(StrBuf *sb);

void *xmalloc(size_t n);
void *xcalloc(size_t count, size_t size);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);
wchar_t *xwcsdup(const wchar_t *s);
char *str_printf(const char *fmt, ...) __attribute__((format(gnu_printf, 1, 2)));

wchar_t *utf8_to_wide(const char *s);
char *wide_to_utf8(const wchar_t *w);
char *wide_n_to_utf8(const wchar_t *w, int n);

char *str_lower(const char *s);
bool str_contains_ci(const char *haystack, const char *needle);
bool str_starts_with(const char *s, const char *prefix);
bool str_ends_with(const char *s, const char *suffix);
bool str_eq_ci(const char *a, const char *b);
char *str_trim(const char *s);
bool str_is_blank(const char *s);
void str_collapse_spaces(char *s);
size_t utf8_truncate_len(const char *s, size_t max_bytes);

char *read_file_all(const wchar_t *path, size_t *out_len);
bool write_file_atomic(const wchar_t *path, const void *data, size_t len);
bool append_file(const wchar_t *path, const void *data, size_t len);
bool file_exists(const wchar_t *path);
bool dir_exists(const wchar_t *path);
bool ensure_dir(const wchar_t *path);
wchar_t *path_join(const wchar_t *a, const wchar_t *b);
wchar_t *path_dirname(const wchar_t *p);
const wchar_t *path_basename(const wchar_t *p);
wchar_t *exe_path(void);
wchar_t *exe_dir(void);
wchar_t *expand_env(const wchar_t *s);

double now_epoch(void);
uint64_t now_ms(void);
char *local_iso_now(void);
char *format_epoch_local(double ts, const char *fmt);
bool parse_iso_local(const char *s, double *out_epoch);

bool secure_equal(const char *a, const char *b);
void random_bytes(void *buf, size_t n);
char *sha256_hex(const char *text);
char *hex_encode(const unsigned char *data, size_t n);

#endif
