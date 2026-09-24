# Compila Jarvis.exe (un solo ejecutable estático, sin DLLs extra).
# Requiere MinGW-w64 (por ejemplo WinLibs: winget install BrechtSanders.WinLibs.POSIX.UCRT)
# y correrse desde un shell tipo POSIX (Git Bash):  mingw32-make

CC = gcc
WINDRES = windres

CFLAGS := -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
          -municode -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -DNTDDI_VERSION=0x0A000006 \
          -DCOBJMACROS -Isrc -MMD -MP
LIBS := -lwinhttp -lole32 -loleaut32 -luuid -lwinmm -lgdi32 -luser32 -lshell32 -lshlwapi \
        -lcomctl32 -ldwmapi -lws2_32 -liphlpapi -lbcrypt -ladvapi32 -lcomdlg32 -luxtheme -lpowrprof -lm

SRC := $(filter-out src/main.c,$(wildcard src/*.c)) src/third_party/cJSON.c
OBJ := $(patsubst src/%.c,build/obj/%.o,$(SRC))
MAIN_OBJ := build/obj/main.o
RES_OBJ := build/obj/jarvis_res.o
RES_DEPS := res/jarvis.rc res/jarvis.manifest res/jarvis.ico res/tools.json res/system_prompt.txt res/wakeword.bin

# Con Jarvis abierto, Windows no deja reemplazar dist/Jarvis.exe:
# mingw32-make OUT=build/Jarvis.exe compila en otro lado.
OUT ?= dist/Jarvis.exe

# GCC de MinGW enlaza su propio manifiesto (default-manifest.o), que choca con
# el de Jarvis (".rsrc merge failure: multiple non-default manifests"). Con -B
# encuentra primero este objeto vacío del mismo nombre y queda un solo manifiesto.
NOMANIFEST_DIR := build/nomanifest/
NOMANIFEST := $(NOMANIFEST_DIR)default-manifest.o

.PHONY: all clean tests

all: $(OUT)

$(OUT): $(OBJ) $(MAIN_OBJ) $(RES_OBJ) | $(NOMANIFEST)
	@mkdir -p $(dir $@)
	$(CC) -o $@ $^ -B$(NOMANIFEST_DIR) -static -municode -mwindows -s $(LIBS)

$(NOMANIFEST):
	@mkdir -p $(dir $@)
	printf '' | $(CC) -x c -c - -o $@

build/obj/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Kernels de la red del wake word: una copia con AVX2+FMA y otra genérica;
# wakeword.c elige cuál usar según el CPU al arrancar.
build/obj/nn_avx2.o: CFLAGS += -O3 -mavx2 -mfma
build/obj/nn_generic.o: CFLAGS += -O3

$(RES_OBJ): $(RES_DEPS)
	@mkdir -p $(dir $@)
	$(WINDRES) -I res -i res/jarvis.rc -o $@

TEST_SRC := $(wildcard tests/*.c)
TESTS := $(patsubst tests/%.c,build/tests/%.exe,$(TEST_SRC))

tests: $(TESTS)

build/tests/%.exe: tests/%.c $(OBJ) $(RES_OBJ) | $(NOMANIFEST)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^ -B$(NOMANIFEST_DIR) -static -municode -mconsole $(LIBS)

# La prueba de confirmación trae su propio Groq y su propio run_tool de
# mentira, así que se enlaza sin groq.o ni tools.o.
build/tests/test_confirmacion.exe: tests/test_confirmacion.c $(filter-out build/obj/groq.o build/obj/tools.o,$(OBJ)) $(RES_OBJ) | $(NOMANIFEST)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^ -B$(NOMANIFEST_DIR) -static -municode -mconsole $(LIBS)

clean:
	rm -rf build dist/Jarvis.exe

-include $(OBJ:.o=.d) $(MAIN_OBJ:.o=.d)
