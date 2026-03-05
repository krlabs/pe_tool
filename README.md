# PETool
PETool - PE Icon & Vendor Info Editor

## Features

- **Icon Replacement** — inject any `.ico` file into an existing `.exe`, supports multi-resolution icons (16x16 → 256x256)
- **Version Info Cloning** — copy full vendor metadata (Company, Description, Copyright, Product, Version) from one PE to another
- **Single pass** — both operations can be applied in one command
- **No recompilation needed** — modify already-built executables directly
- **Zero dependencies** — pure C, uses only Windows API (`UpdateResource`, `GetFileVersionInfo`)

## Build

Requires Visual Studio (any edition) or MSVC Build Tools.

Open **Developer Command Prompt** and run:

```batch
cl /nologo /W4 /O2 /D_CRT_SECURE_NO_WARNINGS /Fe:petool.exe petool.c
```
## Usage

```
petool.exe stub.exe -i myicon.ico # замінити іконку
petool.exe stub.exe -v C:\Windows\notepad.exe # склонувати vendor info зі стороннього додатку
petool.exe stub.exe -i myicon.ico -v C:\Windows\explorer.exe # виконати обидва одразу
```
