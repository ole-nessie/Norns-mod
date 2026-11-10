#pragma once

#include <circle/gpiopin.h>
#include <circle/spimaster.h>
#include <circle/types.h>

class Canvas;

// The norns shield OLED (NHD-2.7-12864WDW3, SSD1322 controller):
// SPI0 with CE0, D/C on GPIO5, reset on GPIO6.
class Ssd1322 {
public:
    explicit Ssd1322(CSPIMaster* spi);

    boolean Initialize();
    void Show(const Canvas& canvas);

private:
    void command(u8 cmd, const u8* data = nullptr, unsigned len = 0);

    CSPIMaster* spi_;
    CGPIOPin dc_;
    CGPIOPin reset_;
    boolean on_ = FALSE;
    u8 frame_[128 * 64];
};
