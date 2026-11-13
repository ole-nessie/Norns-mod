#include "sd_storage.h"

#include <string.h>

bool SdStorage::mount() {
    ready_ = false;
    if (f_mount(&fs_, "SD:", 1) != FR_OK) return false;
    const FRESULT made = f_mkdir("SD:/drumbox");
    if (made != FR_OK && made != FR_EXIST) return false;
    ready_ = true;
    return true;
}

bool SdStorage::path(const char* name, char* out) const {
    const char* prefix = "SD:/drumbox/";
    int n = 0;
    for (; prefix[n]; ++n) out[n] = prefix[n];
    for (int i = 0; name[i] && n < 40; ++i) out[n++] = name[i];
    out[n] = '\0';
    return name[0] != '\0';
}

bool SdStorage::read(const char* name, uint8_t* buf, int cap, int* len) {
    *len = 0;
    if (!ready_) return false;
    char full[48];
    if (!path(name, full)) return false;
    FIL file;
    if (f_open(&file, full, FA_READ) != FR_OK) return false;
    UINT got = 0;
    const bool ok = f_read(&file, buf, static_cast<UINT>(cap), &got) == FR_OK;
    f_close(&file);
    *len = static_cast<int>(got);
    return ok && got > 0;
}

bool SdStorage::write(const char* name, const uint8_t* buf, int len) {
    if (!ready_) return false;
    char full[48];
    if (!path(name, full)) return false;
    FIL file;
    if (f_open(&file, full, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return false;
    UINT wrote = 0;
    const bool ok = f_write(&file, buf, static_cast<UINT>(len), &wrote) == FR_OK && wrote == static_cast<UINT>(len);
    f_sync(&file);
    f_close(&file);
    return ok;
}
