@echo off
setlocal
set "BATDIR=%~dp0"
set "ROOT=%~dp0.."
set "REGEN=0"
set "ODD=0"
set "FULL=0"
set "NOSONG=0"
:args
if "%~1"=="" goto setup
if /i "%~1"=="regen" (set "REGEN=1") else if /i "%~1"=="--odd" (set "ODD=1") else if /i "%~1"=="full" (set "FULL=1") else if /i "%~1"=="--full" (set "FULL=1") else if /i "%~1"=="nosong" (set "NOSONG=1") else if /i "%~1"=="--nosong" (set "NOSONG=1") else (
    echo Usage: build.bat [regen] [--odd] [full] [nosong]
    exit /b 2
)
shift
goto args
:setup
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1
cd /d "%ROOT%"
if not exist host\obj mkdir host\obj
rem Default links the subset font; "full" links the complete CJK table.
set "FONT_SRC=ui\src\font_data.c"
if "%FULL%"=="1" set "FONT_SRC=ui\src\font_data_full.c"
set "SONG_SRC=ui\src\song_data.c"
if "%NOSONG%"=="1" set "SONG_SRC=ui\src\song_data_stub.c"
rem Checked-in font data is the default. Regeneration is explicitly opt-in.
if "%REGEN%"=="1" (
    call :fonts
    if errorlevel 1 exit /b 1
)
call :build 128 160 128 host\watch_ui.exe
if errorlevel 1 exit /b 1
call :build 240 240 240 host\watch_ui_240.exe
if errorlevel 1 exit /b 1
if "%ODD%"=="1" (
    call :build 128 163 odd host\obj\watch_ui_odd.exe
    if errorlevel 1 exit /b 1
)
exit /b 0

:build
if not exist host\obj\%3 mkdir host\obj\%3
cl /nologo /W4 /O2 /utf-8 /D_CRT_SECURE_NO_WARNINGS /DHAL_LCD_W=%1 /DHAL_LCD_H=%2 /I ui /I host host\main.c host\hal_host.c host\panel.c host\self_test.c host\render_test.c ui\src\ui_mem.c ui\src\ui_anim.c ui\src\ui_draw.c ui\src\ui_menu.c ui\src\ui_app.c ui\src\ui_trace.c ui\src\ui_icons.c %FONT_SRC% %SONG_SRC% ui\src\song_draw.c ui\src\link.c ui\src\sport.c ui\src\music.c ui\src\settings.c /Fe:%4 /Fo:host\obj\%3\ /Fd:host\obj\%3\compiler.pdb /link user32.lib gdi32.lib winmm.lib
exit /b %errorlevel%

:fonts
rem NOTE: regen overwrites ui\src\font_data.c with the FULL table; run
rem       "python host\subset_font.py" afterwards to restore the subset.
if not exist host\obj\generators mkdir host\obj\generators
cl /nologo /W4 /O2 /utf-8 /D_CRT_SECURE_NO_WARNINGS host\gen_font.c /Fe:host\obj\generators\gen_font.exe /Fo:host\obj\generators\ /link user32.lib gdi32.lib
if errorlevel 1 exit /b 1
host\obj\generators\gen_font.exe ui\src\font_data.c host\font_preview.bmp
if errorlevel 1 exit /b 1
cl /nologo /W4 /O2 /utf-8 /D_CRT_SECURE_NO_WARNINGS host\gen_clock.c /Fe:host\obj\generators\gen_clock.exe /Fo:host\obj\generators\ /link user32.lib gdi32.lib
if errorlevel 1 exit /b 1
host\obj\generators\gen_clock.exe STHupo ui\src\song_data.c host\obj\clock_preview.bmp 64
exit /b %errorlevel%
