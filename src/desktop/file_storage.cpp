#include "file_storage.h"

#include <sys/stat.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>

FileStorage::FileStorage() {
    const char* home = std::getenv("HOME");
    dir_ = home ? std::string(home) + "/.drumbox" : ".drumbox";
    ready_ = mkdir(dir_.c_str(), 0755) == 0 || errno == EEXIST;
}

std::string FileStorage::path(const char* name) const { return dir_ + "/" + name; }

bool FileStorage::read(const char* name, uint8_t* buf, int cap, int* len) {
    *len = 0;
    if (!ready_) return false;
    FILE* f = std::fopen(path(name).c_str(), "rb");
    if (!f) return false;
    *len = static_cast<int>(std::fread(buf, 1, static_cast<size_t>(cap), f));
    std::fclose(f);
    return *len > 0;
}

bool FileStorage::write(const char* name, const uint8_t* buf, int len) {
    if (!ready_) return false;
    FILE* f = std::fopen(path(name).c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(buf, 1, static_cast<size_t>(len), f) == static_cast<size_t>(len);
    std::fflush(f);
    std::fclose(f);
    return ok;
}
