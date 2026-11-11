@echo off
setlocal
set "BATDIR=%~dp0"
set "FULL=0"
set "NOSONG=0"
:args
if "%~1"=="" goto args_done
if /i "%~1"=="full" (set "FULL=1") else if /i "%~1"=="--full" (set "FULL=1") else if /i "%~1"=="nosong" (set "NOSONG=1") else if /i "%~1"=="--nosong" (set "NOSONG=1") else (
    echo Usage: build.bat [full] [nosong]
    exit /b 2
)
shift
goto args
:args_done
if not defined ARM_GCC_BIN set "ARM_GCC_BIN=D:\dev\arm-none-eabi-gcc\bin"
set "GCC=%ARM_GCC_BIN%\arm-none-eabi-gcc.exe"
set "AR=%ARM_GCC_BIN%\arm-none-eabi-ar.exe"
set "OBJCOPY=%ARM_GCC_BIN%\arm-none-eabi-objcopy.exe"
set "SIZE=%ARM_GCC_BIN%\arm-none-eabi-size.exe"
set "NM=%ARM_GCC_BIN%\arm-none-eabi-nm.exe"
set "ROOT=%BATDIR%.."
set "OUT=%BATDIR%obj"
set "ARCH=-mcpu=cortex-m3 -mthumb -mfloat-abi=soft"
set "CFLAGS=%ARCH% -Os -std=c99 -ffunction-sections -fdata-sections -fno-common -fstack-usage -Wall -Wextra -Werror=implicit-function-declaration"
if not exist "%OUT%" mkdir "%OUT%"
if errorlevel 1 exit /b 1

"%GCC%" %ARCH% -c "%BATDIR%startup.s" -o "%OUT%\startup.o"
if errorlevel 1 exit /b 1
for %%S in (syscalls port main) do (
    "%GCC%" %CFLAGS% -I "%ROOT%\ui" -I "%BATDIR%." -c "%BATDIR%%%S.c" -o "%OUT%\%%S.o"
    if errorlevel 1 exit /b 1
)

rem Rebuild a reusable Cortex-M3/soft-float UI archive without stale members.
rem Default links the subset font; "full" links the complete CJK table.
set "FONT_SRC=font_data"
if "%FULL%"=="1" set "FONT_SRC=font_data_full"
rem "nosong" drops the 华文琥珀 tables; song_draw falls back to the dot font.
set "SONG_SRC=song_data"
if "%NOSONG%"=="1" set "SONG_SRC=song_data_stub"
if exist "%OUT%\libui.a" del "%OUT%\libui.a"
for %%S in (ui_mem ui_anim ui_draw ui_menu ui_trace ui_app ui_icons song_draw %SONG_SRC% %FONT_SRC% link) do (
    "%GCC%" %CFLAGS% -I "%ROOT%\ui" -c "%ROOT%\ui\src\%%S.c" -o "%OUT%\%%S.o"
    if errorlevel 1 exit /b 1
    "%AR%" rcs "%OUT%\libui.a" "%OUT%\%%S.o"
    if errorlevel 1 exit /b 1
)

"%GCC%" %ARCH% -Os -Wl,--gc-sections -Wl,-Map="%OUT%\watch.map",--cref -T "%BATDIR%stm32.ld" -specs=nano.specs -specs=nosys.specs -o "%OUT%\watch.elf" "%OUT%\startup.o" "%OUT%\syscalls.o" "%OUT%\port.o" "%OUT%\main.o" "%OUT%\libui.a" -lc -lm -lgcc
if errorlevel 1 exit /b 1
"%OBJCOPY%" -O binary "%OUT%\watch.elf" "%OUT%\watch.bin"
if errorlevel 1 exit /b 1
"%SIZE%" "%OUT%\watch.elf"
if errorlevel 1 exit /b 1
"%SIZE%" -A "%OUT%\watch.elf" > "%OUT%\watch.size"
if errorlevel 1 exit /b 1
"%NM%" -S --size-sort "%OUT%\watch.elf" > "%OUT%\watch.symbols"
if errorlevel 1 exit /b 1
rem Per-function stack estimates are in obj\*.su; they are not stack high-water measurements.
exit /b 0
