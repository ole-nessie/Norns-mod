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
      emmc_(&interrupt_, &timer_, nullptr),
      i2c_(CMachineInfo::Get()->GetDevice(DeviceI2CMaster), TRUE),
      spi_(kSpiClockHz, 0, 0, 0),
      // With USE_USB_FIQ the FIQ belongs to USB, so the UART uses the IRQ.
      serial_(&interrupt_, FALSE),
      codec_(&i2c_),
      oled_(&spi_),
      synth_(static_cast<float>(kSampleRate)),
      console_(synth_, &sd_) {
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

    showMessage("DRUMBOX", "card...");
    if (!emmc_.Initialize() || !sd_.mount()) {
        showMessage("NO SD CARD", "saves off");
        CTimer::SimpleMsDelay(800);
    }

    pool_.prepare(static_cast<float>(kSampleRate));
    synth_.setPool(&pool_);
    synth_.perform({SynthOp::SetMachine, static_cast<uint8_t>(kKickChannel),
                    static_cast<int8_t>(kMachineEfmDrum), 0});
    synth_.perform({SynthOp::SetMachine, static_cast<uint8_t>(kSidChannel),
                    static_cast<int8_t>(kMachineSid), 0});
    console_.loadStartup();
    console_.arm(static_cast<uint32_t>(CTimer::GetClockTicks64() / (CLOCKHZ / 1000)));
    for (MidiQueue& queue : usbQueues_) synth_.addQueue(&queue);
    synth_.addQueue(&serialQueue_);

    enterStage(Stage::Audio);
    showMessage("DRUMBOX", "audio...");
    sound_ = new DrumboxSound(&interrupt_, &synth_);
    if (!sound_->Start()) {
        showMessage("AUDIO FAILED", "no I2S clock?");
        return FALSE;
    }
    enterStage(Stage::Running);

    logger_.Write(kFrom, LogNotice, "ready: EFM drums on MIDI channel %d, SID on %d, CC %u-%u",
                  kKickChannel + 1, kSidChannel + 1, kFirstParamCc, kFirstParamCc + kMaxParams - 1);
    return TRUE;
}

void CKernel::showMessage(const char* line1, const char* line2) {
    canvas_.clear();
    canvas_.text(0, 20, line1, 15);
    canvas_.text(0, 34, line2, 8);
    oled_.Show(canvas_);
}

void CKernel::usbMidiPacket(unsigned, u8* packet, unsigned length, unsigned device, void*) {
    if (device < 1 || device > kUsbMidiPorts) return;
    if (length < 2 || !(packet[0] & 0x80) || packet[0] >= 0xF0) return;
    MidiMessage msg;
    msg.status = packet[0];
    msg.data1 = packet[1];
    msg.data2 = length > 2 ? packet[2] : 0;
    instance_->usbQueues_[device - 1].push(msg);
}

void CKernel::usbMidiRemoved(CDevice* device, void* context) {
    const unsigned index = static_cast<unsigned>(reinterpret_cast<unsigned long>(context));
    if (index < kUsbMidiPorts && instance_->usbMidi_[index] == device) {
        instance_->usbMidi_[index] = nullptr;
    }
}

void CKernel::attachUsbMidi() {
    for (unsigned i = 0; i < kUsbMidiPorts; ++i) {
        if (usbMidi_[i]) continue;
        auto* midi = static_cast<CUSBMIDIDevice*>(
            CDeviceNameService::Get()->GetDevice("umidi", i + 1, FALSE));
        if (!midi) continue;
        midi->RegisterRemovedHandler(usbMidiRemoved,
                                     reinterpret_cast<void*>(static_cast<unsigned long>(i)));
        midi->RegisterPacketHandler(usbMidiPacket, nullptr);
        usbMidi_[i] = midi;
        logger_.Write(kFrom, LogNotice, "USB MIDI device umidi%u attached", i + 1);
    }
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
    panel_.begin();
    unsigned lastFrame = 0;
    unsigned lastBeat = 0;
    boolean ledOn = FALSE;
    boolean menuWasOpen = FALSE;

    for (;;) {
        // Slow heartbeat on the green LED: the kernel is running.
        const unsigned nowBeat = static_cast<unsigned>(CTimer::GetClockTicks64() / (CLOCKHZ / 1000));
        if (nowBeat - lastBeat >= 500) {
            lastBeat = nowBeat;
            ledOn = !ledOn;
            if (ledOn) actLed_.On(); else actLed_.Off();
        }

        // Must run at task level to notice USB devices being plugged in.
        // Also scans once per loop: a device can finish enumerating without a
        // later plug event, and a removed slot has to be picked up again.
        usb_.UpdatePlugAndPlay();
        attachUsbMidi();
        pollSerialMidi();

        const unsigned nowMs = static_cast<unsigned>(CTimer::GetClockTicks64() / (CLOCKHZ / 1000));
        PanelEvent events[8];
        const int nEvents = panel_.poll(events, 8, nowMs);
        for (int i = 0; i < nEvents; ++i) console_.handle(events[i], nowMs);
        if (nowMs - lastFrame >= kFrameMs) {
            lastFrame = nowMs;
            console_.tick(nowMs);
            if (console_.open()) {
                menuWasOpen = TRUE;
                if (console_.update(canvas_, nowMs)) oled_.Show(canvas_);
            } else {
                if (menuWasOpen) {
                    menuWasOpen = FALSE;
                    page.invalidate();
                }
                if (page.update(canvas_, nowMs)) oled_.Show(canvas_);
            }
        }
    }
}
