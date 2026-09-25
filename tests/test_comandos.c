/* Los comandos simples que Sokari hace sin preguntarle al modelo, probados
   con frases reales de tu log (como las transcribe Whisper, con saludos,
   groserías y el nombre mal escrito). Solo se revisa qué entiende: nunca se
   ejecuta nada. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "intents.h"
#include "util.h"

static int g_fail, g_total;

static void check(bool ok, const char *what)
{
    g_total++;
    printf("%s %s\n", ok ? "ok   " : "FALLA", what);
    if (!ok) g_fail++;
}

static const char *kind_name(IntentKind k)
{
    static const char *const N[] = {"play", "pausa", "siguiente", "anterior", "sube", "baja", "volumen",
                                    "mute", "minimiza", "minimiza_todo", "maximiza", "pantalla_completa",
                                    "cierra_pestana", "cierra_ventana", "explorador", "carpeta", "app", "gracias"};
    return N[k];
}

/* Lo que entendió, como texto: "minimiza+explorador", "app:opera", o "-" si
   se lo deja al modelo. */
static void understood(const char *text, char *out, size_t cap)
{
    IntentList l;
    if (!intents_parse(text, &l)) {
        snprintf(out, cap, "-");
        return;
    }
    out[0] = 0;
    for (int i = 0; i < l.n; i++) {
        size_t len = strlen(out);
        snprintf(out + len, cap - len, "%s%s%s%s", i ? "+" : "", kind_name(l.items[i].kind),
                 *l.items[i].arg ? ":" : "", l.items[i].arg);
    }
}

static void expect(const char *text, const char *want)
{
    char got[160];
    understood(text, got, sizeof got);
    char what[400];
    snprintf(what, sizeof what, "«%s» -> %s%s%s", text, got, strcmp(got, want) ? "   (se esperaba " : "",
             strcmp(got, want) ? want : "");
    if (strcmp(got, want)) strncat(what, ")", sizeof what - strlen(what) - 1);
    check(!strcmp(got, want), what);
}

static void test_del_log(void)
{
    printf("-- frases de tu log que ahora se hacen al momento --\n");
    expect("¿Qué onda Sakari? ¿Cómo andas? Oye, ¿puedes poner el play al video?", "play");
    expect("¿Cómo andas? Oye, ¿puedes poner play al video?", "play");
    expect("¿Qué onda Sokari? Oye, ¿puedes ponerle play a mi video?", "play");
    expect("No lo estás haciendo. No lo estás haciendo. Ponle play.", "play");
    expect("Eh, ¿cómo andas, Akari? Oye, ¿detectas el video que está ahí? ¿Podrías ponerle play, porfa?", "play");
    expect("Sokari, este video está en negro, es un video, ponle play.", "play");
    expect("Sokary, puedes ponerle play al video, porfa ¿Cómo andas? Por cierto ¿Cómo andas?", "play");
    expect("se mueve que onda sacaria como andas oye puedes ponerle play al video", "play");
    expect("¿Ves el video de ahí en fondo? Puedes ponerle play, porfa.", "play");
    expect("Ponle pausa a este video Ey Sokari Pues", "pausa");
    expect("oye Sokary puedes ponerle play al vídeo", "play");
    expect("Bueno, está bien, minimiza la pestaña y abre archivos.", "minimiza+explorador");
    expect("Y abre la pestaña que está abierta del Opera", "app:opera");
    expect("Gracias. Muy bien. Hey, Zachary. Puedes abrir la pestaña de ópera.", "app:opera");
    expect("abre opera por favor", "app:opera");
    expect("¡Gracias!", "gracias");
    expect("Gracias.", "gracias");

    printf("-- y otras de todos los días --\n");
    expect("sube el volumen", "sube");
    expect("bájale tantito", "baja");
    expect("pon el volumen al 30", "volumen:30");
    expect("la siguiente canción porfa", "siguiente");
    expect("regresa la canción", "anterior");
    expect("maximiza la ventana", "maximiza");
    expect("cierra la pestaña", "cierra_pestana");
    expect("minimiza todo", "minimiza_todo");
    expect("abre mis descargas", "carpeta:descargas");
    expect("abre el navegador", "app:navegador");
    expect("oye abre discord y ponle pausa", "app:discord+pausa");
    expect("abre opera y maximízala", "app:opera+maximiza");

    printf("-- lo que se le deja al modelo --\n");
    expect("pon la canción de ACDC de Black in Black", "-");
    expect("ponle play al video que te mandé por whatsapp", "-");
    expect("puedes abrir el navegador de Opera y poner YouTube", "-");
    expect("necesito que me ayudes a conectar mi laptop con la pc para poder mover la laptop desde aquí", "-");
    expect("Borra el chat que tuviste En todo el día de hoy, ¿ok? Borra la memoria temporal", "-");
    expect("¿Qué es play?", "-");
    expect("no le pongas pausa", "-");
    expect("nunca minimices esa ventana", "-");
    expect("lee el siguiente párrafo", "-");
    expect("pausa el video y busca tutoriales de python", "-");
    expect("¿Cómo andas? ¿Me puedes contar cómo estás?", "-");
}

static void test_nombre(void)
{
    printf("-- tu nombre como lo escribe la transcripción --\n");
    const char *yes[] = {"sokari", "sokary", "socari", "sakari", "akari", "okari", "sotori", "zachary",
                         "zockery", "zuckari", "zucari", "sukari", "sacaria", "socar"};
    const char *no[] = {"sacar", "socorro", "cari", "carino", "azucar", "sakura", "secretaria", "cosa", "sabado",
                        "karina", "opera", "discord"};
    bool all_yes = true, any_no = false;
    for (size_t i = 0; i < sizeof yes / sizeof *yes; i++)
        if (!intents_is_name_word(yes[i])) {
            printf("      no reconoció: %s\n", yes[i]);
            all_yes = false;
        }
    for (size_t i = 0; i < sizeof no / sizeof *no; i++)
        if (intents_is_name_word(no[i])) {
            printf("      lo confundió: %s\n", no[i]);
            any_no = true;
        }
    check(all_yes, "Sokari mal escrito se reconoce (Zachary, Akari, Sotori, Zucari…)");
    check(!any_no, "palabras de verdad que suenan parecido no (sacar, socorro, cari…)");
}

int wmain(void)
{
    SetConsoleOutputCP(CP_UTF8);
    test_del_log();
    test_nombre();
    printf("%d/%d pruebas %s\n", g_total - g_fail, g_total, g_fail ? "— HAY FALLAS" : "ok");
    return g_fail ? 1 : 0;
}
