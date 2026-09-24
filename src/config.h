#ifndef JARVIS_CONFIG_H
#define JARVIS_CONFIG_H

#include <stdbool.h>
#include <wchar.h>

#define JARVIS_VERSION "2.0.2"
#define JARVIS_VERSION_W L"2.0.2"
#define GITHUB_REPO "podselxd/jarvis"

typedef enum {
    DISPLAY_FULLSCREEN = 0,
    DISPLAY_FULLSCREEN_BORDERLESS = 1,
    DISPLAY_WINDOWED_BORDERLESS = 2, /* la esfera flotante */
    DISPLAY_WINDOWED = 3,
    DISPLAY_MINIMIZED = 4, /* como DISPLAY_WINDOWED, pero arranca minimizada */
    DISPLAY_MODE_COUNT
} DisplayMode;

typedef struct {
    char *groq_api_key;
    char *user_name;
    char *stop_word;
    char *mesh_secret;
    char *voice;
    char *mic_name;
    char *output_name; /* salida de audio; "" = la predeterminada de Windows */
    int display_mode;
    int resolution; /* 0 = automática, si no el alto en píxeles (720, 1080, 1440, 2160) */
    int volume;     /* 0-100, volumen de la voz de Jarvis (no el del sistema) */
    int wake_sensitivity; /* 0-100 */
    int sphere_style;     /* 0 = halo de puntos, 1 = líneas */
    int orb_x, orb_y;     /* posición de la ventana flotante; -1 = centrada */
    int win_x, win_y, win_w, win_h; /* modo Ventana; win_w <= 0 = tamaño y lugar por defecto */
    bool subtitles;
    bool autostart;
    bool mic_muted;
} AppConfig;

typedef struct {
    wchar_t *local_dir;  /* %LOCALAPPDATA%\Jarvis: config, log, dispositivos */
    wchar_t *memory_dir; /* <OneDrive o perfil>\Desktop\Jarvis: memoria, datos, perfiles (igual que antes) */
    wchar_t *config_file;
    wchar_t *log_file;
    wchar_t *sounds_dir;
    wchar_t *update_dir;
} AppPaths;

extern AppPaths g_paths;

void paths_init(void);

void config_load(void);
bool config_save(void);
void config_migrate_legacy(void);

AppConfig config_snapshot(void);
void config_free(AppConfig *c);
void config_apply(const AppConfig *c);

char *config_api_key(void);
char *config_stop_word(void);
char *config_user_name(void);
char *config_mesh_secret(bool create);
int config_volume(void);
float config_wake_threshold(void);
bool config_mic_muted(void);
void config_set_mic_muted(bool muted);
void config_set_orb_pos(int x, int y);
void config_set_window_rect(int x, int y, int w, int h);
void config_set_display_mode(int mode);
void config_set_output(const char *name);

const char *display_mode_key(int mode);

#endif
