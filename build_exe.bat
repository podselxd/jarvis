@echo off
cd /d "%~dp0"

echo ============================================
echo   Armando Jarvis.exe
echo ============================================
echo.

python -m pip install --quiet -r requirements.txt pyinstaller

echo Generando icono desde la animacion...
python -c "from sphere import SphereRenderer, IDLE_PARAMS; img = SphereRenderer(size=512).render(2.0, IDLE_PARAMS).convert('RGBA'); img.save('icono.ico', sizes=[(16,16),(32,32),(48,48),(64,64),(128,128),(256,256)])"

echo.
echo Empaquetando (puede tardar varios minutos)...
python -m PyInstaller --noconfirm --onefile --windowed --name Jarvis --icon icono.ico --collect-data customtkinter jarvis.py

echo.
echo ============================================
echo   Listo: dist\Jarvis.exe
echo ============================================
pause
