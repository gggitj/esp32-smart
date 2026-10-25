@echo off
setlocal
set IDF_PATH=S:\esp\esp-idf
set IDF_TOOLS_PATH=S:\esp\.espressif
set CMAKE_PROGRAM_PATH=S:\esp\.espressif\tools\ninja\1.12.1;S:\esp\.espressif\tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin
call S:\esp\esp-idf\export.bat
if errorlevel 1 exit /b 1
set IDF_CCACHE_ENABLE=0
cd /d "%~dp0"
"S:\esp\.espressif\python_env\idf5.5_py3.10_env\Scripts\python.exe" "%IDF_PATH%\tools\idf.py" -B build_lvgl -DCMAKE_MAKE_PROGRAM=S:/esp/.espressif/tools/ninja/1.12.1/ninja.exe build
