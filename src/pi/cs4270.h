#pragma once

#include <circle/gpiopin.h>
#include <circle/i2cmaster.h>
#include <circle/types.h>

// The norns shield codec. It runs from its own 12.288 MHz crystal and is the
// I2S clock master at 48 kHz, so the Pi's I2S must run in slave mode.
// Control port: I2C1 address 0x48 (AD0..AD2 tied low). Reset: GPIO17, active low.
class Cs4270 {
public:
    explicit Cs4270(CI2CMaster* i2c);

    // Releases reset and configures master mode, 1x speed, MCLK/LRCK = 256, I2S format.
    boolean Initialize();

private:
    boolean writeReg(u8 reg, u8 value);
    boolean readReg(u8 reg, u8& value);

    CI2CMaster* i2c_;
    CGPIOPin reset_;
};
