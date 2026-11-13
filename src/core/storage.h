#pragma once

#include <stdint.h>

// File names only, such as "state_a.bin". The implementation adds its folder.
class Storage {
public:
    virtual ~Storage() = default;
    virtual bool ready() const = 0;
    virtual bool read(const char* name, uint8_t* buf, int cap, int* len) = 0;
    virtual bool write(const char* name, const uint8_t* buf, int len) = 0;
};
