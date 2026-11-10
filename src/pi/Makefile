# Bare-metal drumbox for Raspberry Pi 3 + norns shield (Circle, AArch64).
# Run scripts/setup-pi.sh once first; scripts/build-pi.sh builds the SD card files.

# The toolchain (pi-deps/toolchain/bin) must be on PATH; build-pi.sh does that.

ROOT       := $(abspath ../..)
CIRCLEHOME ?= $(ROOT)/pi-deps/circle

CORE = ../core

OBJS = main.o kernel.o cs4270.o ssd1322.o drumbox_sound.o \
       $(CORE)/midi.o $(CORE)/synth.o $(CORE)/efm_kick.o $(CORE)/sid_voice.o \
       $(CORE)/ui/canvas.o $(CORE)/ui/param_page.o

INCLUDE += -I $(CORE) -I $(ROOT)/third_party

LIBS = $(CIRCLEHOME)/lib/usb/libusb.a \
       $(CIRCLEHOME)/lib/input/libinput.a \
       $(CIRCLEHOME)/lib/fs/libfs.a \
       $(CIRCLEHOME)/lib/sound/libsound.a \
       $(CIRCLEHOME)/lib/libcircle.a

EXTRACLEAN = $(CORE)/*.o $(CORE)/*.d $(CORE)/ui/*.o $(CORE)/ui/*.d

include $(CIRCLEHOME)/Rules.mk

-include $(DEPS)
