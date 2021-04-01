\
# No default rules
.SUFFIXES:

# We assume clang on all platforms
CC := clang

# Generic compilation flags, both C and C++
CFLAGS := -I.
CFLAGS += -Ofast -fomit-frame-pointer
CFLAGS += -Wall -Werror -Wfloat-conversion -ferror-limit=5
CFLAGS += -Wno-unused -Wno-unused-function

# C++ specific flags
CXXFLAGS := -std=c++11 -fno-exceptions

-include local.make

DUST_LIB ?= dust
DUST_BIN ?= bin
DUST_BUILD ?= build

# Windows specific
ifeq ($(OS),Windows_NT)
    PLATFORM := Windows
    LIBRARY := $(DUST_BUILD)/$(DUST_LIB).lib

    MAKEDIR := tools\win\mkdir-p.bat
    DUST_LINKLIB ?= llvm-lib /out:$(LIBRARY)
    CLEANALL := tools\win\rm-rf.bat $(DUST_BUILD)
    
    LINKFLAGS := --rtlib=compiler-rt $(LIBRARY) -luser32 -lgdi32

    CFLAGS += -D_CRT_SECURE_NO_WARNINGS -DDUST_USE_OPENGL=0
    
    BINEXT := .exe

else
    
    # Anything else is .a
    LIBRARY ?= $(DUST_BUILD)/$(DUST_LIB).a
    MAKEDIR := mkdir -p
    CLEANALL := rm -rf $(DUST_BUILD) && mkdir $(DUST_BUILD)
    
    # MacOSX specific
    ifeq ($(shell uname),Darwin)
        PLATFORM := MacOSX
        
        DUST_LINKLIB := libtool -static -o $(LIBRARY)
        
        CFLAGS += -arch x86_64 -mmacosx-version-min=10.9 -DDUST_USE_OPENGL=1
        
        LIBS := -lc++
        LIBS += -framework AudioUnit -framework Carbon -framework AppKit
        LIBS += -framework OpenGL -framework QuartzCore -framework Security
        
        # We generate new UUID prefix every time we link something
        # to hopefully avoid ever running into Cocoa namespace collisions.
        #
        # This doesn't matter for standalone applications, but it matters
        # for AudioUnits that might have different version of the library.
        LINKFLAGS := -DDUST_COCOA_PREFIX=`uuidgen|sed -e 's/-/_/g'|sed -e 's/.*/_\0_/'`
        LINKFLAGS += $(CFLAGS) $(CXXFLAGS)
        LINKFLAGS += dust/gui/sys_osx.mm $(LIBRARY) $(LIBS)
        
        BINEXT := 
    endif

endif

PLATFORM ?= Unknown
ifeq ($(PLATFORM),Unknown)
   $(error Unknown platform)
endif

# Automatically figure out source files
LIB_SOURCES := $(wildcard dust/*/*.c)
LIB_SOURCES += $(wildcard dust/*/*.cpp)

LIB_OBJECTS := $(patsubst %,$(DUST_BUILD)/%.o,$(LIB_SOURCES))
LIB_DEPENDS := $(LIB_OBJECTS:.o=.d)

SRC_DEPENDS :=

# automatic target generation for any subdirectory of programs/
define ProjectTarget
 SRC_DEPENDS += $(patsubst %,$(DUST_BUILD)/%.d,$(wildcard $1*.cpp))
 $(DUST_BIN)/$(patsubst programs/%/,%,$1)$(BINEXT): $(LIBRARY) \
    $(patsubst %,$(DUST_BUILD)/%.o,$(wildcard $1*.cpp))
	@echo LINK $$@
	@$(MAKEDIR) "bin"
	@$(CC) -o $$@ $(patsubst %,$(DUST_BUILD)/%.o,$(wildcard $1*.cpp)) $(LINKFLAGS)
endef

PROJDIRS := $(wildcard programs/*/)
PROJECTS := $(patsubst programs/%/,bin/%$(BINEXT),$(PROJDIRS))

.PHONY: all clean

all: $(LIBRARY) $(PROJECTS)
	@echo DONE

clean:
	@echo CLEAN $(DUST_BUILD)
	@$(CLEANALL)

$(foreach i,$(PROJDIRS),$(eval $(call ProjectTarget,$(i))))

$(LIBRARY): $(LIB_OBJECTS)
	@echo LIB $@
	@$(MAKEDIR) "$(dir $@)"
	@$(DUST_LINKLIB) $(LIB_OBJECTS)

$(DUST_BUILD)/%.c.o: %.c
	@echo CC $<
	@$(MAKEDIR) "$(dir $@)"
	@$(CC) -MMD -MP $(CFLAGS) -c $< -o $@

$(DUST_BUILD)/%.cpp.o: %.cpp
	@echo CC $<
	@$(MAKEDIR) "$(dir $@)"
	@$(CC) -MMD -MP $(CFLAGS) $(CXXFLAGS) -c $< -o $@

-include $(LIB_DEPENDS)
-include $(SRC_DEPENDS)
