#ifndef JARVIS_TOOLS_H
#define JARVIS_TOOLS_H

#include <stdbool.h>

#include "third_party/cJSON.h"

/* Cada herramienta recibe sus argumentos ya parseados (objeto JSON, nunca
   NULL) y devuelve un string UTF-8 en el heap con el resultado para el modelo.
   Ninguna ejecuta comandos de shell libres: son acciones acotadas. */
typedef char *(*ToolFn)(const cJSON *args);

char *run_tool(const char *name, const char *arguments_json);
const char *arg_str(const cJSON *args, const char *key);
bool arg_bool(const cJSON *args, const char *key);
int arg_int(const cJSON *args, const char *key, int def);

/* sistema */
char *tool_open_app(const cJSON *a);
char *tool_control_media(const cJSON *a);
char *tool_control_desktop(const cJSON *a);
char *tool_focus_window(const cJSON *a);
char *tool_list_windows(const cJSON *a);
char *tool_type_text(const cJSON *a);
char *tool_leer_portapapeles(const cJSON *a);
char *tool_copiar_portapapeles(const cJSON *a);
char *tool_info_sistema(const cJSON *a);
char *focus_window_by_title(const char *needle, bool *ok);

/* archivos */
char *tool_list_files(const cJSON *a);
char *tool_read_file(const cJSON *a);
char *tool_buscar_archivo(const cJSON *a);
char *tool_mover_archivo(const cJSON *a);
char *tool_borrar_archivo(const cJSON *a);
wchar_t *known_folder_alias(const char *alias);
bool path_is_off_limits(const wchar_t *path);

/* web */
char *tool_web_search(const cJSON *a);
char *tool_leer_pagina(const cJSON *a);

/* memoria y perfiles */
char *tool_guardar_dato(const cJSON *a);
char *tool_recordar(const cJSON *a);
char *tool_identificarse(const cJSON *a);
char *tool_proteger_perfil(const cJSON *a);
char *tool_exportar_a_obsidian(const cJSON *a);
char *tool_crear_recordatorio(const cJSON *a);
char *tool_create_macro(const cJSON *a);
char *tool_run_macro(const cJSON *a);

/* calculadora y malla */
char *tool_calcular(const cJSON *a);
char *tool_registrar_dispositivo(const cJSON *a);
char *tool_gestionar_dispositivo(const cJSON *a);

#endif
