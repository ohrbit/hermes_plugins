@echo off
:: Manual ESP-IDF v6.0.2 environment (idf-env not registered for v6.0.2).
set IDF_PATH=C:\esp\v6.0.2\esp-idf
set "IDF_TOOLS_PATH=C:\Espressif\tools"

set "IDF_PYTHON=C:\Espressif\tools\python\v6.0.2\venv\Scripts\python.exe"
set "GCC_DIR=C:\Espressif\tools\xtensa-esp-elf\esp-15.2.0_20251204\xtensa-esp-elf\bin"
set "CMAKE_DIR=C:\Espressif\tools\cmake\4.0.3\bin"
set "NINJA_DIR=C:\Espressif\tools\ninja\1.12.1"
set "GIT_DIR=C:\Espressif\tools\idf-git\2.44.0\cmd"
set "OPENOCD_DIR=C:\Espressif\tools\openocd-esp32\v0.12.0-esp32-20241016\bin"

:: Python venv scripts dir + venv python dir
set "PY_SCRIPTS=C:\Espressif\tools\python\v6.0.2\venv\Scripts"
set "PY_DIR=C:\Espressif\tools\python\v6.0.2\venv"

set "PATH=%GCC_DIR%;%CMAKE_DIR%;%NINJA_DIR%;%GIT_DIR%;%PY_SCRIPTS%;%IDF_TOOLS_PATH%;%PATH%"

:: Clear conflicting python env that may break venv
set PYTHONPATH=
set PYTHONHOME=
set PYTHONNOUSERSITE=True

:: Espressif IDE registers v6.0.2 under a custom tool path; ensure idf.py works
set "PATH=%IDF_PATH%\tools;%PATH%"

:: idf.py aborts early when MSYSTEM is set (git-bash/MSys) — main() never runs.
set MSYSTEM=

:: The installed venv lives here (not the default python_env\idf6.0_py3.11_env).
set "IDF_PYTHON_ENV_PATH=C:\Espressif\tools\python\v6.0.2\venv"

:: Component manager needs ESP_IDF_VERSION (normally set by activate.py).
set "ESP_IDF_VERSION=6.0.2"

:: ROM ELF dir for bootloader/gdbinit generation (normally set by activate.py).
set "ESP_ROM_ELF_DIR=C:\Espressif\tools\esp-rom-elfs\20241011"

:: Enable the Component Manager so idf_component.yml deps get fetched.
set "IDF_COMPONENT_MANAGER=1"
set "IDF_COMPONENT_REGISTRY_URL=https://components.espressif.com"

:: Component Manager needs IDF_TARGET set (normally injected by CMake).
set "IDF_TARGET=esp32s3"

cd /d C:\hermes_plugins\esp-hermes\firmware
%IDF_PYTHON% %IDF_PATH%\tools\idf.py %*
