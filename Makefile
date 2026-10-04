# Set these to typical values if they're not already set.
PS2DEV ?= /usr/local/ps2dev
PS2SDK ?= $(PS2DEV)/ps2sdk

BIN2C := $(PS2SDK)/bin/bin2c
EE_GCC := $(PS2DEV)/ee/bin/mips64r5900el-ps2-elf-gcc
EE_CCFLAGS := -std=gnu11 -D_EE -Wall
EE_INCLUDES := -I$(PS2SDK)/ee/include -I$(PS2SDK)/common/include
EE_LIBS := -L$(PS2SDK)/ee/lib -lcdvd -ldebug -lpatches -T$(PS2SDK)/ee/startup/linkfile

IRX_LIST := iomanX.irx fileXio.irx bdm.irx bdmfs_fatfs.irx usbd_mini.irx usbmass_bd_mini.irx


EE_SRCS := main.c
EE_OBJS := $(patsubst %.c,build/%.o,$(EE_SRCS))
IRX_OBJS := $(patsubst %.irx,build/%_irx.o,$(IRX_LIST))
EE_C_COMPILE := $(EE_GCC) -c $(EE_CCFLAGS) $(EE_INCLUDES)


.PHONY: all clean
.SECONDARY: $(patsubst %.irx,build/%_irx.c,$(IRX_LIST))

all: build/spc970-scmd4-mechadump.elf

clean:
	rm -rf build

build:
	mkdir -p $@

build/%_irx.c: $(PS2SDK)/iop/irx/%.irx | build
	$(BIN2C) $< $@ $*_irx

build/%.o: build/%.c | build
	$(EE_C_COMPILE) $< -o $@

build/%.o: %.c | build
	$(EE_C_COMPILE) $< -o $@

build/spc970-scmd4-mechadump.elf: $(EE_OBJS) $(IRX_OBJS) | build
	$(EE_GCC) $^ $(EE_LIBS) -o $@
