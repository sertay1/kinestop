#---------------------------------------------------------------------------------
# Kinestop sysmodule (dot renderer). Modification of the original Kinestop by
# SertAy (https://github.com/sertay1/Kinestop); build layout follows ChargeCap.
#
# Output: out/kinestop.nsp (exefs PFS0 holding main + main.npdm), installed as
# atmosphere/contents/4200000000001042/exefs.nsp.
#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=/opt/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

#---------------------------------------------------------------------------------
TARGET   := kinestop
BUILD    := build
OUTDIR   := out
SOURCES  := source ../common
INCLUDES := source ../common
EXEFS_SRC := exefs_src

# Only these are taken from ../common (the overlay owns everything else there).
COMMON_CPPFILES := config.cpp dots.cpp

APP_VERSION := 2.0.0

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
DEFINES := -DAPP_VERSION="\"$(APP_VERSION)\""

ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

# -Os plus aggressive section splitting: this module lives in the System pool
# and should be as small as the toolchain will let it be.
CFLAGS := -Wall -Wextra -Os \
	-ffunction-sections -fdata-sections -fomit-frame-pointer \
	-fno-asynchronous-unwind-tables -fno-unwind-tables -fno-stack-protector \
	$(ARCH) $(DEFINES)

CFLAGS += $(INCLUDE) -D__SWITCH__

CXXFLAGS := $(CFLAGS) -std=gnu++20 -fno-rtti -fno-exceptions -fno-threadsafe-statics
CFLAGS   += -std=gnu11

ASFLAGS := $(ARCH)
LDFLAGS  = -specs=$(DEVKITPRO)/libnx/switch.specs $(ARCH) -Wl,-Map,$(notdir $*.map) \
	-Wl,--gc-sections -Wl,--as-needed

LIBS := -lnx

LIBDIRS := $(PORTLIBS) $(LIBNX)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT := $(CURDIR)/$(OUTDIR)/$(TARGET)
export TOPDIR := $(CURDIR)

export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))

export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES   := $(notdir $(wildcard source/*.c))
CPPFILES := $(notdir $(wildcard source/*.cpp)) $(COMMON_CPPFILES)
SFILES   := $(notdir $(wildcard source/*.s))

ifeq ($(strip $(CPPFILES)),)
	export LD := $(CC)
else
	export LD := $(CXX)
endif

export OFILES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)

# libnx/portlibs as system headers: -Wextra stays on for our code without
# drowning the build in warnings from libnx's C headers compiled as C++.
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
	$(foreach dir,$(LIBDIRS),-isystem $(dir)/include) \
	-I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export BUILD_EXEFS_SRC := $(TOPDIR)/$(EXEFS_SRC)
export APP_JSON := $(TOPDIR)/kinestop.json

.PHONY: $(BUILD) clean all

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@[ -d $(OUTDIR) ] || mkdir -p $(OUTDIR)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(OUTDIR) main main.npdm

#---------------------------------------------------------------------------------
else
.PHONY: all

DEPENDS := $(OFILES:.o=.d)

all: $(OUTPUT).nsp

$(OUTPUT).nsp: $(OUTPUT).nso $(OUTPUT).npdm

$(OUTPUT).nso: $(OUTPUT).elf

$(OUTPUT).npdm: $(APP_JSON)

$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
