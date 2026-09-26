/* Una ventana de GTK para probar la extensión de GNOME sin pantalla: escribe
   en la salida todo lo que le pasa (el texto que le escribieron, qué había en
   el portapapeles al pegar, si la cerraron) para que la prueba lo revise.
   Con Ctrl+Tab cambia de "pestaña" (su título), como un navegador.
   Uso: ventana_de_prueba ID_DE_APP "Título" */
#include <gtk/gtk.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static const char *const TABS[] = {"Inicio", "Correo", "Música del día", "Mapas"};
static int g_tab;
static char g_title[256];
static GtkWidget *g_window;

static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}

static void set_title(void)
{
    char t[512];
    snprintf(t, sizeof t, "%s - %s", TABS[g_tab], g_title);
    gtk_window_set_title(GTK_WINDOW(g_window), t);
    say("pestaña: %s", TABS[g_tab]);
}

static void on_changed(GtkTextBuffer *buf, gpointer u)
{
    GtkTextIter a, b;
    gtk_text_buffer_get_bounds(buf, &a, &b);
    char *text = gtk_text_buffer_get_text(buf, &a, &b, FALSE);
    GString *esc = g_string_new(NULL);
    for (const char *p = text; *p; p++) {
        if (*p == '\n') g_string_append(esc, "\\n");
        else g_string_append_c(esc, *p);
    }
    say("texto: %s", esc->str);
    g_string_free(esc, TRUE);
    g_free(text);
}

static void on_targets(GtkClipboard *c, GdkAtom *atoms, gint n, gpointer u)
{
    GString *s = g_string_new(NULL);
    for (int i = 0; i < n; i++) {
        char *name = gdk_atom_name(atoms[i]);
        g_string_append_printf(s, "%s%s", i ? " " : "", name);
        g_free(name);
    }
    say("pegado: %s", s->str);
    g_string_free(s, TRUE);
}

static gboolean on_key(GtkWidget *w, GdkEventKey *e, gpointer u)
{
    bool ctrl = e->state & GDK_CONTROL_MASK;
    if (ctrl && e->keyval == GDK_KEY_Tab) {
        g_tab = (g_tab + 1) % (int)G_N_ELEMENTS(TABS);
        set_title();
        return TRUE;
    }
    if (ctrl && (e->keyval == GDK_KEY_v || e->keyval == GDK_KEY_V))
        gtk_clipboard_request_targets(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), on_targets, NULL);
    if (e->keyval == GDK_KEY_Return || e->keyval == GDK_KEY_KP_Enter) say("enter");
    return FALSE;
}

static gboolean on_delete(GtkWidget *w, GdkEvent *e, gpointer u)
{
    say("cerrada");
    return FALSE;
}

static void on_state(GtkWidget *w, GdkEventWindowState *e, gpointer u)
{
    if (e->changed_mask & GDK_WINDOW_STATE_ICONIFIED)
        say("minimizada: %s", e->new_window_state & GDK_WINDOW_STATE_ICONIFIED ? "sí" : "no");
    if (e->changed_mask & GDK_WINDOW_STATE_MAXIMIZED)
        say("maximizada: %s", e->new_window_state & GDK_WINDOW_STATE_MAXIMIZED ? "sí" : "no");
    if (e->changed_mask & GDK_WINDOW_STATE_FOCUSED)
        say("enfocada: %s", e->new_window_state & GDK_WINDOW_STATE_FOCUSED ? "sí" : "no");
}

static void on_activate(GtkApplication *app, gpointer u)
{
    if (g_window) {
        gtk_window_present(GTK_WINDOW(g_window));
        return;
    }
    g_window = gtk_application_window_new(app);
    gtk_window_set_default_size(GTK_WINDOW(g_window), 500, 300);
    GtkWidget *view = gtk_text_view_new();
    gtk_container_add(GTK_CONTAINER(g_window), view);
    g_signal_connect(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)), "changed", G_CALLBACK(on_changed), NULL);
    g_signal_connect(g_window, "key-press-event", G_CALLBACK(on_key), NULL);
    g_signal_connect(g_window, "delete-event", G_CALLBACK(on_delete), NULL);
    g_signal_connect(g_window, "window-state-event", G_CALLBACK(on_state), NULL);
    set_title();
    gtk_widget_show_all(g_window);
    gtk_widget_grab_focus(view);
    say("lista");
}

int main(int argc, char **argv)
{
    const char *id = argc > 1 ? argv[1] : "io.github.podselxd.SokariPrueba";
    snprintf(g_title, sizeof g_title, "%s", argc > 2 ? argv[2] : "Ventana de prueba de Sokari");
    GtkApplication *app = gtk_application_new(id, G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    char *args[] = {argv[0], NULL};
    int rc = g_application_run(G_APPLICATION(app), 1, args);
    g_object_unref(app);
    return rc;
}
