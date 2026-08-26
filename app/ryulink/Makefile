# RyuLink Homebrew application.
.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

APP_TITLE   := 龙联
APP_AUTHOR  := RyuLink contributors
# Single source for NACP metadata, the query parameter and HTTP User-Agent.
RYULINK_APP_VERSION := 0.1.8
APP_VERSION := $(RYULINK_APP_VERSION)

TARGET      := RyuLink
BUILD       := build
SOURCES     := source
INCLUDES    := include

# Homebrew Menu requires an ASET section with NACP metadata to enumerate an
# NRO as an application. A missing icon is fine; missing NACP is not.
export NACP := $(TOPDIR)/$(TARGET).nacp
export NROFLAGS := --nacp=$(NACP) --icon=$(TOPDIR)/icon.png

ARCH        := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS      := -g -Wall -Wextra -O2 -ffunction-sections $(ARCH) $(INCLUDE) -I$(PORTLIBS)/include/freetype2 -DRYULINK_APP_VERSION=\"$(RYULINK_APP_VERSION)\"
CXXFLAGS    := $(CFLAGS) -fno-exceptions -std=c++20
LDFLAGS     := -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) \
               -Wl,-Map,$(notdir $*.map)
LIBS        := -lcurl -lfreetype -lbz2 -lz -lpng16 -lharfbuzz -lm -lnx
LIBDIRS     := $(PORTLIBS) $(LIBNX)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT  := $(CURDIR)/$(TARGET)
export TOPDIR  := $(CURDIR)
export VPATH   := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES    := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
export OFILES_SRC := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES     := $(OFILES_SRC)

ifeq ($(strip $(CPPFILES)),)
export LD := $(CC)
else
export LD := $(CXX)
endif

export INCLUDE  := $(foreach dir,$(INCLUDES),-I$(TOPDIR)/$(dir)) \
                   $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(TOPDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: all clean

all:
	@[ -d $(BUILD) ] || mkdir -p $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@rm -fr $(BUILD) $(TARGET).elf $(TARGET).nro $(TARGET).nacp

else

DEPENDS := $(OFILES:.o=.d)

.PHONY: all
all: $(OUTPUT).nro

$(NACP): $(TOPDIR)/Makefile
	@nacptool --create "$(APP_TITLE)" "$(APP_AUTHOR)" "$(APP_VERSION)" $@

$(OUTPUT).nro: $(NACP)

$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

endif
