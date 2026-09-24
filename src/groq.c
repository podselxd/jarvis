#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "config.h"
#include "groq.h"
#include "http.h"
#include "log.h"
#include "util.h"

#define GROQ_BASE "https://api.groq.com/openai/v1"

static const char *STT_MODELS[] = {"whisper-large-v3-turbo", "whisper-large-v3"};

/* Frases que Whisper inventa sobre silencio/ruido (aprendidas de subtítulos
   de YouTube). La versión anterior las tomaba como si el usuario las hubiera
   dicho — de ahí los "¡Gracias!" fantasma en el log. */
static const char *HALLUCINATIONS[] = {
    "amara.org", "gracias por ver", "suscríbete", "suscribete", "subtítulos realizados", "subtitulos realizados",
    "subtítulos por la comunidad", "¡gracias por ver", "no olvides suscribirte", "thank you for watching",
};

void groq_error_free(GroqError *e)
{
    if (!e) return;
    free(e->detail);
    e->detail = NULL;
}

static void set_error(GroqError *err, GroqStatus st, const HttpResponse *r, const char *detail)
{
    if (!err) return;
    free(err->detail);
    err->status = st;
    err->http_status = r ? r->status : 0;
    err->retry_after = r ? r->retry_after : -1;
    err->detail = xstrdup(detail ? detail : "");
}

static GroqStatus classify(const HttpResponse *r)
{
    if (r->status == 0) return GROQ_NETWORK_ERROR;
    if (r->status == 200) return GROQ_OK;
    if (r->status == 401 || r->status == 403) return GROQ_AUTH_ERROR;
    if (r->status == 429) return GROQ_RATE_LIMITED;
    if (r->status >= 500) return GROQ_SERVER_ERROR;
    return GROQ_BAD_RESPONSE;
}

static char *error_detail(const HttpResponse *r)
{
    if (r->error) return xstrdup(r->error);
    cJSON *j = cJSON_Parse(r->body);
    char *msg = NULL;
    if (j) {
        cJSON *e = cJSON_GetObjectItem(j, "error");
        cJSON *m = e ? cJSON_GetObjectItem(e, "message") : NULL;
        if (cJSON_IsString(m)) msg = xstrdup(m->valuestring);
        cJSON_Delete(j);
    }
    if (!msg) msg = str_printf("HTTP %d", r->status);
    return msg;
}

unsigned char *wav_encode(const int16_t *pcm, size_t samples, int sample_rate, size_t *out_len)
{
    size_t data_len = samples * 2;
    size_t total = 44 + data_len;
    unsigned char *w = xmalloc(total);
    uint32_t v32;
    uint16_t v16;
    memcpy(w, "RIFF", 4);
    v32 = (uint32_t)(36 + data_len);
    memcpy(w + 4, &v32, 4);
    memcpy(w + 8, "WAVEfmt ", 8);
    v32 = 16;
    memcpy(w + 16, &v32, 4);
    v16 = 1;
    memcpy(w + 20, &v16, 2);
    memcpy(w + 22, &v16, 2);
    v32 = (uint32_t)sample_rate;
    memcpy(w + 24, &v32, 4);
    v32 = (uint32_t)sample_rate * 2;
    memcpy(w + 28, &v32, 4);
    v16 = 2;
    memcpy(w + 32, &v16, 2);
    v16 = 16;
    memcpy(w + 34, &v16, 2);
    memcpy(w + 36, "data", 4);
    v32 = (uint32_t)data_len;
    memcpy(w + 40, &v32, 4);
    memcpy(w + 44, pcm, data_len);
    *out_len = total;
    return w;
}

static char *auth_header(const char *extra)
{
    char *key = config_api_key();
    char *h = str_printf("Authorization: Bearer %s\r\n%s", key, extra ? extra : "");
    SecureZeroMemory(key, strlen(key));
    free(key);
    return h;
}

static bool is_hallucination(const char *text)
{
    char *low = str_lower(text);
    bool bad = false;
    for (size_t i = 0; i < sizeof HALLUCINATIONS / sizeof *HALLUCINATIONS && !bad; i++)
        bad = strstr(low, HALLUCINATIONS[i]) != NULL;
    free(low);
    return bad;
}

/* verbose_json trae por segmento la probabilidad de "no había voz": se
   descartan los segmentos que el propio Whisper marca como silencio, con los
   mismos umbrales que usa Whisper por defecto (0.6 / -1.0). */
static char *text_from_verbose(cJSON *j)
{
    cJSON *segs = cJSON_GetObjectItem(j, "segments");
    if (!cJSON_IsArray(segs)) {
        cJSON *t = cJSON_GetObjectItem(j, "text");
        return str_trim(cJSON_IsString(t) ? t->valuestring : "");
    }
    StrBuf sb;
    sb_init(&sb);
    cJSON *seg;
    cJSON_ArrayForEach(seg, segs)
    {
        cJSON *t = cJSON_GetObjectItem(seg, "text");
        cJSON *nsp = cJSON_GetObjectItem(seg, "no_speech_prob");
        cJSON *lp = cJSON_GetObjectItem(seg, "avg_logprob");
        if (!cJSON_IsString(t)) continue;
        double no_speech = cJSON_IsNumber(nsp) ? nsp->valuedouble : 0;
        double logprob = cJSON_IsNumber(lp) ? lp->valuedouble : 0;
        if (no_speech > 0.6 && logprob < -1.0) continue;
        if (is_hallucination(t->valuestring)) continue;
        sb_append(&sb, t->valuestring);
        sb_append_char(&sb, ' ');
    }
    char *raw = sb_steal(&sb);
    char *r = str_trim(raw ? raw : "");
    free(raw);
    str_collapse_spaces(r);
    return r;
}

char *groq_transcribe(const int16_t *pcm, size_t samples, int sample_rate, GroqError *err)
{
    size_t wav_len;
    unsigned char *wav = wav_encode(pcm, samples, sample_rate, &wav_len);
    char boundary[48];
    unsigned char rnd[12];
    random_bytes(rnd, sizeof rnd);
    char *hex = hex_encode(rnd, sizeof rnd);
    snprintf(boundary, sizeof boundary, "----sokari%s", hex);
    free(hex);

    char *result = NULL;
    for (size_t m = 0; m < sizeof STT_MODELS / sizeof *STT_MODELS; m++) {
        StrBuf body;
        sb_init(&body);
        sb_appendf(&body,
                   "--%s\r\nContent-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n"
                   "Content-Type: audio/wav\r\n\r\n",
                   boundary);
        sb_append_n(&body, (const char *)wav, wav_len);
        sb_appendf(&body, "\r\n--%s\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\n%s\r\n", boundary,
                   STT_MODELS[m]);
        sb_appendf(&body, "--%s\r\nContent-Disposition: form-data; name=\"language\"\r\n\r\nes\r\n", boundary);
        sb_appendf(&body,
                   "--%s\r\nContent-Disposition: form-data; name=\"response_format\"\r\n\r\nverbose_json\r\n", boundary);
        sb_appendf(&body, "--%s\r\nContent-Disposition: form-data; name=\"temperature\"\r\n\r\n0\r\n", boundary);
        sb_appendf(&body, "--%s--\r\n", boundary);

        char *ctype = str_printf("Content-Type: multipart/form-data; boundary=%s\r\n", boundary);
        char *headers = auth_header(ctype);
        free(ctype);
        HttpRequest req = {.method = "POST", .url = GROQ_BASE "/audio/transcriptions", .headers = headers,
                           .body = body.data, .body_len = body.len, .timeout_ms = 60000};
        HttpResponse r = http_request(&req);
        free(headers);
        sb_free(&body);

        GroqStatus st = classify(&r);
        if (st == GROQ_OK) {
            cJSON *j = cJSON_Parse(r.body);
            if (j) {
                result = text_from_verbose(j);
                cJSON_Delete(j);
            } else {
                set_error(err, GROQ_BAD_RESPONSE, &r, "respuesta de transcripción ilegible");
            }
            http_response_free(&r);
            break;
        }
        char *detail = error_detail(&r);
        log_msg("Groq STT (%s) falló: %s", STT_MODELS[m], detail);
        set_error(err, st, &r, detail);
        free(detail);
        http_response_free(&r);
        if (st != GROQ_RATE_LIMITED && st != GROQ_SERVER_ERROR) break;
    }
    free(wav);
    return result;
}

static cJSON *clean_message(cJSON *msg)
{
    cJSON *out = cJSON_CreateObject();
    cJSON_AddStringToObject(out, "role", "assistant");
    cJSON *content = cJSON_GetObjectItem(msg, "content");
    if (cJSON_IsString(content)) cJSON_AddStringToObject(out, "content", content->valuestring);
    else cJSON_AddStringToObject(out, "content", "");
    cJSON *calls = cJSON_GetObjectItem(msg, "tool_calls");
    if (cJSON_IsArray(calls) && cJSON_GetArraySize(calls) > 0) {
        cJSON *clean_calls = cJSON_AddArrayToObject(out, "tool_calls");
        cJSON *c;
        cJSON_ArrayForEach(c, calls)
        {
            cJSON *id = cJSON_GetObjectItem(c, "id");
            cJSON *fn = cJSON_GetObjectItem(c, "function");
            cJSON *name = fn ? cJSON_GetObjectItem(fn, "name") : NULL;
            cJSON *args = fn ? cJSON_GetObjectItem(fn, "arguments") : NULL;
            if (!cJSON_IsString(id) || !cJSON_IsString(name)) continue;
            cJSON *cc = cJSON_CreateObject();
            cJSON_AddStringToObject(cc, "id", id->valuestring);
            cJSON_AddStringToObject(cc, "type", "function");
            cJSON *f = cJSON_AddObjectToObject(cc, "function");
            cJSON_AddStringToObject(f, "name", name->valuestring);
            if (cJSON_IsString(args)) cJSON_AddStringToObject(f, "arguments", args->valuestring);
            else if (args) {
                char *s = cJSON_PrintUnformatted(args);
                cJSON_AddStringToObject(f, "arguments", s ? s : "{}");
                free(s);
            } else cJSON_AddStringToObject(f, "arguments", "{}");
            cJSON_AddItemToArray(clean_calls, cc);
        }
    }
    return out;
}

typedef struct {
    const char *id;
    bool qwen;
    double cool_until;   /* no usar antes de este momento (epoch) */
    int remaining;       /* último cupo de tokens informado por Groq, -1 = desconocido */
    double measured_at;
    double reset_secs;
} ChatModel;

/* Cada modelo gratis de Groq tiene su propio cupo de 8000 tokens por minuto:
   rotar entre los tres triplica lo que Sokari puede contestar seguido. */
static ChatModel MODELS[] = {
    {"openai/gpt-oss-120b", false, 0, -1, 0, 0},
    {"qwen/qwen3.8-27b", true, 0, -1, 0, 0},
    {"openai/gpt-oss-20b", false, 0, -1, 0, 0},
};

#define TOKEN_LIMIT 8000.0
#define EST_REQUEST_TOKENS 2600.0
#define MAX_SILENT_WAIT 12.0

static bool model_ready(const ChatModel *m, double now, double *wait)
{
    if (now < m->cool_until) {
        *wait = m->cool_until - now;
        return false;
    }
    if (m->remaining >= 0 && m->remaining < EST_REQUEST_TOKENS && m->reset_secs > 0) {
        double needed = m->reset_secs * (EST_REQUEST_TOKENS - m->remaining) / (TOKEN_LIMIT - m->remaining + 1.0);
        double ready = m->measured_at + needed;
        if (now < ready) {
            *wait = ready - now;
            return false;
        }
    }
    return true;
}

static void remember_limits(ChatModel *m, const HttpResponse *r)
{
    double now = now_epoch();
    if (r->rl_remaining_tokens >= 0) {
        m->remaining = r->rl_remaining_tokens;
        m->reset_secs = r->rl_reset_tokens > 0 ? r->rl_reset_tokens : 60;
        m->measured_at = now;
    }
    if (r->status == 429) m->cool_until = now + (r->retry_after > 0 ? r->retry_after : 6);
}

static cJSON *chat_once(ChatModel *model, const cJSON *messages, const cJSON *tools, GroqError *err, bool *retryable)
{
    *retryable = false;
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "model", model->id);
    cJSON_AddItemToObject(req, "messages", cJSON_Duplicate(messages, 1));
    if (tools) cJSON_AddItemToObject(req, "tools", cJSON_Duplicate(tools, 1));
    cJSON_AddNumberToObject(req, "max_completion_tokens", 1024);
    if (model->qwen) {
        cJSON_AddStringToObject(req, "reasoning_effort", "none");
        cJSON_AddStringToObject(req, "reasoning_format", "hidden");
    } else {
        cJSON_AddStringToObject(req, "reasoning_effort", "low");
        cJSON_AddFalseToObject(req, "include_reasoning");
    }
    char *payload = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);

    char *headers = auth_header("Content-Type: application/json\r\n");
    HttpRequest hr = {.method = "POST", .url = GROQ_BASE "/chat/completions", .headers = headers, .body = payload,
                      .body_len = strlen(payload), .timeout_ms = 60000};
    HttpResponse r = http_request(&hr);
    free(headers);
    free(payload);
    remember_limits(model, &r);

    GroqStatus st = classify(&r);
    cJSON *result = NULL;
    if (st == GROQ_OK) {
        cJSON *j = cJSON_Parse(r.body);
        cJSON *choices = j ? cJSON_GetObjectItem(j, "choices") : NULL;
        cJSON *first = cJSON_IsArray(choices) ? cJSON_GetArrayItem(choices, 0) : NULL;
        cJSON *msg = first ? cJSON_GetObjectItem(first, "message") : NULL;
        if (cJSON_IsObject(msg)) result = clean_message(msg);
        else set_error(err, GROQ_BAD_RESPONSE, &r, "Groq no mandó respuesta");
        cJSON_Delete(j);
    } else {
        char *detail = error_detail(&r);
        log_msg("Groq chat (%s) HTTP %d: %s", model->id, r.status, detail);
        set_error(err, st, &r, detail);
        /* gpt-oss a veces arma mal una tool call y Groq devuelve 400
           ("tool_use_failed" / "Failed to parse tool call"): con otro modelo
           o reintentando suele salir bien. */
        *retryable = st == GROQ_RATE_LIMITED || st == GROQ_SERVER_ERROR || st == GROQ_NETWORK_ERROR ||
                     (st == GROQ_BAD_RESPONSE && (strstr(r.body, "tool_use_failed") || strstr(r.body, "tool call")));
        free(detail);
    }
    http_response_free(&r);
    return result;
}

cJSON *groq_chat(const cJSON *messages, const cJSON *tools, GroqError *err)
{
    const int n = (int)(sizeof MODELS / sizeof *MODELS);
    for (int attempt = 0; attempt < 4; attempt++) {
        double now = now_epoch(), min_wait = 1e9;
        for (int i = 0; i < n; i++) {
            double wait = 0;
            if (!model_ready(&MODELS[i], now, &wait)) {
                if (wait < min_wait) min_wait = wait;
                continue;
            }
            bool retryable = false;
            cJSON *msg = chat_once(&MODELS[i], messages, tools, err, &retryable);
            if (msg) {
                if (err) err->status = GROQ_OK;
                return msg;
            }
            if (!retryable) return NULL;
            now = now_epoch();
            double w = MODELS[i].cool_until - now;
            if (w > 0 && w < min_wait) min_wait = w;
        }
        if (min_wait > MAX_SILENT_WAIT) {
            if (err) {
                err->status = GROQ_RATE_LIMITED;
                err->retry_after = (int)(min_wait + 0.999);
            }
            return NULL;
        }
        if (min_wait < 1e8 && min_wait > 0) {
            log_msg("Los tres modelos sin cupo; espero %.1fs.", min_wait);
            app_status("Esperando cupo de Groq…");
            Sleep((DWORD)(min_wait * 1000) + 250);
        }
    }
    return NULL;
}
