@echo off
setlocal
set "BATDIR=%~dp0"
if not defined ARM_GCC_BIN set "ARM_GCC_BIN=D:\dev\arm-none-eabi-gcc\bin"
set "GCC=%ARM_GCC_BIN%\arm-none-eabi-gcc.exe"
set "AR=%ARM_GCC_BIN%\arm-none-eabi-ar.exe"
set "OBJCOPY=%ARM_GCC_BIN%\arm-none-eabi-objcopy.exe"
set "SIZE=%ARM_GCC_BIN%\arm-none-eabi-size.exe"
set "ROOT=%BATDIR%.."
set "OUT=%BATDIR%obj"
set "CMSIS=%BATDIR%libraries\cmsis\cm4"
set "LIB=%BATDIR%libraries"
set "ARCH=-mcpu=cortex-m4 -mthumb -mfloat-abi=soft -DAT32F421K8T7"
set "CFLAGS=%ARCH% -Os -std=c99 -ffunction-sections -fdata-sections -fno-common -Wall -Wextra"
set "INC=-I "%ROOT%\ui" -I "%BATDIR%." -I "%BATDIR%inc" -I "%BATDIR%bsp" -I "%LIB%\drivers\inc" -I "%CMSIS%\core_support" -I "%CMSIS%\device_support""
if not exist "%OUT%" mkdir "%OUT%"
if errorlevel 1 exit /b 1

"%GCC%" %ARCH% -c "%CMSIS%\device_support\startup\gcc\startup_at32f421.s" -o "%OUT%\startup.o"
if errorlevel 1 exit /b 1

for %%S in (system_at32f421) do (
    "%GCC%" %CFLAGS% %INC% -c "%CMSIS%\device_support\%%S.c" -o "%OUT%\%%S.o"
    if errorlevel 1 exit /b 1
)
for %%S in (crm gpio spi dma usart misc flash) do (
    "%GCC%" %CFLAGS% %INC% -c "%LIB%\drivers\src\at32f421_%%S.c" -o "%OUT%\%%S.o"
    if errorlevel 1 exit /b 1
)
for %%S in (port) do (
    "%GCC%" %CFLAGS% %INC% -c "%BATDIR%%%S.c" -o "%OUT%\%%S.o"
    if errorlevel 1 exit /b 1
)
"%GCC%" %CFLAGS% %INC% -c "%BATDIR%src\main.c" -o "%OUT%\main.o"
if errorlevel 1 exit /b 1

if exist "%OUT%\libui.a" del "%OUT%\libui.a"
for %%S in (ui_mem ui_anim ui_draw ui_menu ui_trace ui_app ui_icons song_draw song_data font_data link) do (
    "%GCC%" %CFLAGS% -I "%ROOT%\ui" -c "%ROOT%\ui\src\%%S.c" -o "%OUT%\%%S.o"
    if errorlevel 1 exit /b 1
    "%AR%" rcs "%OUT%\libui.a" "%OUT%\%%S.o"
    if errorlevel 1 exit /b 1
)

"%GCC%" %ARCH% -Os -Wl,--gc-sections -Wl,-Map="%OUT%\watch.map" -T "%BATDIR%AT32F421x8_FLASH.ld" -specs=nano.specs -specs=nosys.specs -o "%OUT%\watch.elf" "%OUT%\startup.o" "%OUT%\system_at32f421.o" "%OUT%\crm.o" "%OUT%\gpio.o" "%OUT%\spi.o" "%OUT%\dma.o" "%OUT%\usart.o" "%OUT%\misc.o" "%OUT%\flash.o" "%OUT%\port.o" "%OUT%\main.o" "%OUT%\libui.a" -lc -lm -lgcc
if errorlevel 1 exit /b 1
"%OBJCOPY%" -O binary "%OUT%\watch.elf" "%OUT%\watch.bin"
if errorlevel 1 exit /b 1
"%SIZE%" "%OUT%\watch.elf"
if errorlevel 1 exit /b 1
exit /b 0
