// Desktop simulator: the same synth and parameter page as the Pi build, with
// miniaudio/RtMidi for I/O and the OLED drawn in the terminal.

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include "audio.h"
#include "display.h"
#include "efm_kick.h"
#include "keyboard.h"
#include "midi_input.h"
#include "sid_voice.h"
#include "synth.h"
#include "ui/canvas.h"
#include "ui/param_page.h"

namespace {

constexpr int kKickChannel = 0;  // MIDI channel 1
constexpr int kSidChannel = 1;   // MIDI channel 2
constexpr auto kFrameTime = std::chrono::milliseconds(33);

std::atomic<bool> gRunning{true};

void onSignal(int) { gRunning = false; }

struct Options {
    bool display = true;
    std::string audioDevice;
    std::string midiPort;
    bool list = false;
};

void usage() {
    std::printf(
        "usage: drumbox [options]\n"
        "  --no-display    do not draw the parameter page in the terminal\n"
        "  --audio <name>  playback device name contains <name>\n"
        "  --midi <name>   only open MIDI inputs whose name contains <name>\n"
        "  --list          list audio devices and MIDI inputs, then exit\n");
}

bool parse(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&](std::string& out) {
            if (i + 1 >= argc) return false;
            out = argv[++i];
            return true;
        };
        if (arg == "--no-display") {
            o.display = false;
        } else if (arg == "--audio") {
            if (!value(o.audioDevice)) return false;
        } else if (arg == "--midi") {
            if (!value(o.midiPort)) return false;
        } else if (arg == "--list") {
            o.list = true;
        } else {
            return false;
        }
    }
    return true;
}

// Turns test keys into the same MIDI the Octatrack would send.
class KeyboardController {
public:
    explicit KeyboardController(MidiQueue& out) : out_(out) {}

    // Returns false when the user asked to quit.
    bool handle(int key, const Synth& synth) {
        static const char kNoteKeys[] = "awsedftgyhujk";  // piano layout from C
        if (key == 'q') return false;
        int ch = synth.focusedChannel();
        const char* noteKey = key > 0 ? std::strchr(kNoteKeys, key) : nullptr;
        if (key == 'c') {
            ch = (ch + 1) % 2;
            out_.push({static_cast<uint8_t>(0xB0 | ch), kFirstParamCc,
                       synth.view(ch).value(0)});
            selected_ = 0;
        } else if (key == ' ') {
            releaseNote(ch);
            out_.push({static_cast<uint8_t>(0x90 | ch), 36, 100});
        } else if (key == 'z') {
            releaseNote(ch);
        } else if (noteKey) {
            releaseNote(ch);
            note_ = static_cast<uint8_t>(48 + (noteKey - kNoteKeys));
            out_.push({static_cast<uint8_t>(0x90 | ch), note_, 100});
        } else if (key == 'p') {
            selected_ = (selected_ / kParamsPerPage + 1) % kMaxPages * kParamsPerPage;
            sendParam(ch, synth.view(ch).value(selected_));
        } else if (key >= '1' && key <= '8') {
            selected_ = selected_ / kParamsPerPage * kParamsPerPage + (key - '1');
            sendParam(ch, synth.view(ch).value(selected_));
        } else if (key == '-' || key == '=') {
            const int next = synth.view(ch).value(selected_) + (key == '=' ? 4 : -4);
            sendParam(ch, next < 0 ? 0 : (next > 127 ? 127 : next));
        }
        return true;
    }

private:
    void releaseNote(int ch) {
        if (note_) out_.push({static_cast<uint8_t>(0x80 | ch), note_, 0});
        note_ = 0;
    }

    void sendParam(int ch, int value) {
        out_.push({static_cast<uint8_t>(0xB0 | ch), static_cast<uint8_t>(kFirstParamCc + selected_),
                   static_cast<uint8_t>(value)});
    }

    MidiQueue& out_;
    int selected_ = 0;
    uint8_t note_ = 0;
};

uint32_t nowMs() {
    using namespace std::chrono;
    return static_cast<uint32_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parse(argc, argv, opt)) {
        usage();
        return 2;
    }
    if (opt.list) {
        AudioOutput::listDevices();
        MidiInput::listPorts();
        return 0;
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    static EfmKick kick;
    static Synth synth(static_cast<float>(kSampleRate));
    synth.addVoice(kKickChannel, &kick);
    static SidVoice sid;
    synth.addVoice(kSidChannel, &sid);

    MidiInput midi;
    midi.open(opt.midiPort);
    for (MidiQueue* q : midi.queues()) synth.addQueue(q);
    static MidiQueue keyboardQueue;
    synth.addQueue(&keyboardQueue);

    AudioOutput audio;
    if (!audio.start(synth, opt.audioDevice)) return 1;
    std::fprintf(stderr, "kick on MIDI channel %d, SID on %d, params on CC %d-%d\n",
                 kKickChannel + 1, kSidChannel + 1, kFirstParamCc, kFirstParamCc + kMaxParams - 1);

    TerminalDisplay display;
    const bool showPage = opt.display && isatty(STDOUT_FILENO);
    if (showPage) display.open();

    TerminalKeyboard keyboard;
    KeyboardController keys(keyboardQueue);
    Canvas canvas;
    PageView page(synth);

    while (gRunning) {
        for (int key; (key = keyboard.poll()) >= 0;) {
            if (!keys.handle(key, synth)) gRunning = false;
        }
        if (showPage && page.update(canvas, nowMs())) display.show(canvas);
        std::this_thread::sleep_for(kFrameTime);
    }

    audio.stop();
    return 0;
}
