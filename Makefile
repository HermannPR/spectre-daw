#---------------------------------------------------------------------------------
# SpectreDAW — devkitARM 3DS homebrew Makefile
# Requires devkitPro with devkitARM and libctru installed.
# Set DEVKITPRO in your environment:
#   Windows: C:\devkitpro  (set via devkitPro installer)
#   Linux/macOS: /opt/devkitpro
#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "DEVKITPRO not set. Install devkitPro and ensure the environment variable is configured.")
endif

TOPDIR		?=	$(CURDIR)

#---------------------------------------------------------------------------------
# Project settings
#---------------------------------------------------------------------------------
TARGET		:=	SpectreDAW
BUILD		:=	build
SOURCES		:=	source
DATA		:=
INCLUDES	:=

# Skip SMDH icon generation — avoids needing a 48×48 icon PNG for the MVP.
# Remove this line and supply SpectreDAW.png to generate a proper SMDH.
NO_SMDH		:=	1

APP_TITLE	:=	SpectreDAW
APP_DESCRIPTION	:=	Homebrew Step Sequencer
APP_AUTHOR	:=	SpectreDAW Dev

#---------------------------------------------------------------------------------
# Compiler flags — ARM11 MPCore hard-float ABI
#---------------------------------------------------------------------------------
ARCH		:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS		:=	-g -Wall -O2 -mword-relocations \
			-fomit-frame-pointer -ffunction-sections \
			$(ARCH)
CFLAGS		+=	$(INCLUDE) -D__3DS__

CXXFLAGS	:=	$(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11

ASFLAGS		:=	-g $(ARCH)

# 3dsx.specs adds the 3DS-specific ELF linker script and crt0.
LDFLAGS		=	-specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

#---------------------------------------------------------------------------------
# Libraries
#   -lctru   : libctru (3DS system library)
#   -lm      : libm   (math, needed for sinf / expf / fmodf)
#---------------------------------------------------------------------------------
LIBS		:=	-lctru -lm

# Explicit fallbacks in case 3ds_rules hasn't set these yet.
CTRULIB		?=	$(DEVKITPRO)/libctru
PORTLIBS	?=	$(DEVKITPRO)/portlibs/3ds

LIBDIRS		:=	$(PORTLIBS) $(CTRULIB)

#---------------------------------------------------------------------------------
# Include the devkitARM 3DS build rules.
# Provides $(CC), $(CXX), $(AS), $(NM), 3dsxtool, and compiler tool paths.
#---------------------------------------------------------------------------------
include $(DEVKITARM)/3ds_rules

#---------------------------------------------------------------------------------
# Everything below this line is derived from the standard devkitPro
# application template and should not require modification.
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

ifeq ($(strip $(CPPFILES)),)
	export LD	:=	$(CC)
else
	export LD	:=	$(CXX)
endif

export OFILES_SOURCES	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES		:=	$(OFILES_BIN) $(OFILES_SOURCES)
export HFILES		:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE		:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
				$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
				-I$(CURDIR)/$(BUILD)

export LIBPATHS		:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

ifeq ($(strip $(NO_SMDH)),)
	export _3DSXFLAGS	+=	--smdh=$(CURDIR)/$(TARGET).smdh
endif

.PHONY: clean all

all: $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

$(BUILD):
	@mkdir -p $@

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(OUTPUT).smdh $(TARGET).elf

else

#---------------------------------------------------------------------------------
DEPENDS	:=	$(OFILES:.o=.d)

ifeq ($(strip $(NO_SMDH)),)
$(OUTPUT).3dsx: $(OUTPUT).elf $(OUTPUT).smdh
else
$(OUTPUT).3dsx: $(OUTPUT).elf
endif

$(OFILES_SOURCES): $(HFILES)

%.3dsx: %.elf
	@3dsxtool $< $@ $(_3DSXFLAGS)
	@echo "built ... $(notdir $@)"

%.smdh: $(TOPDIR)/icon.png
	@smdhtool --create "$(APP_TITLE)" "$(APP_DESCRIPTION)" "$(APP_AUTHOR)" $< $@
	@echo "built ... $(notdir $@)"

$(OUTPUT).elf: $(OFILES)
	@echo linking $(notdir $@)
	@$(LD) $(LDFLAGS) $(OFILES) $(LIBPATHS) $(LIBS) -o $@
	@$(NM) -CSn $@ > $(notdir $*.lst)

#---------------------------------------------------------------------------------
# Compilation rules — must be explicit; 3ds_rules does not provide them.
#---------------------------------------------------------------------------------
%.o: %.c
	@echo $(notdir $<)
	@$(CC) -MMD -MP -MF $(DEPSDIR)/$*.d $(CFLAGS) -c $< -o $@

%.o: %.cpp
	@echo $(notdir $<)
	@$(CXX) -MMD -MP -MF $(DEPSDIR)/$*.d $(CXXFLAGS) -c $< -o $@

%.o: %.s
	@echo $(notdir $<)
	@$(CC) -MMD -MP -MF $(DEPSDIR)/$*.d -x assembler-with-cpp $(ASFLAGS) -c $< -o $@

-include $(DEPENDS)

endif
#---------------------------------------------------------------------------------
