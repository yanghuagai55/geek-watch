@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "%~dp0.."
cl /nologo /W4 /O2 /utf-8 /D_CRT_SECURE_NO_WARNINGS /I ui host\sport_test.c ui\src\sport.c /Fe:host\obj\sport_test.exe /Fo:host\obj\
if errorlevel 1 exit /b 1
host\obj\sport_test.exe