#include "cs4270.h"

#include <circle/logger.h>
#include <circle/timer.h>

namespace {

const char kFrom[] = "cs4270";

constexpr u8 kAddress = 0x48;
constexpr unsigned kResetPin = 17;

// Register map and bits as in Linux sound/soc/codecs/cs4270.c.
constexpr u8 kRegChipId = 0x01;
constexpr u8 kRegPower = 0x02;
constexpr u8 kRegMode = 0x03;
constexpr u8 kRegFormat = 0x04;
constexpr u8 kRegTransition = 0x05;
constexpr u8 kRegMute = 0x06;
constexpr u8 kRegVolA = 0x07;
constexpr u8 kRegVolB = 0x08;

constexpr u8 kPowerDownAll = 0x23;     // PDN_ADC | PDN_DAC | PDN
constexpr u8 kModeMaster1x = 0x00;     // single speed, MCLK divide 1 (12.288 MHz / 48 kHz = 256)
constexpr u8 kFormatI2s = 0x08 | 0x01; // DAC I2S, ADC I2S

}  // namespace

Cs4270::Cs4270(CI2CMaster* i2c) : i2c_(i2c), reset_(kResetPin, GPIOModeOutput) {
    reset_.Write(LOW);
}

boolean Cs4270::writeReg(u8 reg, u8 value) {
    const u8 buf[2] = {reg, value};
    return i2c_->Write(kAddress, buf, sizeof buf) == static_cast<int>(sizeof buf);
}

boolean Cs4270::readReg(u8 reg, u8& value) {
    if (i2c_->Write(kAddress, &reg, 1) != 1) return FALSE;
    return i2c_->Read(kAddress, &value, 1) == 1;
}

boolean Cs4270::Initialize() {
    CTimer::SimpleMsDelay(2);
    reset_.Write(HIGH);
    CTimer::SimpleMsDelay(1);

    // The control port has to be claimed (PDN set) soon after reset is
    // released, otherwise the chip falls back to stand-alone mode.
    if (!writeReg(kRegPower, kPowerDownAll)) {
        CLogger::Get()->Write(kFrom, LogError, "no answer at I2C address 0x%02X", kAddress);
        return FALSE;
    }

    u8 id = 0;
    if (!readReg(kRegChipId, id) || (id & 0xF0) != 0xC0) {
        CLogger::Get()->Write(kFrom, LogError, "unexpected chip id 0x%02X", id);
        return FALSE;
    }

    const boolean ok = writeReg(kRegMode, kModeMaster1x)
                       && writeReg(kRegFormat, kFormatI2s)
                       && writeReg(kRegTransition, 0x00)  // no soft ramp / zero-cross delay
                       && writeReg(kRegMute, 0x00)        // auto-mute off, nothing muted
                       && writeReg(kRegVolA, 0x00)        // 0 dB attenuation
                       && writeReg(kRegVolB, 0x00)
                       && writeReg(kRegPower, 0x00);      // power up
    if (!ok) {
        CLogger::Get()->Write(kFrom, LogError, "configuration failed");
        return FALSE;
    }

    CLogger::Get()->Write(kFrom, LogNotice, "codec ready (rev %u)", id & 0x0F);
    return TRUE;
}
