@echo off
REM -----------------------------------------------------------------------
REM start_server.bat
REM -----------------------------------------------------------------------
REM Arranca el servidor del historico del jacuzzi en Windows.
REM Si es la primera vez (o falta el entorno virtual), lo crea e instala
REM las dependencias solo, sin pasos manuales previos.
REM Doble clic para arrancarlo a mano, o usar como accion del Programador
REM de tareas para que arranque solo al iniciar sesion (ver README.md).
REM -----------------------------------------------------------------------
cd /d "%~dp0"

REM Cambia esto por tu clave real antes de usarlo en produccion (debe
REM coincidir con REMOTE_LOG_API_KEY en config.h del ESP32).
set JACUZZI_API_KEY=7fK9xQ2mV8rL4zN6pT3wY5sH1cJ0dB4a

REM Comprueba que Python esta instalado y accesible desde el PATH.
where python >nul 2>nul
if errorlevel 1 (
    echo [ERROR] No se encuentra "python" en el PATH.
    echo         Instala Python desde https://www.python.org/downloads/
    echo         y marca la casilla "Add python.exe to PATH" durante la instalacion.
    pause
    exit /b 1
)

REM Crea el entorno virtual la primera vez (si no existe ya).
if not exist ".venv\Scripts\python.exe" (
    echo Primera ejecucion: creando entorno virtual e instalando dependencias...
    python -m venv .venv
    if errorlevel 1 (
        echo [ERROR] No se pudo crear el entorno virtual.
        pause
        exit /b 1
    )
)

REM Instala/actualiza dependencias siempre que start_server.bat cambie de
REM version (comparamos requirements.txt con un "sello" guardado del
REM ultimo install, para no reinstalar en cada arranque sin necesidad).
set "STAMP=.venv\requirements.stamp"
set "NEED_INSTALL=0"
if not exist "%STAMP%" set "NEED_INSTALL=1"
if exist "%STAMP%" (
    fc /b "%STAMP%" requirements.txt >nul 2>nul
    if errorlevel 1 set "NEED_INSTALL=1"
)

if "%NEED_INSTALL%"=="1" (
    echo Instalando/actualizando dependencias de requirements.txt...
    ".venv\Scripts\python.exe" -m pip install --upgrade pip -q
    ".venv\Scripts\python.exe" -m pip install -r requirements.txt -q
    if errorlevel 1 (
        echo [ERROR] Fallo instalando las dependencias.
        pause
        exit /b 1
    )
    copy /y requirements.txt "%STAMP%" >nul
)

echo.
echo Arrancando servidor en http://0.0.0.0:8000  (Ctrl+C para detener)
".venv\Scripts\python.exe" -m uvicorn main:app --host 0.0.0.0 --port 8000
pause
