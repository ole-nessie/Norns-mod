#include "kernel.h"

#include <circle/machineinfo.h>

#include "ui/param_page.h"

namespace {

const char kFrom[] = "drumbox";

constexpr int kKickChannel = 0;           // MIDI channel 1
constexpr int kSidChannel = 1;            // MIDI channel 2
constexpr unsigned kSpiClockHz = 16000000;
constexpr unsigned kFrameMs = 33;         // OLED refresh, about 30 fps

}  // namespace

CKernel* CKernel::instance_ = nullptr;

CKernel::CKernel()
    : cpu_(CPUSpeedMaximum),
      screen_(options_.GetWidth(), options_.GetHeight()),
      timer_(&interrupt_),
      logger_(options_.GetLogLevel(), &timer_),
      usb_(&interrupt_, &timer_, TRUE),  // TRUE: plug-and-play
      i2c_(CMachineInfo::Get()->GetDevice(DeviceI2CMaster), TRUE),
      spi_(kSpiClockHz, 0, 0, 0),
      // With USE_USB_FIQ the FIQ belongs to USB, so the UART uses the IRQ.
      serial_(&interrupt_, FALSE),
      codec_(&i2c_),
      oled_(&spi_),
      synth_(static_cast<float>(kSampleRate)) {
    instance_ = this;
    actLed_.Blink(3);
}

void CKernel::enterStage(Stage stage) {
    stage_ = stage;
    logger_.Write(kFrom, LogNotice, "stage %d", static_cast<int>(stage));
}

void CKernel::BlinkFailedStage() {
    // Repeats "N blinks, pause" forever, N = the stage that failed.
    for (;;) {
        actLed_.Blink(static_cast<unsigned>(stage_), 200, 300);
        CTimer::SimpleMsDelay(2000);
    }
}

boolean CKernel::Initialize() {
    // The HDMI log is optional; without a framebuffer, log into the void.
    enterStage(Stage::System);
    const boolean haveScreen = screen_.Initialize();
    boolean ok = logger_.Initialize(haveScreen ? static_cast<CDevice*>(&screen_) : &nullLog_);
    if (ok) ok = interrupt_.Initialize();
    if (ok) ok = timer_.Initialize();
    if (!ok) return FALSE;

    enterStage(Stage::Display);
    if (!spi_.Initialize() || !oled_.Initialize()) return FALSE;
    showMessage("DRUMBOX", "starting...");

    enterStage(Stage::I2c);
    if (!i2c_.Initialize()) {
        showMessage("I2C FAILED", "");
        return FALSE;
    }

    // MIDI on UART is optional hardware; the rest works without it.
    serialReady_ = serial_.Initialize(31250);

    enterStage(Stage::Usb);
    showMessage("DRUMBOX", "usb...");
    if (!usb_.Initialize()) {
        showMessage("USB FAILED", "");
        return FALSE;
    }

    enterStage(Stage::Codec);
    showMessage("DRUMBOX", "codec...");
    if (!codec_.Initialize()) {
        showMessage("CODEC FAILED", "check CS4270/I2C");
        return FALSE;
    }

    synth_.addVoice(kKickChannel, &kick_);
    synth_.addVoice(kSidChannel, &sid_);
    synth_.addQueue(&usbQueue_);
    synth_.addQueue(&serialQueue_);

    enterStage(Stage::Audio);
    showMessage("DRUMBOX", "audio...");
    sound_ = new DrumboxSound(&interrupt_, &synth_);
    if (!sound_->Start()) {
        showMessage("AUDIO FAILED", "no I2S clock?");
        return FALSE;
    }
    enterStage(Stage::Running);

    logger_.Write(kFrom, LogNotice, "ready: kick on MIDI channel %d, SID on %d, CC %u-%u",
                  kKickChannel + 1, kSidChannel + 1, kFirstParamCc, kFirstParamCc + kMaxParams - 1);
    return TRUE;
}

void CKernel::showMessage(const char* line1, const char* line2) {
    canvas_.clear();
    canvas_.text(0, 20, line1, 15);
    canvas_.text(0, 34, line2, 8);
    oled_.Show(canvas_);
}

void CKernel::usbMidiPacket(unsigned, u8* packet, unsigned length) {
    if (length < 2 || !(packet[0] & 0x80) || packet[0] >= 0xF0) return;
    MidiMessage msg;
    msg.status = packet[0];
    msg.data1 = packet[1];
    msg.data2 = length > 2 ? packet[2] : 0;
    instance_->usbQueue_.push(msg);
}

void CKernel::usbMidiRemoved(CDevice* device, void*) {
    if (instance_->usbMidi_ == device) instance_->usbMidi_ = nullptr;
}

void CKernel::attachUsbMidi() {
    if (usbMidi_) return;
    auto* midi = static_cast<CUSBMIDIDevice*>(CDeviceNameService::Get()->GetDevice("umidi1", FALSE));
    if (!midi) return;
    midi->RegisterRemovedHandler(usbMidiRemoved);
    midi->RegisterPacketHandler(usbMidiPacket);
    usbMidi_ = midi;
    logger_.Write(kFrom, LogNotice, "USB MIDI device attached");
}

void CKernel::pollSerialMidi() {
    if (!serialReady_) return;
    u8 bytes[64];
    const int n = serial_.Read(bytes, sizeof bytes);
    MidiMessage msg;
    for (int i = 0; i < n; ++i) {
        if (serialParser_.feed(bytes[i], msg)) serialQueue_.push(msg);
    }
}

void CKernel::Run() {
    PageView page(synth_);
    unsigned lastFrame = 0;
    unsigned lastBeat = 0;
    boolean ledOn = FALSE;

    for (;;) {
        // Slow heartbeat on the green LED: the kernel is running.
        const unsigned nowBeat = static_cast<unsigned>(CTimer::GetClockTicks64() / (CLOCKHZ / 1000));
        if (nowBeat - lastBeat >= 500) {
            lastBeat = nowBeat;
            ledOn = !ledOn;
            if (ledOn) actLed_.On(); else actLed_.Off();
        }

        // Must run at task level to notice USB devices being plugged in.
        if (usb_.UpdatePlugAndPlay()) attachUsbMidi();
        pollSerialMidi();

        const unsigned nowMs = static_cast<unsigned>(CTimer::GetClockTicks64() / (CLOCKHZ / 1000));
        if (nowMs - lastFrame >= kFrameMs) {
            lastFrame = nowMs;
            if (page.update(canvas_, nowMs)) oled_.Show(canvas_);
        }
    }
}
