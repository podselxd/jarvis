#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "resources.h"
#include "util.h"

const void *res_data(int id, size_t *len)
{
    HRSRC r = FindResourceW(NULL, MAKEINTRESOURCEW(id), (LPCWSTR)RT_RCDATA);
    if (!r) return NULL;
    HGLOBAL g = LoadResource(NULL, r);
    if (!g) return NULL;
    if (len) *len = SizeofResource(NULL, r);
    return LockResource(g);
}

char *res_string(int id)
{
    size_t n = 0;
    const char *p = res_data(id, &n);
    if (!p) return xstrdup("");
    return xstrndup(p, n);
}
