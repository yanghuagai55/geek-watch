@echo off
setlocal
set GCC=D:\dev\arm-none-eabi-gcc\bin\arm-none-eabi-gcc.exe
set OBJ=D:\dev\arm-none-eabi-gcc\bin\arm-none-eabi-objcopy.exe
set SIZE=D:\dev\arm-none-eabi-gcc\bin\arm-none-eabi-size.exe
cd /d "%~dp0.."
if not exist stm32\obj mkdir stm32\obj
%GCC% -mcpu=cortex-m3 -mthumb -mfloat-abi=soft -Os -std=c99 -ffunction-sections -fdata-sections -Wall -I ui -I stm32 -c stm32\startup.s -o stm32\obj\startup.o
if errorlevel 1 exit /b 1
%GCC% -mcpu=cortex-m3 -mthumb -mfloat-abi=soft -Os -std=c99 -ffunction-sections -fdata-sections -Wall -I ui -I stm32 -c stm32\syscalls.c -o stm32\obj\syscalls.o
if errorlevel 1 exit /b 1
%GCC% -mcpu=cortex-m3 -mthumb -mfloat-abi=soft -Os -std=c99 -ffunction-sections -fdata-sections -Wall -I ui -I stm32 -c stm32\port.c -o stm32\obj\port.o
if errorlevel 1 exit /b 1
%GCC% -mcpu=cortex-m3 -mthumb -mfloat-abi=soft -Os -std=c99 -ffunction-sections -fdata-sections -Wall -I ui -I stm32 -c stm32\main.c -o stm32\obj\main.o
if errorlevel 1 exit /b 1
%GCC% -mcpu=cortex-m3 -mthumb -mfloat-abi=soft -Os -std=c99 -ffunction-sections -fdata-sections -Wall -I ui -c ui\src\ui_mem.c -o stm32\obj\ui_mem.o
if errorlevel 1 exit /b 1
%GCC% -mcpu=cortex-m3 -mthumb -mfloat-abi=soft -Os -std=c99 -ffunction-sections -fdata-sections -Wall -I ui -c ui\src\ui_anim.c -o stm32\obj\ui_anim.o
if errorlevel 1 exit /b 1
%GCC% -mcpu=cortex-m3 -mthumb -mfloat-abi=soft -Os -std=c99 -ffunction-sections -fdata-sections -Wall -I ui -c ui\src\ui_draw.c -o stm32\obj\ui_draw.o
if errorlevel 1 exit /b 1
%GCC% -mcpu=cortex-m3 -mthumb -mfloat-abi=soft -Os -std=c99 -ffunction-sections -fdata-sections -Wall -I ui -c ui\src\ui_menu.c -o stm32\obj\ui_menu.o
if errorlevel 1 exit /b 1
%GCC% -mcpu=cortex-m3 -mthumb -mfloat-abi=soft -Os -std=c99 -ffunction-sections -fdata-sections -Wall -I ui -c ui\src\ui_trace.c -o stm32\obj\ui_trace.o
if errorlevel 1 exit /b 1
%GCC% -mcpu=cortex-m3 -mthumb -mfloat-abi=soft -Os -std=c99 -ffunction-sections -fdata-sections -Wall -I ui -c ui\src\ui_app.c -o stm32\obj\ui_app.o
if errorlevel 1 exit /b 1
%GCC% -mcpu=cortex-m3 -mthumb -mfloat-abi=soft -Os -std=c99 -ffunction-sections -fdata-sections -Wall -I ui -c ui\src\song_draw.c -o stm32\obj\song_draw.o
if errorlevel 1 exit /b 1
%GCC% -mcpu=cortex-m3 -mthumb -mfloat-abi=soft -Os -std=c99 -ffunction-sections -fdata-sections -Wall -I ui -c ui\src\song_data.c -o stm32\obj\song_data.o
if errorlevel 1 exit /b 1
%GCC% -mcpu=cortex-m3 -mthumb -mfloat-abi=soft -Os -std=c99 -ffunction-sections -fdata-sections -Wall -I ui -c ui\src\font_data.c -o stm32\obj\font_data.o
if errorlevel 1 exit /b 1
%GCC% -mcpu=cortex-m3 -mthumb -mfloat-abi=soft -Os -std=c99 -ffunction-sections -fdata-sections -Wall -I ui -c ui\src\link.c -o stm32\obj\link.o
if errorlevel 1 exit /b 1
%GCC% -mcpu=cortex-m3 -mthumb -mfloat-abi=soft -Os -std=c99 -ffunction-sections -fdata-sections -Wl,--gc-sections -T stm32\stm32.ld -specs=nano.specs -specs=nosys.specs -o stm32\obj\watch.elf stm32\obj\startup.o stm32\obj\syscalls.o stm32\obj\port.o stm32\obj\main.o stm32\obj\ui_mem.o stm32\obj\ui_anim.o stm32\obj\ui_draw.o stm32\obj\ui_menu.o stm32\obj\ui_trace.o stm32\obj\ui_app.o stm32\obj\song_draw.o stm32\obj\song_data.o stm32\obj\font_data.o stm32\obj\link.o -lc -lm -lgcc
if errorlevel 1 exit /b 1
%OBJ% -O binary stm32\obj\watch.elf stm32\obj\watch.bin
%SIZE% stm32\obj\watch.elf
exit /b 0
