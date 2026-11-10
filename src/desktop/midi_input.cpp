#include "midi_input.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "RtMidi.h"

namespace {

constexpr const char* kClientName = "drumbox";

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

void onMessage(double, std::vector<unsigned char>* bytes, void* userData) {
    if (bytes->empty()) return;
    MidiMessage msg;
    msg.status = (*bytes)[0];
    if (bytes->size() > 1) msg.data1 = (*bytes)[1];
    if (bytes->size() > 2) msg.data2 = (*bytes)[2];
    static_cast<MidiQueue*>(userData)->push(msg);
}

}  // namespace

MidiInput::MidiInput() = default;
MidiInput::~MidiInput() = default;

void MidiInput::listPorts() {
    try {
        RtMidiIn probe(RtMidi::UNSPECIFIED, kClientName);
        std::printf("MIDI inputs:\n");
        for (unsigned i = 0; i < probe.getPortCount(); ++i) {
            std::printf("  %s\n", probe.getPortName(i).c_str());
        }
    } catch (const RtMidiError& e) {
        std::fprintf(stderr, "midi: %s\n", e.getMessage().c_str());
    }
}

bool MidiInput::addPort(int index, const std::string& name) {
    try {
        Port port;
        port.in = std::make_unique<RtMidiIn>(RtMidi::UNSPECIFIED, kClientName);
        port.queue = std::make_unique<MidiQueue>();
        port.in->ignoreTypes(true, true, true);
        port.in->setCallback(onMessage, port.queue.get());
        if (index < 0) {
            port.in->openVirtualPort(name);
        } else {
            port.in->openPort(index, name);
        }
        ports_.push_back(std::move(port));
        std::fprintf(stderr, "midi: listening on \"%s\"%s\n", name.c_str(),
                     index < 0 ? " (virtual)" : "");
        return true;
    } catch (const RtMidiError& e) {
        std::fprintf(stderr, "midi: could not open \"%s\": %s\n", name.c_str(),
                     e.getMessage().c_str());
        return false;
    }
}

void MidiInput::open(const std::string& portFilter) {
    std::vector<std::string> names;
    try {
        RtMidiIn probe(RtMidi::UNSPECIFIED, kClientName);
        for (unsigned i = 0; i < probe.getPortCount(); ++i) names.push_back(probe.getPortName(i));
    } catch (const RtMidiError& e) {
        std::fprintf(stderr, "midi: %s\n", e.getMessage().c_str());
        return;
    }

    const std::string wanted = lower(portFilter);
    for (size_t i = 0; i < names.size(); ++i) {
        // Skip our own virtual port and ALSA's "Midi Through" loopback.
        const std::string name = lower(names[i]);
        if (name.find(kClientName) != std::string::npos) continue;
        if (name.find("midi through") != std::string::npos) continue;
        if (!wanted.empty() && name.find(wanted) == std::string::npos) continue;
        addPort(static_cast<int>(i), names[i]);
    }
    addPort(-1, kClientName);
}

std::vector<MidiQueue*> MidiInput::queues() const {
    std::vector<MidiQueue*> out;
    for (const Port& p : ports_) out.push_back(p.queue.get());
    return out;
}
