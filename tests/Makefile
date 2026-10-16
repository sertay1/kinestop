#---------------------------------------------------------------------------------
# Kinestop host unit tests. Modification of the original Kinestop by SertAy
# (https://github.com/sertay1/Kinestop): tests for the sysmodule/overlay split.
#
#   make -C tests          build and run every test (non-zero exit on failure)
#   make -C tests bins     build only
#   make -C tests clean
#
# Uses the host compiler only; no devkitPro needed. The code under test is the
# real common/config.cpp, common/dots.cpp and the header-only
# sysmodule/source/{swizzle,raster}.hpp.
#---------------------------------------------------------------------------------

HOSTCXX  ?= g++
HOSTCC   ?= gcc

ROOT     := ..
BUILD    := build

CPPFLAGS := -I$(ROOT)/common -I$(ROOT)/sysmodule/source -MMD -MP
CXXFLAGS := -std=c++20 -Wall -Wextra -O1 -g
CFLAGS   := -std=c11 -Wall -Wextra -O1 -g

TESTS    := test_swizzle test_dots test_config test_ipc test_raster
BINS     := $(addprefix $(BUILD)/,$(TESTS))

COMMON_OBJS := $(BUILD)/config.o $(BUILD)/dots.o

.PHONY: all bins run clean

all: run

bins: $(BINS)

run: $(BINS)
	@failed=0; \
	for t in $(TESTS); do \
		if [ "$$t" = test_ipc ]; then ./$(BUILD)/$$t $(ROOT) || failed=1; \
		else ./$(BUILD)/$$t || failed=1; fi; \
	done; \
	if [ $$failed -ne 0 ]; then echo "host tests: FAILED"; exit 1; fi; \
	echo "host tests: all passed"

$(BUILD):
	@mkdir -p $@

$(BUILD)/%.o: $(ROOT)/common/%.cpp | $(BUILD)
	$(HOSTCXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD)/%.o: %.cpp | $(BUILD)
	$(HOSTCXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD)/%.o: %.c | $(BUILD)
	$(HOSTCC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/test_swizzle: $(BUILD)/test_swizzle.o
	$(HOSTCXX) $^ -o $@

$(BUILD)/test_raster: $(BUILD)/test_raster.o
	$(HOSTCXX) $^ -o $@

$(BUILD)/test_dots: $(BUILD)/test_dots.o $(COMMON_OBJS)
	$(HOSTCXX) $^ -o $@

$(BUILD)/test_config: $(BUILD)/test_config.o $(COMMON_OBJS)
	$(HOSTCXX) $^ -o $@

$(BUILD)/test_ipc: $(BUILD)/test_ipc.o $(BUILD)/ipc_c_sizes.o
	$(HOSTCXX) $^ -o $@

clean:
	rm -rf $(BUILD)

-include $(wildcard $(BUILD)/*.d)
