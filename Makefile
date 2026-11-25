#---------------------------------------------------------------------------------
# Devious Bank - Nintendo 3DS Pokemon storage system
# Based on the devkitPro 3DS application template.
#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

#---------------------------------------------------------------------------------
TARGET		:=	DeviousBank
BUILD		:=	build

PKSMCORE	:=	lib/PKSM-Core

SOURCES		:=	source \
				source/core \
				source/io \
				source/ui \
				$(PKSMCORE)/source/i18n \
				$(PKSMCORE)/source/personal \
				$(PKSMCORE)/source/pkx \
				$(PKSMCORE)/source/sav \
				$(PKSMCORE)/source/utils \
				$(PKSMCORE)/source/wcx \
				$(PKSMCORE)/memecrypto

DATA		:=	data
INCLUDES	:=	include \
				source \
				$(PKSMCORE)/include \
				$(PKSMCORE)/include/enums \
				$(PKSMCORE)/include/personal \
				$(PKSMCORE)/include/pkx \
				$(PKSMCORE)/include/sav \
				$(PKSMCORE)/include/utils \
				$(PKSMCORE)/include/wcx \
				$(PKSMCORE)/memecrypto
GRAPHICS	:=	gfx
GFXBUILD	:=	$(BUILD)
ROMFS		:=	romfs

APP_TITLE		:=	Devious Bank
APP_DESCRIPTION	:=	Pokemon storage for Gen 1-7
APP_AUTHOR		:=	Devious Bank contributors
ICON			:=	assets/cia/icon.png

# CIA packaging (`make cia`, or `.\build.ps1 cia`). makerom and bannertool live in
# tools/bin (not committed); the art comes from tools/make_cia_art.ps1.
VERSION_MAJOR	:=	1
VERSION_MINOR	:=	0
VERSION_MICRO	:=	0
CIA_TOOLS		:=	tools/bin
MAKEROM			:=	$(CIA_TOOLS)/makerom.exe
BANNERTOOL		:=	$(CIA_TOOLS)/bannertool.exe
BANNER_IMAGE	:=	assets/cia/banner.png
BANNER_AUDIO	:=	assets/cia/banner.wav
RSF				:=	cia.rsf

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS	:=	-g -Wall -O2 -mword-relocations \
			-ffunction-sections \
			$(ARCH)

CFLAGS	+=	$(INCLUDE) -D__3DS__

# PKSM-Core needs C++20/23 features, RTTI and exceptions.
CXXFLAGS	:= $(CFLAGS) -std=gnu++23 -fexceptions -frtti -Wno-psabi

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map) -Wl,--gc-sections

LIBS	:= -lcitro2d -lcitro3d -lctru -lm

LIBDIRS	:= $(CTRULIB)

#---------------------------------------------------------------------------------
# no real need to edit anything past this point
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(GRAPHICS),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
PICAFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.v.pica)))
SHLISTFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.shlist)))
GFXFILES	:=	$(foreach dir,$(GRAPHICS),$(notdir $(wildcard $(dir)/*.t3s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

export LD	:=	$(CXX)

export T3XFILES :=  $(GFXFILES:.t3s=.t3x)

export OFILES_SOURCES 	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES)) \
			$(PICAFILES:.v.pica=.shbin.o) $(SHLISTFILES:.shlist=.shbin.o) \
			$(addsuffix .o,$(T3XFILES))

export OFILES := $(OFILES_BIN) $(OFILES_SOURCES)

export HFILES	:=	$(PICAFILES:.v.pica=_shbin.h) $(SHLISTFILES:.shlist=_shbin.h) \
			$(addsuffix .h,$(subst .,_,$(BINFILES))) \
			$(GFXFILES:.t3s=.h)

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export _3DSXDEPS	:=	$(if $(NO_SMDH),,$(OUTPUT).smdh)

ifeq ($(strip $(ICON)),)
	icons := $(wildcard *.png)
	ifneq (,$(findstring $(TARGET).png,$(icons)))
		export APP_ICON := $(TOPDIR)/$(TARGET).png
	else
		ifneq (,$(findstring icon.png,$(icons)))
			export APP_ICON := $(TOPDIR)/icon.png
		endif
	endif
else
	export APP_ICON := $(TOPDIR)/$(ICON)
endif

ifeq ($(strip $(NO_SMDH)),)
	export _3DSXFLAGS += --smdh=$(CURDIR)/$(TARGET).smdh
endif

ifneq ($(ROMFS),)
	export _3DSXFLAGS += --romfs=$(CURDIR)/$(ROMFS)
endif

.PHONY: all clean cia

#---------------------------------------------------------------------------------
all: $(BUILD) $(DEPSDIR)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------
cia: all
	@echo building CIA ...
	@$(BANNERTOOL) makebanner -i $(BANNER_IMAGE) -a $(BANNER_AUDIO) -o $(BUILD)/banner.bnr
	@$(BANNERTOOL) makesmdh -s "$(APP_TITLE)" -l "$(APP_DESCRIPTION)" -p "$(APP_AUTHOR)" \
		-i $(ICON) -o $(BUILD)/icon.icn
	@$(MAKEROM) -f cia -o $(TARGET).cia -target t -exefslogo -rsf $(RSF) \
		-elf $(TARGET).elf -icon $(BUILD)/icon.icn -banner $(BUILD)/banner.bnr \
		-major $(VERSION_MAJOR) -minor $(VERSION_MINOR) -micro $(VERSION_MICRO)
	@echo built ... $(TARGET).cia

$(BUILD):
	@mkdir -p $@

ifneq ($(DEPSDIR),$(BUILD))
$(DEPSDIR):
	@mkdir -p $@
endif

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(OUTPUT).smdh $(TARGET).elf $(TARGET).cia

#---------------------------------------------------------------------------------
$(GFXBUILD)/%.t3x	$(BUILD)/%.h	:	%.t3s
	@echo $(notdir $<)
	@tex3ds -i $< -H $(BUILD)/$*.h -d $(DEPSDIR)/$*.d -o $(GFXBUILD)/$*.t3x

#---------------------------------------------------------------------------------
else

$(OUTPUT).3dsx	:	$(OUTPUT).elf $(_3DSXDEPS)

$(OFILES_SOURCES) : $(HFILES)

$(OUTPUT).elf	:	$(OFILES)

%.bin.o	%_bin.h :	%.bin
	@echo $(notdir $<)
	@$(bin2o)

.PRECIOUS	:	%.t3x %.shbin
%.t3x.o	%_t3x.h :	%.t3x
	$(SILENTMSG) $(notdir $<)
	$(bin2o)

%.shbin.o %_shbin.h : %.shbin
	$(SILENTMSG) $(notdir $<)
	$(bin2o)

-include $(DEPSDIR)/*.d

endif
