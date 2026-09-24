/* Ventana del HUD (la esfera) + ventana oculta de mensajes (bandeja, atajo de
   teclado, avisos). La esfera se dibuja en su propio hilo, sincronizado con el
   refresco del monitor; el hilo de la interfaz solo maneja mensajes.
   La ventana nunca toma el foco (WS_EX_NOACTIVATE): las teclas que manda
   Jarvis siempre llegan a la app que estás usando, no a la esfera. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "config.h"
#include "log.h"
#include "resource.h"
#include "sphere.h"
#include "ui.h"
#include "util.h"
#include "voice.h"

#define HUD_CLASS L"JarvisHUD"
#define HOTKEY_TALK 1
#define SUBTITLE_SECONDS 8.0
#define ORB_FRACTION 0.40f

static struct {
    HINSTANCE inst;
    HWND msg, hud;
    SettingsSavedFn on_saved;
    UINT taskbar_created;

    int mode, style, res;
    bool subtitles;
    volatile LONG visible;
    volatile LONG yielded;
    RECT mon;
    int orb_size;

    HANDLE thread, wake;
    volatile LONG running, reconfig;
    SRWLOCK hud_lock;

    SRWLOCK text_lock;
    wchar_t *sub_user, *sub_jarvis, *status;
    double sub_time;

    volatile LONG state;
    volatile LONG level_milli;
} U = {.hud_lock = SRWLOCK_INIT, .text_lock = SRWLOCK_INIT};

/* ---------------------------------------------------------- puente app --- */

void app_set_state(JvState s)
{
    LONG prev = InterlockedExchange(&U.state, (LONG)s);
    if (U.wake) SetEvent(U.wake);
    if (s == JV_LISTENING && prev != JV_LISTENING && U.msg) PostMessageW(U.msg, WM_APP_SHOWHUD, 2, 0);
}

JvState app_get_state(void)
{
    return (JvState)InterlockedCompareExchange(&U.state, 0, 0);
}

void app_set_level(float level)
{
    if (level < 0) level = 0;
    if (level > 1) level = 1;
    InterlockedExchange(&U.level_milli, (LONG)(level * 1000));
}

static void set_text(wchar_t **field, const char *text)
{
    AcquireSRWLockExclusive(&U.text_lock);
    free(*field);
    *field = text && *text ? utf8_to_wide(text) : NULL;
    U.sub_time = now_epoch();
    ReleaseSRWLockExclusive(&U.text_lock);
}

void app_subtitle(bool from_user, const char *text)
{
    if (from_user) {
        set_text(&U.sub_user, text);
        set_text(&U.sub_jarvis, NULL);
    } else {
        set_text(&U.sub_jarvis, text);
    }
}

void app_status(const char *text)
{
    set_text(&U.status, text);
}

void app_notify(const char *title, const char *text)
{
    if (!U.msg) {
        log_msg("[aviso] %s: %s", title, text);
        return;
    }
    wchar_t **pair = xmalloc(sizeof(wchar_t *) * 2);
    pair[0] = utf8_to_wide(title);
    pair[1] = utf8_to_wide(text);
    if (!PostMessageW(U.msg, WM_APP_NOTIFY, 0, (LPARAM)pair)) {
        free(pair[0]);
        free(pair[1]);
        free(pair);
    }
}

bool app_is_own_window(HWND h)
{
    return h && (h == U.hud || h == U.msg || h == settings_window());
}

/* En "Pantalla completa" la esfera está siempre encima: antes de que Jarvis
   abra algo o mande teclas se aparta, para que se vea el resultado. Vuelve
   sola la próxima vez que le hables. */
void app_yield_focus(void)
{
    if (U.msg) SendMessageTimeoutW(U.msg, WM_APP_YIELD, 0, 0, SMTO_ABORTIFHUNG, 1000, NULL);
}

void app_request_quit(void)
{
    if (U.msg) PostMessageW(U.msg, WM_APP_QUIT, 0, 0);
}

HWND ui_message_window(void)
{
    return U.msg;
}

HICON ui_app_icon(int size)
{
    return (HICON)LoadImageW(U.inst, MAKEINTRESOURCEW(IDI_JARVIS), IMAGE_ICON, size, size, LR_DEFAULTCOLOR);
}

/* ------------------------------------------------------------- ventana --- */

static void primary_monitor(RECT *out)
{
    HMONITOR m = MonitorFromPoint((POINT){0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = {sizeof mi};
    GetMonitorInfoW(m, &mi);
    *out = mi.rcMonitor;
}

static void load_display_config(void)
{
    AppConfig c = config_snapshot();
    U.mode = c.display_mode;
    U.style = c.sphere_style;
    U.res = c.resolution;
    U.subtitles = c.subtitles;
    primary_monitor(&U.mon);
    int mh = U.mon.bottom - U.mon.top;
    U.orb_size = (int)(mh * ORB_FRACTION) & ~3;
    if (U.mode == DISPLAY_WINDOWED_BORDERLESS) {
        RECT wa;
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        int x = c.orb_x, y = c.orb_y;
        if (x < 0 || y < 0) {
            x = wa.right - U.orb_size - mh / 40;
            y = wa.bottom - U.orb_size - mh / 40;
        }
        U.mon = (RECT){x, y, x + U.orb_size, y + U.orb_size};
    }
    config_free(&c);
}

static void create_hud(void)
{
    DWORD ex = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    if (U.mode == DISPLAY_FULLSCREEN || U.mode == DISPLAY_WINDOWED_BORDERLESS) ex |= WS_EX_TOPMOST;
    if (U.mode == DISPLAY_WINDOWED_BORDERLESS) ex |= WS_EX_LAYERED;
    U.hud = CreateWindowExW(ex, HUD_CLASS, L"Jarvis", WS_POPUP, U.mon.left, U.mon.top, U.mon.right - U.mon.left,
                            U.mon.bottom - U.mon.top, NULL, NULL, U.inst, NULL);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(U.hud, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof dark);
    if (InterlockedCompareExchange(&U.visible, 1, 1)) ShowWindow(U.hud, SW_SHOWNOACTIVATE);
}

static void rebuild_hud(void)
{
    AcquireSRWLockExclusive(&U.hud_lock);
    if (U.hud) DestroyWindow(U.hud);
    U.hud = NULL;
    load_display_config();
    create_hud();
    InterlockedExchange(&U.reconfig, 1);
    ReleaseSRWLockExclusive(&U.hud_lock);
    SetEvent(U.wake);
}

void ui_config_changed(void)
{
    if (U.msg) PostMessageW(U.msg, WM_APP_CONFIG, 0, 0);
}

static void show_hud(bool show)
{
    InterlockedExchange(&U.visible, show ? 1 : 0);
    InterlockedExchange(&U.yielded, 0);
    if (U.hud) ShowWindow(U.hud, show ? SW_SHOWNOACTIVATE : SW_HIDE);
    SetEvent(U.wake);
}

static LRESULT CALLBACK hud_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_NCHITTEST:
        if (U.mode == DISPLAY_WINDOWED_BORDERLESS) return HTCAPTION;
        break;
    case WM_NCLBUTTONDBLCLK:
    case WM_LBUTTONDBLCLK:
        show_hud(false);
        return 0;
    case WM_NCRBUTTONUP:
    case WM_RBUTTONUP:
        tray_show_menu(U.msg, true, config_mic_muted());
        return 0;
    case WM_EXITSIZEMOVE: {
        RECT r;
        GetWindowRect(h, &r);
        config_set_orb_pos(r.left, r.top);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        if (U.mode != DISPLAY_WINDOWED_BORDERLESS) FillRect(dc, &ps.rcPaint, (HBRUSH)GetStockObject(BLACK_BRUSH));
        EndPaint(h, &ps);
        SetEvent(U.wake);
        return 0;
    }
    }
    return DefWindowProcW(h, m, w, l);
}

/* ------------------------------------------------------------- render --- */

typedef struct {
    HDC dc;
    HBITMAP bmp, old;
    uint32_t *px;
    int w, h;
} Surface;

static void surface_free(Surface *s)
{
    if (s->dc) {
        SelectObject(s->dc, s->old);
        DeleteObject(s->bmp);
        DeleteDC(s->dc);
    }
    memset(s, 0, sizeof *s);
}

static bool surface_alloc(Surface *s, int w, int h)
{
    surface_free(s);
    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    s->bmp = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!s->bmp) return false;
    s->dc = CreateCompatibleDC(NULL);
    s->old = SelectObject(s->dc, s->bmp);
    s->px = bits;
    s->w = w;
    s->h = h;
    memset(bits, 0, sizeof(uint32_t) * (size_t)w * h);
    return true;
}

static HFONT make_font(int px, int weight)
{
    return CreateFontW(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI Variable Display");
}

static void draw_text_block(HDC dc, const wchar_t *text, RECT *area, HFONT font, COLORREF color, int max_lines)
{
    SelectObject(dc, font);
    RECT calc = *area;
    DrawTextW(dc, text, -1, &calc, DT_CENTER | DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    int max_h = tm.tmHeight * max_lines;
    int h = calc.bottom - calc.top;
    if (h > max_h) h = max_h;
    RECT r = {area->left, area->bottom - h, area->right, area->bottom};
    RECT shadow = r;
    OffsetRect(&shadow, 0, 2);
    SetTextColor(dc, RGB(0, 0, 0));
    DrawTextW(dc, text, -1, &shadow, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS | DT_EDITCONTROL);
    SetTextColor(dc, color);
    DrawTextW(dc, text, -1, &r, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS | DT_EDITCONTROL);
    area->bottom = r.top - tm.tmHeight / 3;
}

static void draw_overlay(Surface *back, HFONT big, HFONT small, HFONT status_font)
{
    int W = back->w, H = back->h;
    RECT area = {W / 8, (int)(H * 0.70), W - W / 8, (int)(H * 0.955)};
    AcquireSRWLockShared(&U.text_lock);
    bool fresh = now_epoch() - U.sub_time < SUBTITLE_SECONDS || InterlockedCompareExchange(&U.state, 0, 0) != JV_IDLE;
    SetBkMode(back->dc, TRANSPARENT);
    if (U.status && *U.status) draw_text_block(back->dc, U.status, &(RECT){area.left, area.top, area.right, (int)(H * 0.975)}, status_font, RGB(0x9d, 0x95, 0xff), 1);
    if (U.subtitles && fresh) {
        if (U.sub_jarvis) draw_text_block(back->dc, U.sub_jarvis, &area, big, RGB(0xec, 0xe8, 0xff), 3);
        if (U.sub_user) draw_text_block(back->dc, U.sub_user, &area, small, RGB(0x8f, 0x93, 0xb3), 2);
    }
    ReleaseSRWLockShared(&U.text_lock);
}

static void target_params(JvState st, SphereParams *out)
{
    switch (st) {
    case JV_SPEAKING:
        *out = SPHERE_SPEAK;
        break;
    case JV_THINKING:
        sphere_lerp(out, &SPHERE_IDLE, &SPHERE_SPEAK, 0.5f);
        out->rotation_speed = 0.55f;
        break;
    case JV_LISTENING:
        *out = SPHERE_IDLE;
        out->rotation_speed = 0.22f;
        out->ripple = 0.30f;
        out->glow = 10.0f;
        break;
    default:
        *out = SPHERE_IDLE;
    }
}

static DWORD WINAPI render_main(LPVOID arg)
{
    SphereRenderer *sr = NULL;
    Surface sphere = {0}, back = {0};
    HFONT big = NULL, small = NULL, status_font = NULL;
    SphereParams cur = SPHERE_IDLE;
    double angle = 0, voice_t = 0, env = 0;
    LARGE_INTEGER freq, last, start;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
    last = start;
    InterlockedExchange(&U.reconfig, 1);
    int frame = 0;

    while (InterlockedCompareExchange(&U.running, 1, 1)) {
        if (!InterlockedCompareExchange(&U.visible, 1, 1) || InterlockedCompareExchange(&U.yielded, 1, 1)) {
            WaitForSingleObject(U.wake, 300);
            QueryPerformanceCounter(&last);
            continue;
        }
        AcquireSRWLockShared(&U.hud_lock);
        HWND hud = U.hud;
        if (InterlockedExchange(&U.reconfig, 0) || !sr) {
            int mh = U.mon.bottom - U.mon.top, mw = U.mon.right - U.mon.left;
            int canvas = U.mode == DISPLAY_WINDOWED_BORDERLESS ? U.orb_size : (U.res > 0 ? U.res : mh);
            if (canvas > 2160) canvas = 2160;
            sphere_destroy(sr);
            sr = sphere_create(canvas);
            canvas = sphere_size(sr);
            surface_alloc(&sphere, canvas, canvas);
            if (U.mode == DISPLAY_WINDOWED_BORDERLESS) {
                surface_free(&back);
            } else {
                surface_alloc(&back, mw, mh);
                if (big) DeleteObject(big), DeleteObject(small), DeleteObject(status_font);
                big = make_font(mh / 34, 350);
                small = make_font(mh / 46, FW_NORMAL);
                status_font = make_font(mh / 54, FW_NORMAL);
            }
        }

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        double dt = (double)(now.QuadPart - last.QuadPart) / freq.QuadPart;
        if (dt > 0.1) dt = 0.1;
        last = now;
        double t = (double)(now.QuadPart - start.QuadPart) / freq.QuadPart;

        JvState st = (JvState)InterlockedCompareExchange(&U.state, 0, 0);
        SphereParams target;
        target_params(st, &target);
        sphere_lerp(&cur, &cur, &target, (float)(1.0 - exp(-dt / 0.25)));
        double level = InterlockedCompareExchange(&U.level_milli, 0, 0) / 1000.0;
        double k = level > env ? 1.0 - exp(-dt / 0.03) : 1.0 - exp(-dt / 0.18);
        env += (level - env) * k;
        float voice = st == JV_SPEAKING ? (float)fmin(1.0, env * 1.6) : st == JV_LISTENING ? (float)fmin(1.0, env * 0.8) : 0.0f;
        angle += cur.rotation_speed * dt * (1.0 + voice * 0.8);
        voice_t += dt * (1.0 + 2.5 * voice);

        bool orb = U.mode == DISPLAY_WINDOWED_BORDERLESS;
        sphere_render(sr, t, angle, voice_t, &cur, voice, (SphereStyle)U.style, sphere.px, sphere.w, orb);

        if (hud && orb) {
            SIZE sz = {sphere.w, sphere.h};
            POINT src = {0, 0};
            BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
            UpdateLayeredWindow(hud, NULL, NULL, &sz, sphere.dc, &src, 0, &bf, ULW_ALPHA);
        } else if (hud && back.dc) {
            int W = back.w, H = back.h;
            int band = (int)(H * 0.68);
            memset(back.px + (size_t)band * W, 0, sizeof(uint32_t) * (size_t)(H - band) * W);
            int side = H;
            int x0 = (W - side) / 2;
            if (sphere.w == side) {
                BitBlt(back.dc, x0, 0, side, side, sphere.dc, 0, 0, SRCCOPY);
            } else {
                SetStretchBltMode(back.dc, HALFTONE);
                SetBrushOrgEx(back.dc, 0, 0, NULL);
                StretchBlt(back.dc, x0, 0, side, side, sphere.dc, 0, 0, sphere.w, sphere.h, SRCCOPY);
            }
            draw_overlay(&back, big, small, status_font);
            HDC dc = GetDC(hud);
            if (dc) {
                BitBlt(dc, 0, 0, W, H, back.dc, 0, 0, SRCCOPY);
                ReleaseDC(hud, dc);
            }
        }
        ReleaseSRWLockShared(&U.hud_lock);

        /* Sincronizado con el monitor; en reposo a la mitad de cuadros (el
           movimiento es lento, no se nota, y ahorra batería). */
        DwmFlush();
        if (st == JV_IDLE && (++frame & 1)) DwmFlush();
    }
    sphere_destroy(sr);
    surface_free(&sphere);
    surface_free(&back);
    if (big) DeleteObject(big), DeleteObject(small), DeleteObject(status_font);
    return 0;
}

/* ------------------------------------------------- ventana de mensajes --- */

static void open_data_folder(void)
{
    ShellExecuteW(NULL, L"open", g_paths.local_dir, NULL, NULL, SW_SHOWNORMAL);
}

static LRESULT CALLBACK msg_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == U.taskbar_created && m) {
        tray_readd();
        return 0;
    }
    switch (m) {
    case WM_APP_TRAY:
        switch (LOWORD(l)) {
        case WM_LBUTTONUP:
        case NIN_SELECT:
        case NIN_KEYSELECT:
            show_hud(!InterlockedCompareExchange(&U.visible, 1, 1));
            break;
        case WM_CONTEXTMENU:
        case WM_RBUTTONUP:
            tray_show_menu(h, InterlockedCompareExchange(&U.visible, 1, 1), config_mic_muted());
            break;
        }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case IDM_TALK:
            voice_trigger();
            break;
        case IDM_TOGGLE_HUD:
            show_hud(!InterlockedCompareExchange(&U.visible, 1, 1));
            break;
        case IDM_MUTE: {
            bool muted = !config_mic_muted();
            config_set_mic_muted(muted);
            tray_set_tooltip(muted ? L"Jarvis — micrófono silenciado" : L"Jarvis — escuchando \"Hey Jarvis\"");
            tray_notify(L"Jarvis", muted ? L"Micrófono silenciado. Jarvis no escucha hasta que lo actives."
                                         : L"Micrófono activado. Di \"Hey Jarvis\" cuando quieras.");
            break;
        }
        case IDM_SETTINGS:
            settings_open(U.inst, false, U.on_saved);
            break;
        case IDM_OPEN_DATA:
            open_data_folder();
            break;
        case IDM_QUIT:
            PostMessageW(h, WM_APP_QUIT, 0, 0);
            break;
        }
        return 0;
    case WM_HOTKEY:
        if (w == HOTKEY_TALK) voice_trigger();
        return 0;
    case WM_APP_NOTIFY: {
        wchar_t **pair = (wchar_t **)l;
        tray_notify(pair[0], pair[1]);
        free(pair[0]);
        free(pair[1]);
        free(pair);
        return 0;
    }
    case WM_APP_YIELD:
        if (U.mode == DISPLAY_FULLSCREEN && InterlockedCompareExchange(&U.visible, 1, 1) && U.hud) {
            InterlockedExchange(&U.yielded, 1);
            ShowWindow(U.hud, SW_HIDE);
        }
        return 0;
    case WM_APP_SHOWHUD:
        if (w == 2) {
            if (InterlockedExchange(&U.yielded, 0) && U.hud && InterlockedCompareExchange(&U.visible, 1, 1))
                ShowWindow(U.hud, SW_SHOWNOACTIVATE);
            if (U.hud && U.mode == DISPLAY_FULLSCREEN_BORDERLESS && InterlockedCompareExchange(&U.visible, 1, 1))
                SetWindowPos(U.hud, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            SetEvent(U.wake);
        } else {
            show_hud(true);
        }
        return 0;
    case WM_APP_SETTINGS:
        settings_open(U.inst, false, U.on_saved);
        return 0;
    case WM_APP_CONFIG:
        rebuild_hud();
        return 0;
    case WM_APP_QUIT:
        tray_set_tooltip(L"Jarvis — cerrando…");
        InterlockedExchange(&U.visible, 0);
        if (U.hud) ShowWindow(U.hud, SW_HIDE);
        voice_stop();
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        UnregisterHotKey(h, HOTKEY_TALK);
        tray_remove();
        InterlockedExchange(&U.running, 0);
        SetEvent(U.wake);
        WaitForSingleObject(U.thread, 3000);
        if (U.hud) DestroyWindow(U.hud);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

bool ui_init(HINSTANCE inst, SettingsSavedFn on_saved)
{
    U.inst = inst;
    U.on_saved = on_saved;
    U.wake = CreateEventW(NULL, FALSE, FALSE, NULL);
    U.taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW wc = {sizeof wc};
    wc.lpfnWndProc = msg_proc;
    wc.hInstance = inst;
    wc.lpszClassName = JARVIS_MSG_CLASS;
    wc.hIcon = ui_app_icon(32);
    RegisterClassExW(&wc);
    WNDCLASSEXW hc = {sizeof hc};
    hc.style = CS_DBLCLKS;
    hc.lpfnWndProc = hud_proc;
    hc.hInstance = inst;
    hc.lpszClassName = HUD_CLASS;
    hc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    hc.hIcon = ui_app_icon(32);
    hc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassExW(&hc);

    U.msg = CreateWindowExW(0, JARVIS_MSG_CLASS, L"Jarvis", WS_OVERLAPPED, 0, 0, 0, 0, NULL, NULL, inst, NULL);
    if (!U.msg) return false;
    tray_init(U.msg, ui_app_icon(GetSystemMetrics(SM_CXSMICON)));
    tray_set_tooltip(config_mic_muted() ? L"Jarvis — micrófono silenciado" : L"Jarvis — escuchando \"Hey Jarvis\"");
    if (!RegisterHotKey(U.msg, HOTKEY_TALK, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'J'))
        log_msg("No pude registrar el atajo Ctrl+Alt+J (otra app lo usa).");

    InterlockedExchange(&U.visible, 1);
    load_display_config();
    create_hud();
    InterlockedExchange(&U.running, 1);
    U.thread = CreateThread(NULL, 0, render_main, NULL, 0, NULL);
    SetThreadPriority(U.thread, THREAD_PRIORITY_BELOW_NORMAL);
    return true;
}

int ui_run(void)
{
    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        HWND sw = settings_window();
        if (sw && IsDialogMessageW(sw, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return (int)m.wParam;
}
