#-------------------------------------------------------------------------------
.SUFFIXES:
#-------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

WUMS_ROOT := $(DEVKITPRO)/wums

TOPDIR ?= $(CURDIR)

include $(DEVKITPRO)/wut/share/wut_rules

#-------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# DATA is a list of directories containing data files
# INCLUDES is a list of directories containing header files
#-------------------------------------------------------------------------------
TARGET		:=	NUSspli
BUILD		?=	debug

SOURCES		:=	zlib/contrib/minizip \
				src/menu \
				src

DATA		:=	
INCLUDES	:=	include \
				zlib/contrib/minizip

#-------------------------------------------------------------------------------
# options for code generation
#-------------------------------------------------------------------------------

CFLAGS		:=	$(MACHDEP) -Ofast -flto=auto -fno-fat-lto-objects \
				-fuse-linker-plugin -fipa-pta -pipe \
				-Wall -Wextra -Wundef -Wshadow -Wpointer-arith \
				-Wcast-align -Wno-trigraphs -Wno-empty-body \
				-Wno-maybe-uninitialized -Wno-pointer-sign \
				-Wno-implicit-fallthrough \
				-D__WIIU__ -D__WUT__ -DIOAPI_NO_64 -D__unix__

CXXFLAGS	:=	$(CFLAGS) -std=c++20 -fpermissive
ASFLAGS		:=	-g $(ARCH)
LDFLAGS		:=	-g $(ARCH) $(RPXSPECS) $(CFLAGS) -Wl,-Map,$(notdir $*.map)

LIBS		:=	-lcurl -lnghttp2 -lbrotlicommon -lbrotlidec -lmbedtls -lmbedx509 -lmbedcrypto `$(PREFIX)pkg-config --libs SDL2_mixer SDL2_ttf SDL2_image harfbuzz jansson` -lwut -lmocha -lrpxloader

#-------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level
# containing include and lib
#-------------------------------------------------------------------------------
LIBDIRS	:= $(WUMS_ROOT) $(PORTLIBS) $(WUT_ROOT) $(WUT_ROOT)/usr


#-------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#-------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#-------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

.PHONY: debug release real clean all

#-------------------------------------------------------------------------------
all: debug

real:
	@git submodule update --init --recursive
	@rm -f zlib/contrib/minizip/iowin* zlib/contrib/minizip/mini* zlib/contrib/minizip/zip.? zlib/contrib/minizip/mztools.? zlib/contrib/minizip/configure.ac zlib/contrib/minizip/Makefile zlib/contrib/minizip/Makefile.am zlib/contrib/minizip/*.com zlib/contrib/minizip/*.txt
	@cd SDL_FontCache && for patch in $(TOPDIR)/SDL_FontCache-patches/*.patch; do echo Applying $$patch && git apply $$patch; done || true
	@[ -d $(BUILD) ] || mkdir -p $(BUILD)
	@$(MAKE) -C $(BUILD) -f $(CURDIR)/Makefile BUILD=$(BUILD) $(MAKE_CMD)

#-------------------------------------------------------------------------------
clean:
	@git submodule deinit --force --all
	@rm -fr debug release $(TARGET).rpx $(TARGET).elf

#-------------------------------------------------------------------------------
debug:		MAKE_CMD	:=	debug
debug:		export V	:=	1
debug:		real

#-------------------------------------------------------------------------------
release:	MAKE_CMD	:=	all
release:	BUILD		:=	release
release:	real

#-------------------------------------------------------------------------------
else
.PHONY:	debug all


CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(TOPDIR)/$(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(TOPDIR)/$(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(TOPDIR)/$(dir)/*.s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(TOPDIR)/$(dir)/*.*)))

#-------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#-------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
#-------------------------------------------------------------------------------
	export LD	:=	$(CC)
#-------------------------------------------------------------------------------
else
#-------------------------------------------------------------------------------
	export LD	:=	$(CXX)
#-------------------------------------------------------------------------------
endif
#-------------------------------------------------------------------------------

OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
OFILES_SRC	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
OFILES 		:=	$(OFILES_BIN) $(OFILES_SRC)
HFILES_BIN	:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(TOPDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(TOPDIR)/$(BUILD) -I$(PORTLIBS_PATH)/ppc/include/freetype2

LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)
VPATH	:=	$(foreach dir,$(SOURCES),$(TOPDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(TOPDIR)/$(dir))

DEPSDIR	:=	$(TOPDIR)/$(BUILD)
DEPENDS	:=	$(OFILES:.o=.d)

CFLAGS += $(INCLUDE)
CXXFLAGS += $(INCLUDE)
LDFLAGS += $(INCLUDE)

#-------------------------------------------------------------------------------
# main targets
#-------------------------------------------------------------------------------
all	:	$(OUTPUT).rpx

$(OUTPUT).rpx	:	$(OUTPUT).elf
$(OUTPUT).elf	:	$(OFILES)

$(OFILES_SRC)	: $(HFILES_BIN)

#-------------------------------------------------------------------------------
debug: CFLAGS	+=	-Wall -Wno-trigraphs -DNUSSPLI_DEBUG
debug: CXXFLAGS	+=	-Wall -Wno-trigraphs -DNUSSPLI_DEBUG
debug: LDFLAGS	+=	-Wall -Wno-trigraphs -DNUSSPLI_DEBUG
debug: all

#-------------------------------------------------------------------------------
# you need a rule like this for each extension you use as binary data
#-------------------------------------------------------------------------------
%.bin.o	%_bin.h :	%.bin
#-------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

#-------------------------------------------------------------------------------
endif
#-------------------------------------------------------------------------------
