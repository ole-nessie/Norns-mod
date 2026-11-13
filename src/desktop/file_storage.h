#pragma once

#include <string>

#include "storage.h"

// Saves under ~/.drumbox/ so the simulator can test sets without the Pi.
class FileStorage : public Storage {
public:
    FileStorage();
    bool ready() const override { return ready_; }
    bool read(const char* name, uint8_t* buf, int cap, int* len) override;
    bool write(const char* name, const uint8_t* buf, int len) override;

private:
    std::string path(const char* name) const;

    std::string dir_;
    bool ready_ = false;
};
