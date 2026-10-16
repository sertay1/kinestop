#---------------------------------------------------------------------------------
# Kinestop - modified version of the original Kinestop by SertAy
# (https://github.com/sertay1/Kinestop): a single overlay that runs in
# Ultrahand's default 4 MB, with the settings panel and the dot HUD as two modes.
#
#   make            build overlay/kinestop.ovl
#   make dist       build and lay out an SD-card-ready tree in sdcard/
#   make zip        build and produce Kinestop.zip (the sdcard/ tree, zipped)
#   make test       run the host-side tests in tests/
#   make clean      clean the build, the tests and the packaging output
#---------------------------------------------------------------------------------

PYTHON  ?= python3
PACKAGE := scripts/package_sd.py

.PHONY: all overlay dist zip test clean

all: overlay

overlay:
	@echo "===> Building overlay (kinestop.ovl)..."
	@$(MAKE) --no-print-directory -C overlay

dist: all
	@echo "===> Packaging SD card tree..."
	@$(PYTHON) $(PACKAGE) .

zip: all
	@echo "===> Packaging SD card tree + Kinestop.zip..."
	@$(PYTHON) $(PACKAGE) . --zip

test:
	@$(MAKE) --no-print-directory -C tests

clean:
	@$(MAKE) --no-print-directory -C overlay clean
	@$(MAKE) --no-print-directory -C tests clean
	@rm -rf sdcard Kinestop.zip
