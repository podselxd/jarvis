/* Tus dispositivos de la malla (dispositivos.json) y cómo se nombran al
   hablar ("dile a cloe", "abre Spotify en mi laptop"). No depende del
   sistema: la red y Tailscale están en mesh.c. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "intents.h"
#include "memory.h"
#include "mesh.h"
#include "util.h"

bool mesh_is_tailscale_v4(const unsigned char b[4])
{
    return b[0] == 100 && b[1] >= 64 && b[1] <= 127;
}

/* Solo direcciones de Tailscale: una IP 100.64.0.0/10 o un nombre MagicDNS
   *.ts.net. Así el secreto de la malla (que viaja en cada pedido) nunca sale
   hacia una dirección cualquiera que el modelo haya registrado. */
bool mesh_host_allowed(const char *host)
{
    unsigned v[4];
    char extra;
    if (sscanf(host, "%u.%u.%u.%u%c", &v[0], &v[1], &v[2], &v[3], &extra) == 4) {
        if (v[0] > 255 || v[1] > 255 || v[2] > 255 || v[3] > 255) return false;
        unsigned char b[4] = {(unsigned char)v[0], (unsigned char)v[1], (unsigned char)v[2], (unsigned char)v[3]};
        return mesh_is_tailscale_v4(b);
    }
    size_t n = strlen(host);
    if (n <= 7 || n > 253) return false;
    for (const char *p = host; *p; p++)
        if (!(isalnum((unsigned char)*p) || *p == '.' || *p == '-')) return false;
    char *low = str_lower(host);
    bool ok = host[0] != '.' && host[0] != '-' && !strstr(host, "..") && str_ends_with(low, ".ts.net");
    free(low);
    return ok;
}

/* ------------------------------------------------------- dispositivos --- */

cJSON *mesh_devices_load(void)
{
    wchar_t *df = local_file(L"dispositivos.json");
    cJSON *devs = json_load_object(df);
    free(df);
    return devs;
}

static void devices_save(cJSON *devs)
{
    wchar_t *df = local_file(L"dispositivos.json");
    json_save(df, devs);
    free(df);
}

int mesh_devices(MeshDevice **out)
{
    cJSON *devs = mesh_devices_load();
    int n = 0, cap = cJSON_GetArraySize(devs);
    MeshDevice *list = xcalloc((size_t)cap + 1, sizeof *list);
    cJSON *d;
    cJSON_ArrayForEach(d, devs)
    {
        if (!cJSON_IsString(d) || !d->string) continue;
        list[n].name = xstrdup(d->string);
        list[n].host = xstrdup(d->valuestring);
        n++;
    }
    cJSON_Delete(devs);
    *out = list;
    return n;
}

void mesh_devices_free(MeshDevice *list, int n)
{
    for (int i = 0; i < n; i++) {
        free(list[i].name);
        free(list[i].host);
    }
    free(list);
}

bool mesh_device_set(const char *name, const char *host)
{
    if (!*name || !mesh_host_allowed(host)) return false;
    cJSON *devs = mesh_devices_load();
    cJSON_DeleteItemFromObject(devs, name);
    cJSON_AddStringToObject(devs, name, host);
    devices_save(devs);
    cJSON_Delete(devs);
    return true;
}

bool mesh_device_remove(const char *name)
{
    cJSON *devs = mesh_devices_load();
    bool had = cJSON_GetObjectItem(devs, name) != NULL;
    cJSON_DeleteItemFromObject(devs, name);
    if (had) devices_save(devs);
    cJSON_Delete(devs);
    return had;
}

/* "la laptop", "la otra compu": otra PC, sin decir cuál (nunca "mi pc" a
   secas, que casi siempre es esta). */
static bool says_other_pc(const char *norm)
{
    static const char *const W[] = {" laptop ",       " laptops ",     " lap ",        " portatil ",
                                    " notebook ",     " otra pc ",     " otra compu ", " otra computadora ",
                                    " otra maquina ", " otro equipo ", " la de alla "};
    for (size_t i = 0; i < sizeof W / sizeof *W; i++)
        if (strstr(norm, W[i])) return true;
    return false;
}

/* ¿norm (normalizado, con espacios a los lados) nombra a ese dispositivo?
   Por su nombre completo ("laptop ismael") o por cómo suena, si es de una
   palabra ("chloe" es "cloe"). */
static bool names_device(const char *norm, const char *device)
{
    char *dn = intents_normalize(device); /* " laptop ismael " */
    bool hit = strlen(dn) > 2 && strstr(norm, dn) != NULL;
    char *d = str_trim(dn);
    free(dn);
    if (!hit && !strchr(d, ' ') && strlen(d) >= 3) {
        const char *p = norm;
        while (*p && !hit) {
            while (*p == ' ') p++;
            const char *e = strchr(p, ' ');
            if (!e) break;
            char w[40];
            size_t len = (size_t)(e - p);
            if (len >= 3 && len < sizeof w) {
                memcpy(w, p, len);
                w[len] = 0;
                hit = intents_sounds_like(w, d);
            }
            p = e;
        }
    }
    free(d);
    return hit;
}

/* NULL si el texto no habla de otra PC; "" si sí, pero no se sabe cuál (no
   hay ninguna registrada, o hay varias y no dijo el nombre); si no, el
   nombre registrado. */
static char *device_in(const char *norm)
{
    MeshDevice *devs;
    int nd = mesh_devices(&devs);
    char *found = NULL;
    int hits = 0;
    for (int i = 0; i < nd; i++) {
        if (!names_device(norm, devs[i].name)) continue;
        hits++;
        free(found);
        found = xstrdup(devs[i].name);
    }
    if (hits > 1) {
        free(found);
        found = xstrdup("");
    }
    if (!found && says_other_pc(norm)) found = xstrdup(nd == 1 ? devs[0].name : "");
    mesh_devices_free(devs, nd);
    return found;
}

char *mesh_device_mentioned(const char *text)
{
    char *norm = intents_normalize(text);
    char *r = device_in(norm);
    free(norm);
    return r;
}

char *mesh_resolve_device(const char *spoken)
{
    char *r = mesh_device_mentioned(spoken);
    if (r && !*r) {
        free(r);
        r = NULL;
    }
    return r;
}

char *mesh_device_name_for_ip(const char *ip)
{
    cJSON *devs = mesh_devices_load();
    char *name = NULL;
    cJSON *d;
    cJSON_ArrayForEach(d, devs)
    {
        if (!name && cJSON_IsString(d) && !strcmp(d->valuestring, ip)) name = xstrdup(d->string);
    }
    cJSON_Delete(devs);
    return name;
}
