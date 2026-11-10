#include "midi.h"

namespace {

int dataBytesFor(uint8_t status) {
    const uint8_t type = status & 0xF0;
    return (type == 0xC0 || type == 0xD0) ? 1 : 2;
}

}  // namespace

bool MidiParser::feed(uint8_t byte, MidiMessage& out) {
    if (byte >= 0xF8) return false;  // real-time bytes may appear anywhere
    if (byte >= 0xF0) {              // system common / sysex cancels running status
        status_ = 0;
        count_ = 0;
        return false;
    }
    if (byte & 0x80) {
        status_ = byte;
        count_ = 0;
        return false;
    }
    if (!status_) return false;

    data_[count_++] = byte;
    if (count_ < dataBytesFor(status_)) return false;

    out.status = status_;
    out.data1 = data_[0];
    out.data2 = count_ > 1 ? data_[1] : 0;
    count_ = 0;  // keep status_ for running status
    return true;
}
