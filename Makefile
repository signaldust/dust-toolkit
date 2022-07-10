
# No default rules
.SUFFIXES:

# We assume clang on all platforms
CC := clang

# Generic compilation flags, both C and C++
CFLAGS := -I. -fvisibility=hidden
CFLAGS += -Ofast -fomit-frame-pointer
CFLAGS += -Wall -Werror -Wfloat-conversion -ferror-limit=5
CFLAGS += -Wno-unused -Wno-unused-function

# C++ specific flags
CXXFLAGS := -std=c++11 -fno-exceptions

LINKFLAGS :=

-include local.make

DUST_LIB ?= dust

DUST_BINDIR ?= bin
DUST_BUILDDIR ?= build

# Windows specific
ifeq ($(OS),Windows_NT)
    PLATFORM := Windows
    LIBRARY := $(DUST_BUILDDIR)/$(DUST_LIB).lib

    LINKDEP := $(LIBRARY)

    MAKEDIR := tools\win\mkdir-p.bat
    DUST_LINKLIB ?= llvm-lib /out:$(LIBRARY)
    CLEANALL := tools\win\rm-rf.bat $(DUST_BUILDDIR)
    
    LINKFLAGS += --rtlib=compiler-rt $(LIBRARY) -luser32 -lgdi32

    CFLAGS += -D_CRT_SECURE_NO_WARNINGS

    #CFLAGS += -DDUST_USE_OPENGL=0

    CFLAGS += -DDUST_USE_OPENGL=1 -Idust/libs/gl3w
    LINKFLAGS += -lopengl32
    PLATFORM_SOURCES := dust/libs/gl3w/gl3w.c
    
    BINEXT := .exe
    LIBEXT := .dll

else
    
    # Anything else is .a
    LIBRARY ?= $(DUST_BUILDDIR)/$(DUST_LIB).a
    MAKEDIR := mkdir -p
    CLEANALL := rm -rf $(DUST_BUILDDIR) && mkdir $(DUST_BUILDDIR)
    
    LINKDEP := $(LIBRARY)
    
    # MacOSX specific
    ifeq ($(shell uname),Darwin)
        PLATFORM := MacOSX
        
        DUST_LINKLIB := libtool -static -o $(LIBRARY)
        
        CFLAGS += -mmacosx-version-min=10.9 -DDUST_USE_OPENGL=1
        
        LIBS := -lc++
        LIBS += -framework AudioUnit -framework Carbon -framework AppKit
        LIBS += -framework OpenGL -framework QuartzCore -framework Security
        
        # We generate new UUID prefix every time we link something
        # to hopefully avoid ever running into Cocoa namespace collisions.
        #
        # This doesn't matter for standalone applications, but it matters
        # for AudioUnits that might have different version of the library.
        LINKFLAGS += -DDUST_COCOA_PREFIX=`uuidgen|sed -e 's/-/_/g'`
        LINKFLAGS += $(CFLAGS) $(CXXFLAGS)
        LINKFLAGS += dust/gui/sys_osx.mm $(LIBRARY) $(LIBS)

        LINKDEP += dust/gui/sys_osx.mm
        
        BINEXT :=
        LIBEXT := .dylib
        
    endif

endif

PLATFORM ?= Unknown
ifeq ($(PLATFORM),Unknown)
   $(error Unknown platform)
endif

# Automatically figure out source files
LIB_SOURCES := $(wildcard dust/*/*.c)
LIB_SOURCES += $(wildcard dust/*/*.cpp)
LIB_SOURCES += $(PLATFORM_SOURCES)

LIB_OBJECTS := $(patsubst %,$(DUST_BUILDDIR)/%.o,$(LIB_SOURCES))
LIB_DEPENDS := $(LIB_OBJECTS:.o=.d)

SRC_DEPENDS :=

# automatic target generation for any subdirectory of programs/
define ProjectTarget
 SRC_DEPENDS += $(patsubst %,$(DUST_BUILDDIR)/%.d,$(wildcard $1*.cpp))
 $(DUST_BINDIR)/$(patsubst programs/%/,%,$1)$(BINEXT): $(LINKDEP) \
    $(patsubst %,$(DUST_BUILDDIR)/%.o,$(wildcard $1*.cpp))
	@echo LINK $$@
	@$(MAKEDIR) $(DUST_BINDIR)
	@$(CC) -o $$@ \
        $(patsubst %,$(DUST_BUILDDIR)/%.o,$(wildcard $1*.cpp)) $(LINKFLAGS)
endef

# automatic target generation for any subdirectory of plugins/
define PluginTarget
 SRC_DEPENDS += $(patsubst %,$(DUST_BUILDDIR)/%.d,$(wildcard $1*.cpp))
 $(DUST_BUILDDIR)/$(patsubst plugins/%/,%,$1)$(LIBEXT): $(LINKDEP) \
    $(patsubst %,$(DUST_BUILDDIR)/%.o,$(wildcard $1*.cpp))
	@echo LINKLIB $$@
	@$(MAKEDIR) $(DUST_BUILDDIR)
	@$(CC) -shared -o $$@ \
        $(patsubst %,$(DUST_BUILDDIR)/%.o,$(wildcard $1*.cpp)) $(LINKFLAGS)
endef

PROJDIRS := $(wildcard programs/*/)
PROJECTS := $(patsubst programs/%/,$(DUST_BINDIR)/%$(BINEXT),$(PROJDIRS))

PLUGDIRS := $(wildcard plugins/*/)
PROJECTS += $(patsubst plugins/%/,$(DUST_BUILDDIR)/%$(LIBEXT),$(PLUGDIRS))

.PHONY: all clean

all: $(LIBRARY) $(PROJECTS)
	@echo DONE

clean:
	@echo CLEAN $(DUST_BUILDDIR)
	@$(CLEANALL)

$(foreach i,$(PROJDIRS),$(eval $(call ProjectTarget,$(i))))
$(foreach i,$(PLUGDIRS),$(eval $(call PluginTarget,$(i))))

$(LIBRARY): $(LIB_OBJECTS)
	@echo LIB $@
	@$(MAKEDIR) "$(dir $@)"
	@$(DUST_LINKLIB) $(LIB_OBJECTS)

$(DUST_BUILDDIR)/%.c.o: %.c
	@echo CC $<
	@$(MAKEDIR) "$(dir $@)"
	@$(CC) -MMD -MP $(CFLAGS) -c $< -o $@

$(DUST_BUILDDIR)/%.cpp.o: %.cpp
	@echo CC $<
	@$(MAKEDIR) "$(dir $@)"
	@$(CC) -MMD -MP $(CFLAGS) $(CXXFLAGS) -c $< -o $@

-include $(LIB_DEPENDS)
-include $(SRC_DEPENDS)
