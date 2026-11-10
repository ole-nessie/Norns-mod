#pragma once

#include <memory>
#include <string>
#include <vector>

#include "midi.h"

class RtMidiIn;

// Opens hardware MIDI inputs plus a virtual "drumbox" input. Each port gets
// its own queue so every queue keeps exactly one producer thread.
class MidiInput {
public:
    MidiInput();
    ~MidiInput();

    static void listPorts();

    // Opens every input whose name contains `portFilter` (case-insensitive);
    // empty opens all of them.
    void open(const std::string& portFilter);

    std::vector<MidiQueue*> queues() const;

private:
    struct Port {
        std::unique_ptr<RtMidiIn> in;
        std::unique_ptr<MidiQueue> queue;
    };
    bool addPort(int index, const std::string& name);

    std::vector<Port> ports_;
};
