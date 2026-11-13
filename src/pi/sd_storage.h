#pragma once

#include <fatfs/ff.h>

#include "storage.h"

// Files live in SD:/drumbox/ on the Pi's SD card. Mount failure leaves the
// synth running; saves then report that the card is unavailable.
class SdStorage : public Storage {
public:
    bool mount();
    bool ready() const override { return ready_; }
    bool read(const char* name, uint8_t* buf, int cap, int* len) override;
    bool write(const char* name, const uint8_t* buf, int len) override;

private:
    bool path(const char* name, char* out) const;

    FATFS fs_{};
    bool ready_ = false;
};
