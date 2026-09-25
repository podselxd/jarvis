#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>

#include "config.h"
#include "ui.h"

static NOTIFYICONDATAW g_nid;
static bool g_added;

/* La tuerca junto a "Configuración…": el glifo de ajustes de Segoe Fluent
   Icons (Windows 11) o Segoe MDL2 Assets (Windows 10), pintado blanco sobre
   negro y convertido a un mapa de bits con alfa en el color de Sokari, que se
   ve igual de bien en menús oscuros y claros. */
static HBITMAP gear_bitmap(void)
{
    static HBITMAP bmp;
    static bool tried;
    if (tried) return bmp;
    tried = true;
    int size = MulDiv(16, (int)GetDpiForSystem(), 96);
    BITMAPINFO bi = {{sizeof(BITMAPINFOHEADER), size, -size, 1, 32, BI_RGB}};
    void *bits = NULL;
    HDC dc = CreateCompatibleDC(NULL);
    HBITMAP b = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!b) {
        DeleteDC(dc);
        return NULL;
    }
    HGDIOBJ old = SelectObject(dc, b);
    const wchar_t *faces[] = {L"Segoe Fluent Icons", L"Segoe MDL2 Assets"};
    bool drawn = false;
    for (int i = 0; i < 2 && !drawn; i++) {
        HFONT f = CreateFontW(-size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, faces[i]);
        HGDIOBJ old_font = SelectObject(dc, f);
        wchar_t face[LF_FACESIZE] = L"";
        WORD glyph = 0xFFFF;
        GetTextFaceW(dc, LF_FACESIZE, face);
        if (!_wcsicmp(face, faces[i]) && GetGlyphIndicesW(dc, L"\xE713", 1, &glyph, GGI_MARK_NONEXISTING_GLYPHS) == 1 &&
            glyph != 0xFFFF) {
            memset(bits, 0, (size_t)size * (size_t)size * 4);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(255, 255, 255));
            RECT r = {0, 0, size, size};
            DrawTextW(dc, L"\xE713", 1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            GdiFlush();
            drawn = true;
        }
        SelectObject(dc, old_font);
        DeleteObject(f);
    }
    SelectObject(dc, old);
    DeleteDC(dc);
    if (!drawn) {
        DeleteObject(b);
        return NULL;
    }
    uint32_t *px = bits;
    for (int i = 0; i < size * size; i++) {
        uint32_t r = (px[i] >> 16) & 0xFF, g = (px[i] >> 8) & 0xFF, bl = px[i] & 0xFF;
        uint32_t a = r > g ? r : g;
        if (bl > a) a = bl;
        px[i] = a << 24 | (0x9d * a / 255) << 16 | (0x8f * a / 255) << 8 | (0xff * a / 255);
    }
    bmp = b;
    return bmp;
}

static void add_icon(void)
{
    g_added = Shell_NotifyIconW(NIM_ADD, &g_nid) != 0;
    if (g_added) {
        g_nid.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &g_nid);
    }
}

bool tray_init(HWND owner, HICON icon)
{
    g_nid.cbSize = sizeof g_nid;
    g_nid.hWnd = owner;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    g_nid.uCallbackMessage = WM_APP_TRAY;
    g_nid.hIcon = icon;
    wcscpy(g_nid.szTip, L"Sokari");
    add_icon();
    return g_added;
}

void tray_readd(void)
{
    add_icon();
}

void tray_set_tooltip(const wchar_t *text)
{
    wcsncpy(g_nid.szTip, text, sizeof g_nid.szTip / sizeof g_nid.szTip[0] - 1);
    g_nid.uFlags = NIF_TIP | NIF_SHOWTIP;
    if (g_added) Shell_NotifyIconW(NIM_MODIFY, &g_nid);
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
}

void tray_notify(const wchar_t *title, const wchar_t *text)
{
    NOTIFYICONDATAW n = g_nid;
    n.uFlags = NIF_INFO;
    wcsncpy(n.szInfoTitle, title, sizeof n.szInfoTitle / sizeof n.szInfoTitle[0] - 1);
    wcsncpy(n.szInfo, text, sizeof n.szInfo / sizeof n.szInfo[0] - 1);
    n.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON;
    n.hBalloonIcon = g_nid.hIcon;
    if (g_added) Shell_NotifyIconW(NIM_MODIFY, &n);
}

void tray_show_menu(HWND owner, bool hud_visible, bool muted, int display_mode)
{
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, IDM_TALK, L"Hablar con Sokari\tCtrl+Alt+J");
    AppendMenuW(m, MF_STRING, IDM_TOGGLE_HUD, hud_visible ? L"Ocultar la esfera" : L"Mostrar la esfera");
    HMENU modes = CreatePopupMenu();
    for (int i = 0; i < DISPLAY_MODE_COUNT; i++)
        AppendMenuW(modes, MF_STRING, (UINT_PTR)(IDM_MODE_BASE + i), DISPLAY_MODE_LABELS[i]);
    CheckMenuRadioItem(modes, IDM_MODE_BASE, IDM_MODE_BASE + DISPLAY_MODE_COUNT - 1, IDM_MODE_BASE + display_mode,
                       MF_BYCOMMAND);
    AppendMenuW(m, MF_POPUP, (UINT_PTR)modes, L"Modo de pantalla");
    AppendMenuW(m, MF_STRING | (muted ? MF_CHECKED : 0), IDM_MUTE, L"Silenciar micrófono");
    AppendMenuW(m, MF_STRING | (config_full_access() ? MF_CHECKED : 0), IDM_FULL_ACCESS,
                L"Acceso completo (menos borrar)");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDM_HOME, L"Ventana de inicio…");
    AppendMenuW(m, MF_STRING, IDM_SETTINGS, L"Configuración…");
    HBITMAP gear = gear_bitmap();
    if (gear) {
        MENUITEMINFOW mii = {.cbSize = sizeof mii, .fMask = MIIM_BITMAP, .hbmpItem = gear};
        SetMenuItemInfoW(m, IDM_SETTINGS, FALSE, &mii);
    }
    AppendMenuW(m, MF_STRING, IDM_OPEN_DATA, L"Abrir carpeta de Sokari");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDM_QUIT, L"Salir");
    SetMenuDefaultItem(m, IDM_TOGGLE_HUD, FALSE);
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(owner);
    TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, owner, NULL);
    PostMessageW(owner, WM_NULL, 0, 0);
    DestroyMenu(m);
}

void tray_remove(void)
{
    if (g_added) Shell_NotifyIconW(NIM_DELETE, &g_nid);
    g_added = false;
}
