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

echo.
echo ============================================
echo   Iniciando Jarvis...
echo   ^(la primera vez se abre una ventana para configurar tu API key^)
echo ============================================
echo.
python jarvis.py
pause
