@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

echo ============================================
echo   Jarvis - instalador
echo ============================================
echo.

where python >nul 2>nul
if errorlevel 1 (
    echo No se encontro Python instalado.
    echo Instala Python desde https://python.org/downloads ^(marca "Add to PATH"^) y vuelve a correr este archivo.
    pause
    exit /b 1
)

if exist ".git" (
    echo Buscando actualizaciones...
    git pull --ff-only
    if errorlevel 1 (
        echo.
        echo No se pudo actualizar solo ^(puede que hayas tocado algun archivo del repo
        echo a mano^). Arranca igual con lo que ya tenes; si queres forzar la
        echo actualizacion, corre "git pull" vos mismo y revisa el conflicto.
        pause
    ) else (
        echo Listo, al dia.
    )
    echo.
) else if exist "update.py" (
    python update.py
    echo.
)

echo Instalando dependencias, puede tardar un minuto...
python -m pip install -r requirements.txt --quiet
if errorlevel 1 (
    echo.
    echo Hubo un error instalando dependencias. Revisa el mensaje de arriba.
    pause
    exit /b 1
)

if not exist ".env" type nul > .env

findstr /B "GROQ_API_KEY=" .env >nul 2>nul
if errorlevel 1 (
    echo.
    echo Necesitas una API key gratis de Groq ^(sin tarjeta^): https://console.groq.com
    set /p GROQ_KEY="Pega tu API key de Groq y presiona Enter: "
    echo GROQ_API_KEY=!GROQ_KEY!>> .env
    echo Guardada en .env
) else (
    echo.
    echo Ya hay una API key guardada, la uso.
)

findstr /B "JARVIS_USER_NAME=" .env >nul 2>nul
if errorlevel 1 (
    echo.
    set /p USER_NAME="Como te llamas? (para que Jarvis te reconozca desde el arranque, opcional): "
    if not "!USER_NAME!"=="" echo JARVIS_USER_NAME=!USER_NAME!>> .env
)

findstr /B "JARVIS_STOP_WORD=" .env >nul 2>nul
if errorlevel 1 (
    echo.
    echo Por seguridad podes poner una palabra secreta que apague a Jarvis apenas
    echo la digas. La IA nunca la conoce - se revisa antes de mandarle nada. Elegi
    echo algo que no digas en una charla normal, para que no se dispare solo.
    set /p STOP_WORD="Palabra de apagado (dejala vacia si no la queres): "
    if not "!STOP_WORD!"=="" echo JARVIS_STOP_WORD=!STOP_WORD!>> .env
)

where tailscale >nul 2>nul
if errorlevel 1 (
    echo.
    set /p WANT_TS="Queres conectar este dispositivo a tus otros Jarvis via Tailscale? (s/n): "
    if /i "!WANT_TS!"=="s" set WANT_TS_OK=1
    if /i "!WANT_TS!"=="si" set WANT_TS_OK=1
    if defined WANT_TS_OK (
        echo Instalando Tailscale...
        winget install --id Tailscale.Tailscale --accept-package-agreements --accept-source-agreements
        echo.
        echo Instalado. Corre "tailscale up" en una terminal para iniciar sesion con tu
        echo cuenta ^(se abre el navegador^) - eso es tuyo, no lo hago yo por vos.
        pause
    )
) else (
    echo.
    echo Tailscale ya esta instalado.
)

set STARTUP_LNK=%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup\Jarvis.lnk
if exist "%STARTUP_LNK%" (
    echo.
    echo El autostart ya esta configurado ^(inicia solo con Windows^).
) else (
    echo.
    set /p AUTOSTART="Quieres que Jarvis se inicie solo con Windows? (s/n): "
    if /i "!AUTOSTART!"=="s" set AUTOSTART_OK=1
    if /i "!AUTOSTART!"=="si" set AUTOSTART_OK=1
    if /i "!AUTOSTART!"=="y" set AUTOSTART_OK=1
    if /i "!AUTOSTART!"=="yes" set AUTOSTART_OK=1
    if defined AUTOSTART_OK (
        for /f "delims=" %%P in ('where pythonw') do set PYTHONW=%%P
        powershell -NoProfile -Command "$s=(New-Object -COM WScript.Shell).CreateShortcut('%STARTUP_LNK%'); $s.TargetPath='!PYTHONW!'; $s.Arguments='\"%~dp0jarvis.py\"'; $s.WorkingDirectory='%~dp0'; $s.Save()"
        echo Listo, Jarvis va a iniciar solo con Windows ^(en segundo plano, sin ventana^).
    )
)

echo.
echo ============================================
echo   Todo listo. Iniciando Jarvis...
echo   Di "Hey Jarvis" para activarlo.
echo   Cierra esta ventana para apagarlo.
echo ============================================
echo.
python jarvis.py
pause
