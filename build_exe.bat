@echo off
cd /d "%~dp0"

echo ============================================
echo   Armando Jarvis.exe
echo ============================================
echo.

python -m pip install --quiet -r requirements.txt pyinstaller

echo Generando icono desde la animacion...
python -c "from PIL import Image; img = Image.open('assets/idle_purple.gif'); img.seek(20); frame = img.convert('RGBA'); frame = frame.crop((frame.width//2-300, frame.height//2-300, frame.width//2+300, frame.height//2+300)); frame.save('icono.ico', sizes=[(16,16),(32,32),(48,48),(64,64),(128,128),(256,256)])"

echo.
echo Empaquetando (puede tardar varios minutos)...
python -m PyInstaller --noconfirm --onefile --windowed --name Jarvis --icon icono.ico --add-data "assets;assets" --collect-data customtkinter jarvis.py

echo.
echo ============================================
echo   Listo: dist\Jarvis.exe
echo ============================================
pause
