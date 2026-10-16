#---------------------------------------------------------------------------------
# Kinestop - modified version of the original Kinestop by SertAy
# (https://github.com/sertay1/Kinestop): dot rendering moved into a sysmodule,
# the overlay is a settings panel.
#
#   make            build sysmodule + overlay
#   make sysmodule  build sysmodule/out/kinestop.nsp
#   make overlay    build overlay/kinestop.ovl
#   make dist       build and lay out an SD-card-ready tree in sdcard/
#   make zip        build and produce Kinestop.zip (the sdcard/ tree, zipped)
#   make test       run the host-side tests in tests/ (if present)
#   make clean      clean both builds and the packaging output
#---------------------------------------------------------------------------------

PYTHON  ?= python3
PACKAGE := scripts/package_sd.py

.PHONY: all sysmodule overlay dist zip test clean

all: sysmodule overlay

sysmodule:
	@echo "===> Building sysmodule (kinestop.nsp)..."
	@$(MAKE) --no-print-directory -C sysmodule

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
	@if [ -f tests/Makefile ]; then \
		$(MAKE) --no-print-directory -C tests; \
	else \
		echo "tests/Makefile not found - no host tests to run (skipping)."; \
	fi

clean:
	@$(MAKE) --no-print-directory -C sysmodule clean
	@$(MAKE) --no-print-directory -C overlay clean
	@if [ -f tests/Makefile ]; then $(MAKE) --no-print-directory -C tests clean; fi
	@rm -rf sdcard Kinestop.zip
