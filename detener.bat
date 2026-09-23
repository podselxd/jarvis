@echo off
setlocal
cd /d "%~dp0"

if not exist "jarvis.pid" (
    echo Jarvis no parece estar corriendo ^(no encontre jarvis.pid^).
    pause
    exit /b 0
)

set /p JARVIS_PID=<jarvis.pid

tasklist /FI "PID eq %JARVIS_PID%" 2>nul | find "%JARVIS_PID%" >nul
if errorlevel 1 (
    echo Jarvis no esta corriendo ^(el proceso %JARVIS_PID% ya no existe^).
    del "jarvis.pid" >nul 2>nul
    pause
    exit /b 0
)

taskkill /PID %JARVIS_PID% /F >nul 2>nul
if errorlevel 1 (
    echo No se pudo cerrar el proceso %JARVIS_PID%. Prueba cerrarlo desde el Administrador de tareas.
) else (
    echo Jarvis cerrado.
)
del "jarvis.pid" >nul 2>nul
pause
