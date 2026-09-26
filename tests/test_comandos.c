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
#include "tools.h"
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
                                    "cierra_pestana", "cierra_ventana", "explorador", "carpeta", "app", "gracias",
                                    "teclas"};
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
        len = strlen(out);
        if (l.items[i].times > 1) snprintf(out + len, cap - len, " x%d", l.items[i].times);
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
    expect("abre spotify", "app:spotify");
    expect("pausa spotify", "pausa");

    printf("-- a la mexicana --\n");
    expect("púchale play", "play");
    expect("pícale pausa", "pausa");
    expect("qué pedo wey, ponle play", "play");
    expect("no mames, ponle pausa", "pausa");
    expect("quiubo carnal, púchale a la siguiente", "siguiente");
    expect("ponme otra rola", "siguiente");
    expect("órale, cámbiale a la que sigue", "siguiente");
    expect("súbele un buen", "sube");
    expect("súbele al máximo", "volumen:100");
    expect("bájale al mínimo", "volumen:5");
    expect("más quedito", "baja");
    expect("quítale el volumen", "mute");
    expect("grax", "gracias");
    expect("se agradece carnal", "gracias");
    expect("súbele dos rayitas", "sube");
    expect("más suave porfa mi rey", "baja");
    expect("ponle stop", "pausa");
    expect("escóndela", "minimiza");
    expect("eres un crack, vato", "gracias");

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

static void test_youtube(void)
{
    printf("-- YouTube: el primer video de la búsqueda --\n");
    char id[12];
    const char *html = "<script>var ytInitialData = {\"contents\":{\"sectionListRenderer\":{\"contents\":[{\"videoRenderer\":"
                       "{\"videoId\":\"pAgnJDJN4VA\",\"thumbnail\":{}}},{\"videoRenderer\":{\"videoId\":\"etAIpkdhU9Q\"}}]}}};";
    check(youtube_first_video_id(html, id) && !strcmp(id, "pAgnJDJN4VA"), "toma el primer video de los resultados");
    check(!youtube_first_video_id("{\"videoId\":\"corto\"}", id) && !*id, "un id que no tiene 11 caracteres no cuenta");
    check(!youtube_first_video_id("<html>Antes de ir a YouTube…</html>", id), "sin resultados (aviso de cookies): nada");
}

static void test_teclas(void)
{
    printf("-- teclas y atajos: al momento, sin el modelo --\n");
    expect("Oprime Windows.", "teclas:windows");
    expect("Dale enter.", "teclas:enter");
    expect("Oye Sokari, presiona escape por favor", "teclas:escape");
    expect("Control zeta.", "teclas:control zeta");
    expect("control Z", "teclas:control z");
    expect("Alt tab", "teclas:alt tab");
    expect("dale al enter", "teclas:al enter");
    expect("oprime la tecla windows", "teclas:la tecla windows");
    expect("Presiona F5.", "teclas:f5");
    expect("Dale enter tres veces.", "teclas:enter x3");
    expect("oprime flecha abajo 5 veces porfa", "teclas:flecha abajo x5");
    expect("Presiona control shift T", "teclas:control shift t");
    expect("Pícale a la barra espaciadora", "teclas:a la barra espaciadora");
    printf("-- lo que parece tecla pero no lo es (o se confirma) --\n");
    expect("púchale play", "play");
    expect("Dale play.", "play");
    expect("dale siguiente", "siguiente");
    expect("oprime suprimir", "-");       /* borrar: siempre con un sí, por el modelo */
    expect("presiona shift supr", "-");
    expect("control de", "-");            /* Ctrl+D también borra en el Explorador */
    expect("no oprimas enter", "-");
    expect("no, dale enter", "-");
    expect("enter", "-");                 /* sin verbo, solo combinaciones */
    expect("abajo", "-");
    expect("control banana", "-");
    expect("oprime control", "-");        /* Ctrl sola no hace nada */
    expect("¿Qué hace control zeta?", "-");
    expect("dale enter mil veces", "-");
    expect("abre una pestaña nueva", "-"); /* no es una app que se llame «nueva» */
    expect("abre una ventana nueva de chrome", "app:chrome");
}

int wmain(void)
{
    SetConsoleOutputCP(CP_UTF8);
    test_del_log();
    test_nombre();
    test_youtube();
    test_teclas();
    printf("%d/%d pruebas %s\n", g_total - g_fail, g_total, g_fail ? "— HAY FALLAS" : "ok");
    return g_fail ? 1 : 0;
}
