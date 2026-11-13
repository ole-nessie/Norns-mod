#pragma once

#include <circle/actled.h>
#include <circle/cputhrottle.h>
#include <circle/devicenameservice.h>
#include <circle/exceptionhandler.h>
#include <circle/i2cmaster.h>
#include <circle/interrupt.h>
#include <circle/koptions.h>
#include <circle/logger.h>
#include <circle/nulldevice.h>
#include <circle/screen.h>
#include <circle/serial.h>
#include <circle/spimaster.h>
#include <circle/timer.h>
#include <circle/types.h>
#include <circle/usb/usbhcidevice.h>
#include <circle/usb/usbmidi.h>
#include <SDCard/emmc.h>

#include "cs4270.h"
#include "drumbox_sound.h"
#include "machines.h"
#include "midi.h"
#include "panel.h"
#include "sd_storage.h"
#include "ssd1322.h"
#include "synth.h"
#include "ui/canvas.h"
#include "ui/console.h"

class CKernel {
public:
    CKernel();

    boolean Initialize();
    void Run();

    // After a failed Initialize(): blinks the green LED N times in a loop,
    // N = the stage that failed. Never returns.
    void BlinkFailedStage();

private:
    enum class Stage { System = 1, Display, I2c, Usb, Codec, Audio, Running };

    void enterStage(Stage stage);

    void attachUsbMidi();
    void pollSerialMidi();
    void showMessage(const char* line1, const char* line2);

    static void usbMidiPacket(unsigned cable, u8* packet, unsigned length, unsigned device,
                              void* param);
    static void usbMidiRemoved(CDevice* device, void* context);

    // Construction order matters (Circle's system objects first).
    CActLED actLed_;
    CKernelOptions options_;
    CDeviceNameService deviceNames_;
    CCPUThrottle cpu_;      // the Pi 3 boots at a reduced clock otherwise
    CScreenDevice screen_;  // HDMI log output, handy for debugging
    CNullDevice nullLog_;
    CExceptionHandler exceptions_;
    CInterruptSystem interrupt_;
    CTimer timer_;
    CLogger logger_;
    CUSBHCIDevice usb_;
    CEMMCDevice emmc_;
    CI2CMaster i2c_;
    CSPIMaster spi_;
    CSerialDevice serial_;  // DIN MIDI in on the UART RX pin (GPIO15), 31250 baud

    Cs4270 codec_;
    Ssd1322 oled_;
    Canvas canvas_;

    SdStorage sd_;
    MachinePool pool_;
    Synth synth_;
    Console console_;
    Panel panel_;
    DrumboxSound* sound_ = nullptr;

    static constexpr int kUsbMidiPorts = 4;
    MidiQueue usbQueues_[kUsbMidiPorts];  // one producer IRQ per USB device
    MidiQueue serialQueue_;               // producer: main loop
    MidiParser serialParser_;
    CUSBMIDIDevice* usbMidi_[kUsbMidiPorts] = {};
    boolean serialReady_ = FALSE;
    Stage stage_ = Stage::System;

    static CKernel* instance_;
};
