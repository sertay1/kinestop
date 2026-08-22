#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

TARGET      := kinestop
BUILD       := build
SOURCES     := source ../common
DATA        := data
INCLUDES    := include source ../common

ARCH    := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS  := -g -Wall -O2 -ffunction-sections $(ARCH) $(DEFINES)
CFLAGS  += $(INCLUDE) -D__SWITCH__

CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=c++20

ASFLAGS := -g $(ARCH)
LDFLAGS = -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS    := -lnx

LIBDIRS := $(PORTLIBS) $(LIBNX)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)

export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                   $(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR  := $(CURDIR)/$(BUILD)

CFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(CURDIR)/$(dir)/*.c)))
CPPFILES    := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(CURDIR)/$(dir)/*.cpp)))
SFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(CURDIR)/$(dir)/*.s)))
BINFILES    := $(foreach dir,$(DATA),$(notdir $(wildcard $(CURDIR)/$(dir)/*.*)))

ifeq ($(strip $(CPPFILES)),)
    export LD   := $(CC)
else
    export LD   := $(CXX)
endif

export OFILES_BIN   := $(addsuffix .o,$(BINFILES))
export OFILES_SRC   := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES       := $(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN   := $(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE  := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                   $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                   -I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: $(BUILD) clean all

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f "$(TOPDIR)/Makefile"

clean:
	@rm -fr $(BUILD) $(TARGET).elf $(TARGET).nso $(TARGET).npdm main main.npdm $(TARGET).map

else

DEPENDS := $(OFILES:.o=.d)

all: $(TOPDIR)/main $(TOPDIR)/main.npdm

$(TOPDIR)/main: $(OUTPUT).nso
	@cp $< $@
	@echo "Created ... $(notdir $@)"

$(TOPDIR)/main.npdm: $(OUTPUT).npdm
	@cp $< $@
	@echo "Created ... $(notdir $@)"

$(OUTPUT).nso: $(OUTPUT).elf
	@elf2nso $< $@
	@echo "built ... $(notdir $@)"

$(OUTPUT).npdm: $(TOPDIR)/kinestop.json
	@npdmtool $< $@
	@echo "built ... $(notdir $@)"

$(OUTPUT).elf: $(OFILES)
	@echo Linking $@...
	$(LD) $(LDFLAGS) $(OFILES) $(LIBPATHS) $(LIBS) -o $@

$(OFILES_SRC): $(HFILES_BIN)

%.o: %.cpp
	@echo Compiling $<...
	$(CXX) -MMD -MP -MF $(DEPSDIR)/$*.d $(CXXFLAGS) $(INCLUDE) -c $< -o $@

-include $(DEPENDS)

endif
