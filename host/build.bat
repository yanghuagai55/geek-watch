@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1
cd /d "%~dp0.."
if not exist host\obj mkdir host\obj
cl /nologo /W4 /O2 /utf-8 /D_CRT_SECURE_NO_WARNINGS host\gen_font.c /Fe:host\obj\gen_font.exe /Fo:host\obj\ /link user32.lib gdi32.lib
if errorlevel 1 exit /b 1
host\obj\gen_font.exe ui\src\font_data.c host\font_preview.bmp
if errorlevel 1 exit /b 1
cl /nologo /W4 /O2 /utf-8 /D_CRT_SECURE_NO_WARNINGS host\gen_clock.c /Fe:host\obj\gen_clock.exe /Fo:host\obj\ /link user32.lib gdi32.lib
if errorlevel 1 exit /b 1
host\obj\gen_clock.exe STHupo ui\src\song_data.c host\obj\clock_preview.bmp 64
if errorlevel 1 exit /b 1
cl /nologo /W4 /O2 /utf-8 /D_CRT_SECURE_NO_WARNINGS /I ui /I host host\main.c host\hal_host.c host\panel.c host\self_test.c ui\src\ui_mem.c ui\src\ui_anim.c ui\src\ui_draw.c ui\src\ui_menu.c ui\src\ui_app.c ui\src\ui_trace.c ui\src\font_data.c ui\src\song_data.c ui\src\song_draw.c ui\src\link.c /Fe:host\watch_ui.exe /Fo:host\obj\ /link user32.lib gdi32.lib winmm.lib
exit /b %errorlevel%
