#ifndef JARVIS_UI_H
#define JARVIS_UI_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdbool.h>

#define WM_APP_TRAY (WM_APP + 1)
#define WM_APP_NOTIFY (WM_APP + 2)
#define WM_APP_YIELD (WM_APP + 3)
#define WM_APP_QUIT (WM_APP + 4)
#define WM_APP_SHOWHUD (WM_APP + 5)
#define WM_APP_SETTINGS (WM_APP + 6)
#define WM_APP_CONFIG (WM_APP + 7)

#define JARVIS_MSG_CLASS L"JarvisMessageWindow"

enum {
    IDM_TALK = 1001,
    IDM_TOGGLE_HUD,
    IDM_MUTE,
    IDM_SETTINGS,
    IDM_OPEN_DATA,
    IDM_QUIT,
};

typedef void (*SettingsSavedFn)(bool first_run);

bool ui_init(HINSTANCE inst, SettingsSavedFn on_saved);
int ui_run(void);
void ui_config_changed(void);
HWND ui_message_window(void);
HICON ui_app_icon(int size);

/* tray.c */
bool tray_init(HWND owner, HICON icon);
void tray_readd(void);
void tray_set_tooltip(const wchar_t *text);
void tray_notify(const wchar_t *title, const wchar_t *text);
void tray_show_menu(HWND owner, bool hud_visible, bool muted);
void tray_remove(void);

/* ui_settings.c */
void settings_open(HINSTANCE inst, bool first_run, SettingsSavedFn on_saved);
HWND settings_window(void);

#endif
