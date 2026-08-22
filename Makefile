.PHONY: all clean overlay package

all: overlay package

overlay:
	@echo "===> Building Overlay (kinestop.ovl)..."
	@$(MAKE) -C overlay

package: overlay
	@echo "===> Packaging SD Card Release..."
	@python scripts/package_sd.py .

clean:
	@$(MAKE) -C overlay clean
