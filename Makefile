KIPS := ldn_mitm
NROS := ldnmitm_config

SUBFOLDERS := Atmosphere-libs/libstratosphere $(KIPS) $(NROS) overlay

OUTDIR		:=	out
SD_ROOT     :=  $(OUTDIR)/sd
NRO_DIR     :=  $(SD_ROOT)/switch/ldnmitm_config
TITLE_DIR   :=  $(SD_ROOT)/atmosphere/contents/4200000000000010
OVERLAY_DIR :=  $(SD_ROOT)/switch/.overlays

ATMOSPHERE_PATCH := docs/atmosphere-libs-mitm-domain-id-collision.patch

# libstratosphere miscompiles under devkitA64's gcc 16.1.0: LDN sessions form
# and play normally, then the console wedges at session end - no error screen,
# network dead, power-hold to recover. Bisected by mixing objects: our code on
# gcc 16 against a gcc 15 libstratosphere is clean, so the fault is in the
# library, not here. It survives dropping our Atmosphere-libs patch, and
# upstream ships 1.11.2 built with the same compiler, so their own sysmodules
# just never reach it. Build with devkitA64 r29.2 / gcc 15.2.0 (what CI uses).
GCC_MAJOR := $(shell (aarch64-none-elf-gcc -dumpversion 2>/dev/null || $(DEVKITPRO)/devkitA64/bin/aarch64-none-elf-gcc -dumpversion 2>/dev/null) | cut -d. -f1)
ifeq ($(shell test "$(GCC_MAJOR)" -ge 16 2>/dev/null && echo yes),yes)
$(warning ==========================================================)
$(warning gcc $(GCC_MAJOR) detected. libstratosphere miscompiles with)
$(warning gcc >= 16 - LDN sessions freeze the console when they end.)
$(warning Use devkitA64 r29.2 / gcc 15.2.0, as CI does.)
$(warning ==========================================================)
endif

all: PACK

# Only `all` packs. Sharing one rule with clean meant `make clean` cleaned every
# subfolder and then ran PACK's cp on the outputs it had just deleted.
clean: $(SUBFOLDERS)
	@ rm -rf $(OUTDIR)

$(SUBFOLDERS):
	$(MAKE) -C $@ $(MAKECMDGOALS)

# The submodule is pinned to its public master; the libstratosphere mitm
# domain-id fix is carried as a patch (see docs/). Apply it before building:
# skipped when already applied (or committed locally), loud failure on drift.
atmosphere-patch:
	@ if git -C Atmosphere-libs apply --check -R ../$(ATMOSPHERE_PATCH) 2>/dev/null; then \
		echo "Atmosphere-libs patch already applied"; \
	else \
		echo "Applying $(ATMOSPHERE_PATCH)"; \
		git -C Atmosphere-libs apply ../$(ATMOSPHERE_PATCH); \
	fi

ifeq (,$(filter clean,$(MAKECMDGOALS)))
Atmosphere-libs/libstratosphere: atmosphere-patch
endif

$(KIPS): Atmosphere-libs/libstratosphere

#---------------------------------------------------------------------------------
PACK: $(SUBFOLDERS)
	@ mkdir -p $(NRO_DIR)
	@ mkdir -p $(TITLE_DIR)/flags
	@ mkdir -p $(OVERLAY_DIR)
	@ cp ldnmitm_config/ldnmitm_config.nro $(NRO_DIR)/ldnmitm_config.nro
	@ cp ldn_mitm/ldn_mitm.nsp $(TITLE_DIR)/exefs.nsp
	@ cp overlay/overlay.ovl $(OVERLAY_DIR)/ldnmitm_config.ovl
	@ cp ldn_mitm/res/toolbox.json $(TITLE_DIR)/toolbox.json
	@ touch $(TITLE_DIR)/flags/boot2.flag
#---------------------------------------------------------------------------------

.PHONY: all clean PACK atmosphere-patch $(SUBFOLDERS)
