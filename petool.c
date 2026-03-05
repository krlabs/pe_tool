/*
 * PETool - PE Icon & Vendor Info Editor
 * ======================================
 * Змінює іконку EXE-файлу та копіює Version Info
 * з іншого PE-файлу.
 *
 * Збірка:
 *   cl /nologo /W4 /O2 /Fe:petool.exe petool.c
 *
 * Використання:
 *   petool.exe <target.exe> -i <icon.ico>           - змінити іконку
 *   petool.exe <target.exe> -v <source.exe>          - скопіювати vendor info
 *   petool.exe <target.exe> -i <icon.ico> -v <source.exe>  - обидва
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#pragma comment(lib, "version.lib")

/* ============================================================
 * ICO file structures
 * ============================================================ */

#pragma pack(push, 2)

typedef struct {
    WORD reserved;   /* 0 */
    WORD type;       /* 1 = icon */
    WORD count;      /* number of images */
} ICONDIR;

typedef struct {
    BYTE  width;
    BYTE  height;
    BYTE  colorCount;
    BYTE  reserved;
    WORD  planes;
    WORD  bitCount;
    DWORD bytesInRes;
    DWORD imageOffset;   /* offset in .ico file */
} ICONDIRENTRY;

/* PE resource version of icon dir entry */
typedef struct {
    BYTE  width;
    BYTE  height;
    BYTE  colorCount;
    BYTE  reserved;
    WORD  planes;
    WORD  bitCount;
    DWORD bytesInRes;
    WORD  nID;           /* resource ID */
} GRPICONDIRENTRY;

typedef struct {
    WORD reserved;
    WORD type;
    WORD count;
    /* GRPICONDIRENTRY entries[] follows */
} GRPICONDIR;

#pragma pack(pop)

/* ============================================================
 * Replace Icon
 * ============================================================
 *
 * ICO -> PE resource mapping:
 *   ICO file has: ICONDIR + ICONDIRENTRY[] + raw image data
 *   PE needs:     RT_GROUP_ICON (GRPICONDIR) + RT_ICON for each image
 *
 * Steps:
 *   1. Read and parse .ico file
 *   2. BeginUpdateResource on target exe
 *   3. Write each icon image as RT_ICON (ID = 1, 2, 3...)
 *   4. Build GRPICONDIR and write as RT_GROUP_ICON
 *   5. EndUpdateResource
 * ============================================================ */

static BOOL replace_icon(const char *exe_path, const char *ico_path)
{
    printf("[*] Reading icon: %s\n", ico_path);

    /* Read .ico file */
    FILE *f = fopen(ico_path, "rb");
    if (!f) {
        printf("[-] Cannot open: %s\n", ico_path);
        return FALSE;
    }

    fseek(f, 0, SEEK_END);
    long ico_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    unsigned char *ico_data = (unsigned char *)malloc(ico_size);
    if (!ico_data) {
        fclose(f);
        return FALSE;
    }
    fread(ico_data, 1, ico_size, f);
    fclose(f);

    /* Parse ICO header */
    ICONDIR *dir = (ICONDIR *)ico_data;

    if (dir->reserved != 0 || dir->type != 1 || dir->count == 0) {
        printf("[-] Invalid ICO file\n");
        free(ico_data);
        return FALSE;
    }

    printf("[+] Icon contains %d image(s)\n", dir->count);

    ICONDIRENTRY *entries = (ICONDIRENTRY *)(ico_data + sizeof(ICONDIR));

    /* Begin resource update */
    HANDLE hUpdate = BeginUpdateResourceA(exe_path, FALSE);
    if (!hUpdate) {
        printf("[-] BeginUpdateResource failed: %lu\n", GetLastError());
        free(ico_data);
        return FALSE;
    }

    /* Write each icon image as RT_ICON resource */
    for (int i = 0; i < dir->count; i++) {
        WORD icon_id = (WORD)(i + 1);

        if (entries[i].imageOffset + entries[i].bytesInRes > (DWORD)ico_size) {
            printf("    [!] Icon image %d exceeds file size, skipping\n", i);
            continue;
        }

        BOOL ok = UpdateResourceA(
            hUpdate,
            RT_ICON,
            MAKEINTRESOURCEA(icon_id),
            MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
            ico_data + entries[i].imageOffset,
            entries[i].bytesInRes
        );

        if (!ok) {
            printf("    [-] Failed to update RT_ICON %d: %lu\n", i, GetLastError());
        } else {
            printf("    [+] RT_ICON %d: %dx%d, %d bytes\n",
                   icon_id,
                   entries[i].width  ? entries[i].width  : 256,
                   entries[i].height ? entries[i].height : 256,
                   entries[i].bytesInRes);
        }
    }

    /* Build RT_GROUP_ICON */
    DWORD grp_size = sizeof(GRPICONDIR) + dir->count * sizeof(GRPICONDIRENTRY);
    unsigned char *grp_data = (unsigned char *)malloc(grp_size);
    memset(grp_data, 0, grp_size);

    GRPICONDIR *grp = (GRPICONDIR *)grp_data;
    grp->reserved = 0;
    grp->type = 1;
    grp->count = dir->count;

    GRPICONDIRENTRY *grp_entries = (GRPICONDIRENTRY *)(grp_data + sizeof(GRPICONDIR));

    for (int i = 0; i < dir->count; i++) {
        grp_entries[i].width      = entries[i].width;
        grp_entries[i].height     = entries[i].height;
        grp_entries[i].colorCount = entries[i].colorCount;
        grp_entries[i].reserved   = 0;
        grp_entries[i].planes     = entries[i].planes;
        grp_entries[i].bitCount   = entries[i].bitCount;
        grp_entries[i].bytesInRes = entries[i].bytesInRes;
        grp_entries[i].nID        = (WORD)(i + 1);
    }

    BOOL ok = UpdateResourceA(
        hUpdate,
        RT_GROUP_ICON,
        MAKEINTRESOURCEA(1),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
        grp_data,
        grp_size
    );

    if (!ok) {
        printf("[-] Failed to update RT_GROUP_ICON: %lu\n", GetLastError());
    } else {
        printf("[+] RT_GROUP_ICON updated\n");
    }

    free(grp_data);
    free(ico_data);

    /* Commit */
    if (!EndUpdateResourceA(hUpdate, FALSE)) {
        printf("[-] EndUpdateResource failed: %lu\n", GetLastError());
        return FALSE;
    }

    printf("[+] Icon replaced successfully\n");
    return TRUE;
}

/* ============================================================
 * Copy Version Info
 * ============================================================
 *
 * Steps:
 *   1. GetFileVersionInfoSize / GetFileVersionInfo from source
 *   2. UpdateResource(RT_VERSION) on target
 * ============================================================ */

static BOOL copy_version_info(const char *target_path, const char *source_path)
{
    printf("[*] Reading version info from: %s\n", source_path);

    /* Get version info size from source */
    DWORD dummy = 0;
    DWORD ver_size = GetFileVersionInfoSizeA(source_path, &dummy);

    if (ver_size == 0) {
        printf("[-] No version info found in: %s (error %lu)\n",
               source_path, GetLastError());
        return FALSE;
    }

    printf("[+] Version info size: %lu bytes\n", ver_size);

    /* Read version info */
    void *ver_data = malloc(ver_size);
    if (!ver_data) return FALSE;

    if (!GetFileVersionInfoA(source_path, 0, ver_size, ver_data)) {
        printf("[-] GetFileVersionInfo failed: %lu\n", GetLastError());
        free(ver_data);
        return FALSE;
    }

    /* Print some info from source */
    VS_FIXEDFILEINFO *ffi = NULL;
    UINT ffi_len = 0;
    if (VerQueryValueA(ver_data, "\\", (LPVOID *)&ffi, &ffi_len) && ffi) {
        printf("[*] Source version: %d.%d.%d.%d\n",
               HIWORD(ffi->dwFileVersionMS), LOWORD(ffi->dwFileVersionMS),
               HIWORD(ffi->dwFileVersionLS), LOWORD(ffi->dwFileVersionLS));
    }

    /* Read string values */
    struct { const char *key; const char *label; } fields[] = {
        { "CompanyName",      "Company" },
        { "FileDescription",  "Description" },
        { "ProductName",      "Product" },
        { "LegalCopyright",   "Copyright" },
        { "OriginalFilename", "Original Name" },
        { NULL, NULL }
    };

    for (int i = 0; fields[i].key; i++) {
        char query[256];
        /* Try common code pages: 040904B0 (EN/Unicode), 040904E4 (EN/CP1252) */
        const char *blocks[] = { "040904B0", "040904E4", "000004B0", NULL };
        for (int b = 0; blocks[b]; b++) {
            sprintf(query, "\\StringFileInfo\\%s\\%s", blocks[b], fields[i].key);
            char *value = NULL;
            UINT value_len = 0;
            if (VerQueryValueA(ver_data, query, (LPVOID *)&value, &value_len) && value_len > 0) {
                printf("    %-15s: %s\n", fields[i].label, value);
                break;
            }
        }
    }

    /* Write to target */
    HANDLE hUpdate = BeginUpdateResourceA(target_path, FALSE);
    if (!hUpdate) {
        printf("[-] BeginUpdateResource failed: %lu\n", GetLastError());
        free(ver_data);
        return FALSE;
    }

    BOOL ok = UpdateResourceA(
        hUpdate,
        RT_VERSION,
        MAKEINTRESOURCEA(VS_VERSION_INFO),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
        ver_data,
        ver_size
    );

    if (!ok) {
        printf("[-] UpdateResource RT_VERSION failed: %lu\n", GetLastError());
        EndUpdateResourceA(hUpdate, TRUE);  /* discard */
        free(ver_data);
        return FALSE;
    }

    if (!EndUpdateResourceA(hUpdate, FALSE)) {
        printf("[-] EndUpdateResource failed: %lu\n", GetLastError());
        free(ver_data);
        return FALSE;
    }

    free(ver_data);
    printf("[+] Version info copied successfully\n");
    return TRUE;
}

/* ============================================================
 * Usage
 * ============================================================ */

static void print_usage(const char *prog)
{
    printf("PETool - PE Icon & Vendor Info Editor\n");
    printf("Red Team 3.0 - Educational Demo\n\n");
    printf("Usage:\n");
    printf("  %s <target.exe> -i <icon.ico>              Change icon\n", prog);
    printf("  %s <target.exe> -v <source.exe>             Copy vendor info\n", prog);
    printf("  %s <target.exe> -i <icon.ico> -v <source.exe>  Both\n", prog);
    printf("\nExamples:\n");
    printf("  %s stub.exe -i myicon.ico\n", prog);
    printf("  %s stub.exe -v C:\\Windows\\notepad.exe\n", prog);
    printf("  %s stub.exe -i shield.ico -v C:\\Windows\\explorer.exe\n", prog);
}

/* ============================================================
 * Main
 * ============================================================ */

int main(int argc, char *argv[])
{
    if (argc < 4) {
        print_usage(argv[0]);
        return 1;
    }

    const char *target = argv[1];
    const char *ico_path = NULL;
    const char *vendor_src = NULL;

    /* Parse args */
    for (int i = 2; i < argc - 1; i++) {
        if (strcmp(argv[i], "-i") == 0) {
            ico_path = argv[++i];
        } else if (strcmp(argv[i], "-v") == 0) {
            vendor_src = argv[++i];
        }
    }

    if (!ico_path && !vendor_src) {
        print_usage(argv[0]);
        return 1;
    }

    printf("=== PETool ===\n");
    printf("Target: %s\n\n", target);

    /* Verify target exists */
    DWORD attr = GetFileAttributesA(target);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        printf("[-] Target not found: %s\n", target);
        return 1;
    }

    BOOL success = TRUE;

    /* Replace icon */
    if (ico_path) {
        printf("--- Icon Replacement ---\n");
        if (!replace_icon(target, ico_path)) {
            success = FALSE;
        }
        printf("\n");
    }

    /* Copy version info */
    if (vendor_src) {
        printf("--- Version Info Copy ---\n");
        if (!copy_version_info(target, vendor_src)) {
            success = FALSE;
        }
        printf("\n");
    }

    if (success) {
        printf("=== Done! ===\n");
    } else {
        printf("=== Completed with errors ===\n");
    }

    return success ? 0 : 1;
}
