CMAKE_MINIMUM_REQUIRED(VERSION 3.12)

PROJECT(mint-arena)

#
# Spearmint Makefile
#
# # GNU Make required
# #
#COMPILE_PLATFORM=$(shell uname | sed -e 's/_.*//' | tr '[:upper:]' '[:lower:]' | sed -e 's/\//_/g')
set(COMPILE_PLATFORM "${CMAKE_HOST_SYSTEM_NAME}")
#COMPILE_ARCH=$(shell uname -m | sed -e 's/i.86/x86/' | sed -e 's/^arm.*/arm/')
set(COMPILE_ARCH "${CMAKE_HOST_SYSTEM_PROCESSOR}")


# ifeq ($(COMPILE_PLATFORM),sunos)
#   # Solaris uname and GNU uname differ
#   COMPILE_ARCH=$(shell uname -p | sed -e 's/i.86/x86/')
# endif

if(NOT BUILD_GAME_SO)
   set(BUILD_GAME_SO ON)
endif()
if(NOT BUILD_GAME_QVM)
   set(BUILD_GAME_QVM OFF)
endif()
if(NOT BUILD_BASEGAME)
   set(BUILD_BASEGAME ON)
endif()
if(NOT USE_BASEGAME_MP_HUD)
   set(USE_BASEGAME_MP_HUD OFF)
endif()
if(NOT BUILD_MISSIONPACK)
   set(BUILD_MISSIONPACK OFF)
endif()
if(NOT USE_MISSIONPACK_Q3_UI)
   set(USE_MISSIONPACK_Q3_UI OFF)
endif()
if(NOT USE_MISSIONPACK_MP_HUD)
   set(USE_MISSIONPACK_MP_HUD ON)
endif()
if(NOT BUILD_FINAL)
   set(BUILD_FINAL OFF)
endif()
if(NOT USE_YACC)
   set(USE_YACC OFF)
endif()

#############################################################################
#
# If you require a different configuration from the defaults below, create a
# new file named "Makefile.local" in the same directory as this file and define
# your parameters there. This allows you to change configuration without
# causing problems with keeping up to date with the repository.
#
#############################################################################
# -include Makefile.local
include(local.cmake OPTIONAL)

if(${COMPILE_PLATFORM} MATCHES cygwin)
   set(PLATFORM mingw32)
endif()

if(NOT PLATFORM)
  set(PLATFORM ${COMPILE_PLATFORM})
endif()
set($ENV{PLATFORM} ${PLATFORM})

if(${PLATFORM} MATCHES mingw32)
  set(MINGW ON)
endif()
if(${PLATFORM} MATCHES mingw64)
  set(MINGW ON)
endif()

if(${COMPILE_ARCH} MATCHES i86pc)
   set(COMPILE_ARCH x86)
endif()

if(${COMPILE_ARCH} MATCHES amd64)
   set(COMPILE_ARCH x86_64)
endif()
if(${COMPILE_ARCH} MATCHES x64)
   set(COMPILE_ARCH x86_64)
endif()

if(${COMPILE_ARCH} MATCHES powerpc)
   set(COMPILE_ARCH ppc)
endif()
if(${COMPILE_ARCH} MATCHES powerpc64)
   set(COMPILE_ARCH ppc64)
endif()

if(${COMPILE_ARCH} MATCHES axp)
   set(COMPILE_ARCH alpha)
endif()

if(NOT ARCH)
   set(ARCH ${COMPILE_ARCH})
endif()
set($ENV{ARCH} ${ARCH})

if(NOT ${PLATFORM} MATCHES ${COMPILE_PLATFORM})
   set(CROSS_COMPILING ON)
else()
   set(CROSS_COMPILING OFF)

   if(NOT ${ARCH} MATCHES ${COMPILE_ARCH})
     set(CROSS_COMPILING ON)
   endif()
endif()
set($ENV{CROSS_COMPILING} ${CROSS_COMPILING})

if(NOT VERSION)
  set(VERSION "1.0.3")
endif()

if(NOT VM_PREFIX)
  set(VM_PREFIX "mint-")
endif()

if(NOT SOURCE_ARCHIVE)
  set(SOURCE_ARCHIVE "mint-arena")
endif()

if(NOT BASEGAME)
  set(BASEGAME "baseq3")
endif()

if(NOT BASEGAME_CFLAGS)
  set(BASEGAME_CFLAGS "")

  if(USE_BASEGAME_MP_HUD)
    set(BASEGAME_CFLAGS "${BASEGAME_CFLAGS} -DMISSIONPACK_HUD")
  endif()
endif()

set(BASEGAME_CFLAGS ${BASEGAME_CFLAGS} -DMODDIR=\"${BASEGAME}\" -DBASETA=\"${MISSIONPACK}\")

if(NOT MISSIONPACK)
 set(MISSIONPACK missionpack)
endif()

if(NOT MISSIONPACK_CFLAGS)
  set(MISSIONPACK_CFLAGS -DMISSIONPACK)

  if (USE_MISSIONPACK_MP_HUD)
    list(APPEND MISSIONPACK_CFLAGS -DMISSIONPACK_HUD)
  endif()
endif()

list(APPEND MISSIONPACK_CFLAGS -DMODDIR=\"${MISSIONPACK}\" -DBASEQ3=\"${BASEGAME}\")

# Add "-DEXAMPLE" to define EXAMPLE in engine and game/cgame.
if(NOT BUILD_DEFINES)
 set(BUILD_DEFINES "")
endif()

if(NOT COPYDIR)
 set(COPYDIR "/usr/local/games/spearmint")
endif()

if(NOT COPYBINDIR)
 set(COPYBINDIR ${COPYDIR})
endif()

if(NOT MOUNT_DIR)
  set(MOUNT_DIR code)
endif()

if(NOT BUILD_DIR)
  set(BUILD_DIR build)
endif()

if(NOT TEMPDIR)
  set(TEMPDIR /tmp)
endif()

if(NOT GENERATE_DEPENDENCIES)
 set(GENERATE_DEPENDENCIES ON)
endif()

if(NOT DEBUG_CFLAGS)
 set(DEBUG_CFLAGS "-ggdb -O0")
endif()

#############################################################################

# BD=$(BUILD_DIR)/debug-$(PLATFORM)-$(ARCH)
# BR=$(BUILD_DIR)/release-$(PLATFORM)-$(ARCH)
# CMDIR=$(MOUNT_DIR)/qcommon
# BLIBDIR=$(MOUNT_DIR)/botlib
# GDIR=$(MOUNT_DIR)/game
# CGDIR=$(MOUNT_DIR)/cgame
# UIDIR=$(MOUNT_DIR)/ui
# Q3UIDIR=$(MOUNT_DIR)/q3_ui
# Q3ASMDIR=$(MOUNT_DIR)/tools/asm
# LBURGDIR=$(MOUNT_DIR)/tools/lcc/lburg
# Q3CPPDIR=$(MOUNT_DIR)/tools/lcc/cpp
# Q3LCCETCDIR=$(MOUNT_DIR)/tools/lcc/etc
# Q3LCCSRCDIR=$(MOUNT_DIR)/tools/lcc/src

# bin_path=$(shell which $(1) 2> /dev/null)

# ifneq ($(BUILD_FINAL),1)

# # Add git version info
# USE_GIT=
# ifeq ($(wildcard .git),.git)
#   GIT_REV=$(shell git show -s --pretty=format:%ad+%h --date=short | tr -d '-')
#   ifneq ($(GIT_REV),)
#     VERSION:=$(VERSION)+$(GIT_REV)
#     USE_GIT=1
#   endif
# endif

# endif


# #############################################################################
# # SETUP AND BUILD -- LINUX
# #############################################################################

# INSTALL=install
# MKDIR=mkdir -p

# ifneq (,$(findstring "$(COMPILE_PLATFORM)", "linux" "gnu_kfreebsd" "kfreebsd-gnu" "gnu"))
#   TOOLS_CFLAGS += -DARCH_STRING=\"$(COMPILE_ARCH)\"
# endif

# ifneq (,$(findstring "$(PLATFORM)", "linux" "gnu_kfreebsd" "kfreebsd-gnu" "gnu"))
if(UNIX)
   set(BASE_CFLAGS -Wall -fno-strict-aliasing -Wimplicit -Wstrict-prototypes -pipe -DUSE_ICON -DARCH_STRING="${ARCH}")

   set(OPTIMIZEVM "-O3")
   set(OPTIMIZE "${OPTIMIZEVM} -ffast-math")

   if(${ARCH} MATCHES x86_64)
     set(OPTIMIZEVM -O3)
     set(OPTIMIZE "${OPTIMIZEVM} -ffast-math")
     set(HAVE_VM_COMPILED ON)
   elseif(${ARCH} MATCHES x86)
     set(OPTIMIZEVM "-O3 -march=i586")
     set(OPTIMIZE "${OPTIMIZEVM} -ffast-math")
     set(HAVE_VM_COMPILED ON)
   elseif(${ARCH} MATCHES ppc)
     set(ALTIVEC_CFLAGS -maltivec)
     set(HAVE_VM_COMPILED ON)
   elseif(${ARCH} MATCHES ppc64)
     set(ALTIVEC_CFLAGS -maltivec)
     set(HAVE_VM_COMPILED ON)
   endif()
#   ifeq ($(ARCH),sparc)
#     OPTIMIZE += -mtune=ultrasparc3 -mv8plus
#     OPTIMIZEVM += -mtune=ultrasparc3 -mv8plus
#     HAVE_VM_COMPILED=true
#   endif
#   ifeq ($(ARCH),alpha)
#     # According to http://bugs.debian.org/cgi-bin/bugreport.cgi?bug=410555
#     # -ffast-math will cause the client to die with SIGFPE on Alpha
#     OPTIMIZE = $(OPTIMIZEVM)
#   endif
#   endif
#   endif

   #set(SHLIBEXT so)
   set(SHLIBCFLAGS -fPIC -fvisibility=hidden)
   set(SHLIBLDFLAGS -shared)

   set(THREAD_LIBS -lpthread)
   set(LIBS -ldl -lm)

   #if(${ARCH} MATCHES x86)
     # linux32 make ...
     #list(APPEND BASE_CFLAGS -m32)
   #elseif(${ARCH} MATCHES ppc64)
     #list(APPEND BASE_CFLAGS -m64)
   #endif()
endif() # ifeq Linux

# #############################################################################
# # SETUP AND BUILD -- MAC OS X
# #############################################################################

# ifeq ($(PLATFORM),darwin)
#   HAVE_VM_COMPILED=true
#   LIBS = -framework Cocoa
#   OPTIMIZEVM = -O3

#   # Default minimum Mac OS X version
#   ifeq ($(MACOSX_VERSION_MIN),)
#     MACOSX_VERSION_MIN=10.7
#   endif

#   MACOSX_MAJOR=$(shell echo $(MACOSX_VERSION_MIN) | cut -d. -f1)
#   MACOSX_MINOR=$(shell echo $(MACOSX_VERSION_MIN) | cut -d. -f2)
#   ifeq ($(shell test $(MACOSX_MINOR) -gt 9; echo $$?),0)
#     # Multiply and then remove decimal. 10.10 -> 101000.0 -> 101000
#     MAC_OS_X_VERSION_MIN_REQUIRED=$(shell echo "$(MACOSX_MAJOR) * 10000 + $(MACOSX_MINOR) * 100" | bc | cut -d. -f1)
#   else
#     # Multiply by 100 and then remove decimal. 10.7 -> 1070.0 -> 1070
#     MAC_OS_X_VERSION_MIN_REQUIRED=$(shell echo "$(MACOSX_VERSION_MIN) * 100" | bc | cut -d. -f1)
#   endif

#   LDFLAGS += -mmacosx-version-min=$(MACOSX_VERSION_MIN)
#   BASE_CFLAGS += -mmacosx-version-min=$(MACOSX_VERSION_MIN) \
#                  -DMAC_OS_X_VERSION_MIN_REQUIRED=$(MAC_OS_X_VERSION_MIN_REQUIRED)

#   ifeq ($(ARCH),ppc)
#     BASE_CFLAGS += -arch ppc
#     ALTIVEC_CFLAGS = -faltivec
#   endif
#   ifeq ($(ARCH),ppc64)
#     BASE_CFLAGS += -arch ppc64
#     ALTIVEC_CFLAGS = -faltivec
#   endif
#   ifeq ($(ARCH),x86)
#     OPTIMIZEVM += -march=prescott -mfpmath=sse
#     # x86 vm will crash without -mstackrealign since MMX instructions will be
#     # used no matter what and they corrupt the frame pointer in VM calls
#     BASE_CFLAGS += -arch i386 -m32 -mstackrealign
#   endif
#   ifeq ($(ARCH),x86_64)
#     OPTIMIZEVM += -mfpmath=sse
#     BASE_CFLAGS += -arch x86_64
#   endif

#   # When compiling on OSX for OSX, we're not cross compiling as far as the
#   # Makefile is concerned, as target architecture is specified as a compiler
#   # argument
#   ifeq ($(COMPILE_PLATFORM),darwin)
#     CROSS_COMPILING=0
#   endif

#   ifeq ($(CROSS_COMPILING),1)
#     ifeq ($(ARCH),x86_64)
#       CC=x86_64-apple-darwin13-cc
#       RANLIB=x86_64-apple-darwin13-ranlib
#     else
#       ifeq ($(ARCH),x86)
#         CC=i386-apple-darwin13-cc
#         RANLIB=i386-apple-darwin13-ranlib
#       else
#         $(error Architecture $(ARCH) is not supported when cross compiling)
#       endif
#     endif
#   endif

#   BASE_CFLAGS += -fno-strict-aliasing -fno-common -pipe

#   BASE_CFLAGS += -D_THREAD_SAFE=1

#   OPTIMIZE = $(OPTIMIZEVM) -ffast-math

#   SHLIBEXT=dylib
#   SHLIBCFLAGS=-fPIC -fno-common
#   SHLIBLDFLAGS=-dynamiclib $(LDFLAGS) -Wl,-U,_com_altivec

#   NOTSHLIBCFLAGS=-mdynamic-no-pic

# else # ifeq darwin


# #############################################################################
# # SETUP AND BUILD -- MINGW32
# #############################################################################

# ifdef MINGW

#   ifeq ($(CROSS_COMPILING),1)
#     # If CC is already set to something generic, we probably want to use
#     # something more specific
#     ifneq ($(findstring $(strip $(CC)),cc gcc),)
#       CC=
#     endif

#     # We need to figure out the correct gcc and windres
#     ifeq ($(ARCH),x86_64)
#       MINGW_PREFIXES=x86_64-w64-mingw32 amd64-mingw32msvc
#     endif
#     ifeq ($(ARCH),x86)
#       MINGW_PREFIXES=i686-w64-mingw32 i586-mingw32msvc i686-pc-mingw32
#     endif

#     ifndef CC
#       CC=$(firstword $(strip $(foreach MINGW_PREFIX, $(MINGW_PREFIXES), \
#          $(call bin_path, $(MINGW_PREFIX)-gcc))))
#     endif

#     ifndef WINDRES
#       WINDRES=$(firstword $(strip $(foreach MINGW_PREFIX, $(MINGW_PREFIXES), \
#          $(call bin_path, $(MINGW_PREFIX)-windres))))
#     endif
#   else
#     # Some MinGW installations define CC to cc, but don't actually provide cc,
#     # so check that CC points to a real binary and use gcc if it doesn't
#     ifeq ($(call bin_path, $(CC)),)
#       CC=gcc
#     endif

#   endif

#   # using generic windres if specific one is not present
#   ifndef WINDRES
#     WINDRES=windres
#   endif

#   BASE_CFLAGS = -Wall -fno-strict-aliasing -Wimplicit -Wstrict-prototypes \
#     -DUSE_ICON

#   # In the absence of wspiapi.h, require Windows XP or later
#   ifeq ($(shell test -e $(CMDIR)/wspiapi.h; echo $$?),1)
#     BASE_CFLAGS += -DWINVER=0x501
#   endif

#   ifeq ($(ARCH),x86_64)
#     OPTIMIZEVM = -O3
#     OPTIMIZE = $(OPTIMIZEVM) -ffast-math
#     HAVE_VM_COMPILED = true
#   endif
#   ifeq ($(ARCH),x86)
#     OPTIMIZEVM = -O3 -march=i586
#     OPTIMIZE = $(OPTIMIZEVM) -ffast-math
#     HAVE_VM_COMPILED = true
#   endif

#   SHLIBEXT=dll
#   SHLIBCFLAGS=
#   SHLIBLDFLAGS=-shared $(LDFLAGS)

#   BINEXT=.exe

#   ifeq ($(CROSS_COMPILING),0)
#     TOOLS_BINEXT=.exe
#   endif

#   ifeq ($(COMPILE_PLATFORM),cygwin)
#     TOOLS_BINEXT=.exe

#     # Under cygwin the default of using gcc for TOOLS_CC won't work, so
#     # we need to figure out the appropriate compiler to use, based on the
#     # host architecture that we're running under (as tools run on the host)
#     ifeq ($(COMPILE_ARCH),x86_64)
#       TOOLS_MINGW_PREFIXES=x86_64-w64-mingw32 amd64-mingw32msvc
#     endif
#     ifeq ($(COMPILE_ARCH),x86)
#       TOOLS_MINGW_PREFIXES=i686-w64-mingw32 i586-mingw32msvc i686-pc-mingw32
#     endif

#     TOOLS_CC=$(firstword $(strip $(foreach TOOLS_MINGW_PREFIX, $(TOOLS_MINGW_PREFIXES), \
#       $(call bin_path, $(TOOLS_MINGW_PREFIX)-gcc))))
#   endif

#   LIBS= -lws2_32 -lwinmm -lpsapi

#   ifeq ($(ARCH),x86)
#     # build 32bit
#     BASE_CFLAGS += -m32
#   else
#     BASE_CFLAGS += -m64
#   endif

# else # ifdef MINGW

# #############################################################################
# # SETUP AND BUILD -- FREEBSD
# #############################################################################

# ifeq ($(PLATFORM),freebsd)

#   # flags
#   BASE_CFLAGS = \
#     -Wall -fno-strict-aliasing -Wimplicit -Wstrict-prototypes \
#     -DUSE_ICON -DMAP_ANONYMOUS=MAP_ANON
#   HAVE_VM_COMPILED = true

#   OPTIMIZEVM =
#   OPTIMIZE = $(OPTIMIZEVM) -ffast-math

#   SHLIBEXT=so
#   SHLIBCFLAGS=-fPIC
#   SHLIBLDFLAGS=-shared $(LDFLAGS)

#   THREAD_LIBS=-lpthread
#   # don't need -ldl (FreeBSD)
#   LIBS=-lm

#   # cross-compiling tweaks
#   ifeq ($(ARCH),x86)
#     ifeq ($(CROSS_COMPILING),1)
#       BASE_CFLAGS += -m32
#     endif
#   endif
#   ifeq ($(ARCH),x86_64)
#     ifeq ($(CROSS_COMPILING),1)
#       BASE_CFLAGS += -m64
#     endif
#   endif
# else # ifeq freebsd

# #############################################################################
# # SETUP AND BUILD -- OPENBSD
# #############################################################################

# ifeq ($(PLATFORM),openbsd)

#   BASE_CFLAGS = -Wall -fno-strict-aliasing -Wimplicit -Wstrict-prototypes \
#     -pipe -DUSE_ICON -DMAP_ANONYMOUS=MAP_ANON
#   CLIENT_CFLAGS += $(SDL_CFLAGS)

#   OPTIMIZEVM = -O3
#   OPTIMIZE = $(OPTIMIZEVM) -ffast-math

#   ifeq ($(ARCH),x86_64)
#     OPTIMIZEVM = -O3
#     OPTIMIZE = $(OPTIMIZEVM) -ffast-math
#     HAVE_VM_COMPILED = true
#   else
#   ifeq ($(ARCH),x86)
#     OPTIMIZEVM = -O3 -march=i586
#     OPTIMIZE = $(OPTIMIZEVM) -ffast-math
#     HAVE_VM_COMPILED=true
#   else
#   ifeq ($(ARCH),ppc)
#     ALTIVEC_CFLAGS = -maltivec
#     HAVE_VM_COMPILED=true
#   endif
#   ifeq ($(ARCH),ppc64)
#     ALTIVEC_CFLAGS = -maltivec
#     HAVE_VM_COMPILED=true
#   endif
#   ifeq ($(ARCH),sparc64)
#     OPTIMIZE += -mtune=ultrasparc3 -mv8plus
#     OPTIMIZEVM += -mtune=ultrasparc3 -mv8plus
#     HAVE_VM_COMPILED=true
#   endif
#   ifeq ($(ARCH),alpha)
#     # According to http://bugs.debian.org/cgi-bin/bugreport.cgi?bug=410555
#     # -ffast-math will cause the client to die with SIGFPE on Alpha
#     OPTIMIZE = $(OPTIMIZEVM)
#   endif
#   endif
#   endif

#   SHLIBEXT=so
#   SHLIBCFLAGS=-fPIC
#   SHLIBLDFLAGS=-shared $(LDFLAGS)

#   THREAD_LIBS=-lpthread
#   LIBS=-lm
# else # ifeq openbsd

# #############################################################################
# # SETUP AND BUILD -- NETBSD
# #############################################################################

# ifeq ($(PLATFORM),netbsd)

#   LIBS=-lm
#   SHLIBEXT=so
#   SHLIBCFLAGS=-fPIC
#   SHLIBLDFLAGS=-shared $(LDFLAGS)
#   THREAD_LIBS=-lpthread

#   BASE_CFLAGS = -Wall -fno-strict-aliasing -Wimplicit -Wstrict-prototypes

#   ifeq ($(ARCH),x86)
#     HAVE_VM_COMPILED=true
#   endif
# else # ifeq netbsd

# #############################################################################
# # SETUP AND BUILD -- IRIX
# #############################################################################

# ifeq ($(PLATFORM),irix64)
#   LIB=lib

#   ARCH=mips

#   CC = c99

#   BASE_CFLAGS=-Dstricmp=strcasecmp -Xcpluscomm -woff 1185 \
#     -I. -I$(ROOT)/usr/include
#   OPTIMIZE = -O3

#   SHLIBEXT=so
#   SHLIBCFLAGS=
#   SHLIBLDFLAGS=-shared

#   LIBS=-ldl -lm -lgen

# else # ifeq IRIX

# #############################################################################
# # SETUP AND BUILD -- SunOS
# #############################################################################

# ifeq ($(PLATFORM),sunos)

#   CC=gcc
#   INSTALL=ginstall
#   MKDIR=gmkdir -p
#   COPYDIR="/usr/local/share/games/spearmint"

#   ifneq ($(ARCH),x86)
#     ifneq ($(ARCH),sparc)
#       $(error arch $(ARCH) is currently not supported)
#     endif
#   endif

#   BASE_CFLAGS = -Wall -fno-strict-aliasing -Wimplicit -Wstrict-prototypes \
#     -pipe -DUSE_ICON
#   CLIENT_CFLAGS += $(SDL_CFLAGS)

#   OPTIMIZEVM = -O3 -funroll-loops

#   ifeq ($(ARCH),sparc)
#     OPTIMIZEVM += -O3 \
#       -fstrength-reduce -falign-functions=2 \
#       -mtune=ultrasparc3 -mv8plus -mno-faster-structs
#     HAVE_VM_COMPILED=true
#   else
#   ifeq ($(ARCH),x86)
#     OPTIMIZEVM += -march=i586 -fomit-frame-pointer \
#       -falign-functions=2 -fstrength-reduce
#     HAVE_VM_COMPILED=true
#     BASE_CFLAGS += -m32
#     CLIENT_CFLAGS += -I/usr/X11/include/NVIDIA
#     CLIENT_LDFLAGS += -L/usr/X11/lib/NVIDIA -R/usr/X11/lib/NVIDIA
#   endif
#   endif

#   OPTIMIZE = $(OPTIMIZEVM) -ffast-math

#   SHLIBEXT=so
#   SHLIBCFLAGS=-fPIC
#   SHLIBLDFLAGS=-shared $(LDFLAGS)

#   THREAD_LIBS=-lpthread
#   LIBS=-lsocket -lnsl -ldl -lm

#   BOTCFLAGS=-O0

# else # ifeq sunos

# #############################################################################
# # SETUP AND BUILD -- GENERIC
# #############################################################################
#   BASE_CFLAGS=
#   OPTIMIZE = -O3

#   SHLIBEXT=so
#   SHLIBCFLAGS=-fPIC
#   SHLIBLDFLAGS=-shared

# endif #Linux
# endif #darwin
# endif #MINGW
# endif #FreeBSD
# endif #OpenBSD
# endif #NetBSD
# endif #IRIX
# endif #SunOS

# ifndef CC
#   CC=gcc
# endif

# ifndef RANLIB
#   RANLIB=ranlib
# endif

# ifneq ($(HAVE_VM_COMPILED),true)
#   BASE_CFLAGS += -DNO_VM_COMPILED
# endif

# TARGETS =

if(NOT FULLBINEXT)
  set(FULLBINEXT "_${ARCH}${BINEXT}")
endif()

if(NOT SHLIBNAME)
  set(SHLIBNAME "${ARCH}")
endif()

if(${BUILD_GAME_SO})
   if(${BUILD_BASEGAME})
#     TARGETS += \
#       $(B)/$(BASEGAME)/$(VM_PREFIX)cgame_$(SHLIBNAME) \
#       $(B)/$(BASEGAME)/$(VM_PREFIX)game_$(SHLIBNAME)
   endif()
   if(${BUILD_MISSIONPACK})
#     TARGETS += \
#       $(B)/$(MISSIONPACK)/$(VM_PREFIX)cgame_$(SHLIBNAME) \
#       $(B)/$(MISSIONPACK)/$(VM_PREFIX)game_$(SHLIBNAME)
   endif()
endif()

if(${BUILD_GAME_QVM})
   if(${BUILD_BASEGAME})
#     TARGETS += \
#       $(B)/$(BASEGAME)/vm/$(VM_PREFIX)cgame.qvm \
#       $(B)/$(BASEGAME)/vm/$(VM_PREFIX)game.qvm
   endif()
   if(${BUILD_MISSIONPACK})
#     TARGETS += \
#       $(B)/$(MISSIONPACK)/vm/$(VM_PREFIX)cgame.qvm \
#       $(B)/$(MISSIONPACK)/vm/$(VM_PREFIX)game.qvm
   endif()
endif()

#if("${CC}", $(findstring "$(CC)", "clang" "clang++"))
#   BASE_CFLAGS += -Qunused-arguments
#endif

if(${GENERATE_DEPENDENCIES})
   set(DEPEND_CFLAGS "-MMD")
else()
   set(DEPEND_CFLAGS "")
endif()

# ifeq ($(NO_STRIP),1)
#   STRIP_FLAG =
# else
#   STRIP_FLAG = -s
# endif

# # https://reproducible-builds.org/specs/source-date-epoch/
#if(SOURCE_DATE_EPOCH)
#   list(APPEND BASE_BUILD_DEFINES -DPRODUCT_DATE=\\\"$(shell date --date="@$$SOURCE_DATE_EPOCH" "+%b %_d %Y" | sed -e 's/ /\\\ /g')\\\")
#endif()

list(APPEND BASE_BUILD_DEFINES -DPRODUCT_VERSION=\"${VERSION}\")
if(${USE_GIT})
   list(APPEND BASE_BUILD_DEFINES -DPRODUCT_VERSION_HAS_DATE)
endif()

list(APPEND BASE_CFLAGS -Wformat=2 -Wno-format-zero-length -Wformat-security -Wno-format-nonliteral)
list(APPEND BASE_CFLAGS -Wstrict-aliasing=2 -Wmissing-format-attribute)
list(APPEND BASE_CFLAGS -Wdisabled-optimization)
list(APPEND BASE_CFLAGS -Werror-implicit-function-declaration)

# ifeq ($(V),1)
# echo_cmd=@:
# Q=
# else
# echo_cmd=@echo
# Q=@
# endif

# ifeq ($(GENERATE_DEPENDENCIES),1)
#   DO_QVM_DEP=cat $(@:%.o=%.d) | sed -e 's/\.o/\.asm/g' >> $(@:%.o=%.d)
# endif

# define DO_SHLIB_CC
# $(echo_cmd) "SHLIB_CC $<"
# $(Q)$(CC) $(BASEGAME_CFLAGS) $(SHLIBCFLAGS) $(CFLAGS) $(OPTIMIZEVM) -o $@ -c $<
# $(Q)$(DO_QVM_DEP)
# endef

# define DO_GAME_CC
# $(echo_cmd) "GAME_CC $<"
# $(Q)$(CC) $(BASEGAME_CFLAGS) -DGAME $(SHLIBCFLAGS) $(CFLAGS) $(OPTIMIZEVM) -o $@ -c $<
# $(Q)$(DO_QVM_DEP)
# endef

# define DO_CGAME_CC
# $(echo_cmd) "CGAME_CC $<"
# $(Q)$(CC) $(BASEGAME_CFLAGS) -DCGAME $(SHLIBCFLAGS) $(CFLAGS) $(OPTIMIZEVM) -o $@ -c $<
# $(Q)$(DO_QVM_DEP)
# endef

# define DO_SHLIB_CC_MISSIONPACK
# $(echo_cmd) "SHLIB_CC_MISSIONPACK $<"
# $(Q)$(CC) $(MISSIONPACK_CFLAGS) $(SHLIBCFLAGS) $(CFLAGS) $(OPTIMIZEVM) -o $@ -c $<
# $(Q)$(DO_QVM_DEP)
# endef

# define DO_GAME_CC_MISSIONPACK
# $(echo_cmd) "GAME_CC_MISSIONPACK $<"
# $(Q)$(CC) $(MISSIONPACK_CFLAGS) -DGAME $(SHLIBCFLAGS) $(CFLAGS) $(OPTIMIZEVM) -o $@ -c $<
# $(Q)$(DO_QVM_DEP)
# endef

# define DO_CGAME_CC_MISSIONPACK
# $(echo_cmd) "CGAME_CC_MISSIONPACK $<"
# $(Q)$(CC) $(MISSIONPACK_CFLAGS) -DCGAME $(SHLIBCFLAGS) $(CFLAGS) $(OPTIMIZEVM) -o $@ -c $<
# $(Q)$(DO_QVM_DEP)
# endef


# #############################################################################
# # MAIN TARGETS
# #############################################################################

# default: release
# all: debug release

# debug:
# 	@$(MAKE) targets B=$(BD) CFLAGS="$(CFLAGS) $(BASE_BUILD_DEFINES) $(BASE_CFLAGS) $(BUILD_DEFINES) $(DEPEND_CFLAGS)" \
# 	  OPTIMIZE="$(DEBUG_CFLAGS)" OPTIMIZEVM="$(DEBUG_CFLAGS)" \
# 	  CLIENT_CFLAGS="$(CLIENT_CFLAGS)" SERVER_CFLAGS="$(SERVER_CFLAGS)" V=$(V) \
# 	  QVM_CFLAGS="$(BASE_BUILD_DEFINES) $(BUILD_DEFINES)"

# release:
# 	@$(MAKE) targets B=$(BR) CFLAGS="$(CFLAGS) $(BASE_BUILD_DEFINES) $(BASE_CFLAGS) $(BUILD_DEFINES) $(DEPEND_CFLAGS)" \
# 	  OPTIMIZE="-DNDEBUG $(OPTIMIZE)" OPTIMIZEVM="-DNDEBUG $(OPTIMIZEVM)" \
# 	  CLIENT_CFLAGS="$(CLIENT_CFLAGS)" SERVER_CFLAGS="$(SERVER_CFLAGS)" V=$(V) \
# 	  QVM_CFLAGS="-DNDEBUG $(BASE_BUILD_DEFINES) $(BUILD_DEFINES)"

# ifneq ($(call bin_path, tput),)
#   TERM_COLUMNS=$(shell if c=`tput cols`; then echo $$(($$c-4)); else echo 76; fi)
# else
#   TERM_COLUMNS=76
# endif

# NAKED_TARGETS=$(shell echo $(TARGETS) | sed -e "s!$(B)/!!g")

# print_list=-@for i in $(1); \
#      do \
#              echo "    $$i"; \
#      done

# ifneq ($(call bin_path, fmt),)
#   print_wrapped=@echo $(1) | fmt -w $(TERM_COLUMNS) | sed -e "s/^\(.*\)$$/    \1/"
# else
#   print_wrapped=$(print_list)
# endif

# # Create the build directories, check libraries and print out
# # an informational message, then start building
# targets: makedirs
# 	@echo ""
# 	@echo "Building in $(B):"
# 	@echo "  PLATFORM: $(PLATFORM)"
# 	@echo "  ARCH: $(ARCH)"
# 	@echo "  VERSION: $(VERSION)"
# 	@echo "  COMPILE_PLATFORM: $(COMPILE_PLATFORM)"
# 	@echo "  COMPILE_ARCH: $(COMPILE_ARCH)"
# 	@echo "  CC: $(CC)"
# ifeq ($(PLATFORM),mingw32)
# 	@echo "  WINDRES: $(WINDRES)"
# endif
# 	@echo ""
# 	@echo "  CFLAGS:"
# 	$(call print_wrapped, $(CFLAGS) $(OPTIMIZE))
# 	@echo ""
# 	@echo "  TOOLS_CFLAGS:"
# 	$(call print_wrapped, $(TOOLS_CFLAGS))
# 	@echo ""
# 	@echo "  LDFLAGS:"
# 	$(call print_wrapped, $(LDFLAGS))
# 	@echo ""
# 	@echo "  LIBS:"
# 	$(call print_wrapped, $(LIBS))
# 	@echo ""
# 	@echo "  Output:"
# 	$(call print_list, $(NAKED_TARGETS))
# 	@echo ""
# ifneq ($(TARGETS),)
#   ifndef DEBUG_MAKEFILE
# 	@$(MAKE) $(TARGETS) $(B).zip V=$(V)
#   endif
# endif

# $(B).zip: $(TARGETS)
# ifdef ARCHIVE
# 	@rm -f $@
# 	@(cd $(B) && zip -r9 ../../$@ $(NAKED_TARGETS))
# endif

# makedirs:
# 	@$(MKDIR) $(B)/$(BASEGAME)/cgame
# 	@$(MKDIR) $(B)/$(BASEGAME)/botlib
# 	@$(MKDIR) $(B)/$(BASEGAME)/game
# 	@$(MKDIR) $(B)/$(BASEGAME)/ui
# 	@$(MKDIR) $(B)/$(BASEGAME)/mpui
# 	@$(MKDIR) $(B)/$(BASEGAME)/qcommon
# 	@$(MKDIR) $(B)/$(BASEGAME)/vm
# 	@$(MKDIR) $(B)/$(MISSIONPACK)/cgame
# 	@$(MKDIR) $(B)/$(MISSIONPACK)/botlib
# 	@$(MKDIR) $(B)/$(MISSIONPACK)/game
# 	@$(MKDIR) $(B)/$(MISSIONPACK)/ui
# 	@$(MKDIR) $(B)/$(MISSIONPACK)/qcommon
# 	@$(MKDIR) $(B)/$(MISSIONPACK)/q3ui
# 	@$(MKDIR) $(B)/$(MISSIONPACK)/vm
# 	@$(MKDIR) $(B)/tools/asm
# 	@$(MKDIR) $(B)/tools/etc
# 	@$(MKDIR) $(B)/tools/rcc
# 	@$(MKDIR) $(B)/tools/cpp
# 	@$(MKDIR) $(B)/tools/lburg

# #############################################################################
# # QVM BUILD TOOLS
# #############################################################################

# ifndef TOOLS_CC
#   # A compiler which probably produces native binaries
#   TOOLS_CC = gcc
# endif

# ifndef YACC
#   YACC = yacc
# endif

# TOOLS_OPTIMIZE = -g -Wall -fno-strict-aliasing
# TOOLS_CFLAGS += $(TOOLS_OPTIMIZE) \
#                 -DTEMPDIR=\"$(TEMPDIR)\" -DSYSTEM=\"\" \
#                 -I$(Q3LCCSRCDIR) \
#                 -I$(LBURGDIR)
# TOOLS_LIBS =
# TOOLS_LDFLAGS =

# ifeq ($(GENERATE_DEPENDENCIES),1)
#   TOOLS_CFLAGS += -MMD
# endif

# define DO_YACC
# $(echo_cmd) "YACC $<"
# $(Q)$(YACC) $<
# $(Q)mv -f y.tab.c $@
# endef

# define DO_TOOLS_CC
# $(echo_cmd) "TOOLS_CC $<"
# $(Q)$(TOOLS_CC) $(TOOLS_CFLAGS) -o $@ -c $<
# endef

# define DO_TOOLS_CC_DAGCHECK
# $(echo_cmd) "TOOLS_CC_DAGCHECK $<"
# $(Q)$(TOOLS_CC) $(TOOLS_CFLAGS) -Wno-unused -o $@ -c $<
# endef

# LBURG       = $(B)/tools/lburg/lburg$(TOOLS_BINEXT)
# DAGCHECK_C  = $(B)/tools/rcc/dagcheck.c
# Q3RCC       = $(B)/tools/q3rcc$(TOOLS_BINEXT)
# Q3CPP       = $(B)/tools/q3cpp$(TOOLS_BINEXT)
# Q3LCC       = $(B)/tools/q3lcc$(TOOLS_BINEXT)
# Q3ASM       = $(B)/tools/q3asm$(TOOLS_BINEXT)

# LBURGOBJ= \
#   $(B)/tools/lburg/lburg.o \
#   $(B)/tools/lburg/gram.o

# # override GNU Make built-in rule for converting gram.y to gram.c
# %.c: %.y
# ifeq ($(USE_YACC),1)
# 	$(DO_YACC)
# endif

# $(B)/tools/lburg/%.o: $(LBURGDIR)/%.c
# 	$(DO_TOOLS_CC)

# $(LBURG): $(LBURGOBJ)
# 	$(echo_cmd) "LD $@"
# 	$(Q)$(TOOLS_CC) $(TOOLS_CFLAGS) $(TOOLS_LDFLAGS) -o $@ $^ $(TOOLS_LIBS)

# Q3RCCOBJ = \
#   $(B)/tools/rcc/alloc.o \
#   $(B)/tools/rcc/bind.o \
#   $(B)/tools/rcc/bytecode.o \
#   $(B)/tools/rcc/dag.o \
#   $(B)/tools/rcc/dagcheck.o \
#   $(B)/tools/rcc/decl.o \
#   $(B)/tools/rcc/enode.o \
#   $(B)/tools/rcc/error.o \
#   $(B)/tools/rcc/event.o \
#   $(B)/tools/rcc/expr.o \
#   $(B)/tools/rcc/gen.o \
#   $(B)/tools/rcc/init.o \
#   $(B)/tools/rcc/inits.o \
#   $(B)/tools/rcc/input.o \
#   $(B)/tools/rcc/lex.o \
#   $(B)/tools/rcc/list.o \
#   $(B)/tools/rcc/main.o \
#   $(B)/tools/rcc/null.o \
#   $(B)/tools/rcc/output.o \
#   $(B)/tools/rcc/prof.o \
#   $(B)/tools/rcc/profio.o \
#   $(B)/tools/rcc/simp.o \
#   $(B)/tools/rcc/stmt.o \
#   $(B)/tools/rcc/string.o \
#   $(B)/tools/rcc/sym.o \
#   $(B)/tools/rcc/symbolic.o \
#   $(B)/tools/rcc/trace.o \
#   $(B)/tools/rcc/tree.o \
#   $(B)/tools/rcc/types.o

# $(DAGCHECK_C): $(LBURG) $(Q3LCCSRCDIR)/dagcheck.md
# 	$(echo_cmd) "LBURG $(Q3LCCSRCDIR)/dagcheck.md"
# 	$(Q)$(LBURG) $(Q3LCCSRCDIR)/dagcheck.md $@

# $(B)/tools/rcc/dagcheck.o: $(DAGCHECK_C)
# 	$(DO_TOOLS_CC_DAGCHECK)

# $(B)/tools/rcc/%.o: $(Q3LCCSRCDIR)/%.c
# 	$(DO_TOOLS_CC)

# $(Q3RCC): $(Q3RCCOBJ)
# 	$(echo_cmd) "LD $@"
# 	$(Q)$(TOOLS_CC) $(TOOLS_CFLAGS) $(TOOLS_LDFLAGS) -o $@ $^ $(TOOLS_LIBS)

# Q3CPPOBJ = \
#   $(B)/tools/cpp/cpp.o \
#   $(B)/tools/cpp/lex.o \
#   $(B)/tools/cpp/nlist.o \
#   $(B)/tools/cpp/tokens.o \
#   $(B)/tools/cpp/macro.o \
#   $(B)/tools/cpp/eval.o \
#   $(B)/tools/cpp/include.o \
#   $(B)/tools/cpp/hideset.o \
#   $(B)/tools/cpp/getopt.o \
#   $(B)/tools/cpp/unix.o

# $(B)/tools/cpp/%.o: $(Q3CPPDIR)/%.c
# 	$(DO_TOOLS_CC)

# $(Q3CPP): $(Q3CPPOBJ)
# 	$(echo_cmd) "LD $@"
# 	$(Q)$(TOOLS_CC) $(TOOLS_CFLAGS) $(TOOLS_LDFLAGS) -o $@ $^ $(TOOLS_LIBS)

# Q3LCCOBJ = \
# 	$(B)/tools/etc/lcc.o \
# 	$(B)/tools/etc/bytecode.o

# $(B)/tools/etc/%.o: $(Q3LCCETCDIR)/%.c
# 	$(DO_TOOLS_CC)

# $(Q3LCC): $(Q3LCCOBJ) $(Q3RCC) $(Q3CPP)
# 	$(echo_cmd) "LD $@"
# 	$(Q)$(TOOLS_CC) $(TOOLS_CFLAGS) $(TOOLS_LDFLAGS) -o $@ $(Q3LCCOBJ) $(TOOLS_LIBS)

# define DO_Q3LCC
# $(echo_cmd) "Q3LCC $<"
# $(Q)$(Q3LCC) $(BASEGAME_CFLAGS) $(QVM_CFLAGS) -o $@ $<
# endef

# define DO_CGAME_Q3LCC
# $(echo_cmd) "CGAME_Q3LCC $<"
# $(Q)$(Q3LCC) $(BASEGAME_CFLAGS) -DCGAME $(QVM_CFLAGS) -o $@ $<
# endef

# define DO_GAME_Q3LCC
# $(echo_cmd) "GAME_Q3LCC $<"
# $(Q)$(Q3LCC) $(BASEGAME_CFLAGS) -DGAME $(QVM_CFLAGS) -o $@ $<
# endef

# define DO_Q3LCC_MISSIONPACK
# $(echo_cmd) "Q3LCC_MISSIONPACK $<"
# $(Q)$(Q3LCC) $(MISSIONPACK_CFLAGS) $(QVM_CFLAGS) -o $@ $<
# endef

# define DO_CGAME_Q3LCC_MISSIONPACK
# $(echo_cmd) "CGAME_Q3LCC_MISSIONPACK $<"
# $(Q)$(Q3LCC) $(MISSIONPACK_CFLAGS) -DCGAME $(QVM_CFLAGS) -o $@ $<
# endef

# define DO_GAME_Q3LCC_MISSIONPACK
# $(echo_cmd) "GAME_Q3LCC_MISSIONPACK $<"
# $(Q)$(Q3LCC) $(MISSIONPACK_CFLAGS) -DGAME $(QVM_CFLAGS) -o $@ $<
# endef


# Q3ASMOBJ = \
#   $(B)/tools/asm/q3asm.o \
#   $(B)/tools/asm/cmdlib.o

# $(B)/tools/asm/%.o: $(Q3ASMDIR)/%.c
# 	$(DO_TOOLS_CC)

# $(Q3ASM): $(Q3ASMOBJ)
# 	$(echo_cmd) "LD $@"
# 	$(Q)$(TOOLS_CC) $(TOOLS_CFLAGS) $(TOOLS_LDFLAGS) -o $@ $^ $(TOOLS_LIBS)


# #############################################################################
# ## BASEGAME CGAME
# #############################################################################

set(Q3CGOBJ
  cgame/cg_main.c
  game/bg_misc.c
  game/bg_pmove.c
  game/bg_slidemove.c
  game/bg_lib.c
  game/bg_tracemap.c
  cgame/cg_atmospheric.c
  cgame/cg_console.c
  cgame/cg_consolecmds.c
  cgame/cg_newdraw.c
  cgame/cg_draw.c
  cgame/cg_drawtools.c
  cgame/cg_effects.c
  cgame/cg_ents.c
  cgame/cg_event.c
  cgame/cg_field.c
  cgame/cg_info.c
  cgame/cg_input.c
  cgame/cg_localents.c
  cgame/cg_marks.c
  cgame/cg_particles.c
  cgame/cg_players.c
  cgame/cg_playerstate.c
  cgame/cg_polybus.c
  cgame/cg_predict.c
  cgame/cg_scoreboard.c
  cgame/cg_servercmds.c
  cgame/cg_snapshot.c
  cgame/cg_spawn.c
  cgame/cg_surface.c
  cgame/cg_syscalls.c
  cgame/cg_text.c
  cgame/cg_unlagged.c
  cgame/cg_view.c
  cgame/cg_weapons.c
  
  q3_ui/ui_main.c
  q3_ui/ui_addbots.c
  q3_ui/ui_atoms.c
  q3_ui/ui_cinematics.c
  q3_ui/ui_confirm.c
  q3_ui/ui_connect.c
  q3_ui/ui_controls2.c
  q3_ui/ui_credits.c
  q3_ui/ui_demo2.c
  q3_ui/ui_display.c
  q3_ui/ui_gameinfo.c
  q3_ui/ui_ingame.c
  q3_ui/ui_ingame_selectplayer.c
  q3_ui/ui_joystick.c
  q3_ui/ui_loadconfig.c
  q3_ui/ui_menu.c
  q3_ui/ui_mfield.c
  q3_ui/ui_mods.c
  q3_ui/ui_network.c
  q3_ui/ui_options.c
  q3_ui/ui_playermodel.c
  q3_ui/ui_players.c
  q3_ui/ui_playersettings.c
  q3_ui/ui_preferences.c
  q3_ui/ui_qmenu.c
  q3_ui/ui_removebots.c
  q3_ui/ui_saveconfig.c
  q3_ui/ui_selectplayer.c
  q3_ui/ui_serverinfo.c
  q3_ui/ui_servers2.c
  q3_ui/ui_setup.c
  q3_ui/ui_sound.c
  q3_ui/ui_sparena.c
  q3_ui/ui_specifyserver.c
  q3_ui/ui_splevel.c
  q3_ui/ui_sppostgame.c
  q3_ui/ui_spskill.c
  q3_ui/ui_startserver.c
  q3_ui/ui_team.c
  q3_ui/ui_teamorders.c
  q3_ui/ui_video.c
  
  qcommon/q_math.c
  qcommon/q_shared.c
  qcommon/q_unicode.c)

if(${USE_BASEGAME_MP_HUD})
  list(APPEND Q3CGOBJ
    mpui/ui_shared.c)
endif()

list(TRANSFORM Q3CGOBJ PREPEND code/)

# Q3CGVMOBJ = $(Q3CGOBJ:%.o=%.asm)

# $(B)/$(BASEGAME)/$(VM_PREFIX)cgame_$(SHLIBNAME): $(Q3CGOBJ)
# 	$(echo_cmd) "LD $@"
# 	$(Q)$(CC) $(CFLAGS) $(SHLIBLDFLAGS) -o $@ $(Q3CGOBJ)

# $(B)/$(BASEGAME)/vm/$(VM_PREFIX)cgame.qvm: $(Q3CGVMOBJ) $(GDIR)/bg_syscalls.asm $(Q3ASM)
# 	$(echo_cmd) "Q3ASM $@"
# 	$(Q)$(Q3ASM) -o $@ $(Q3CGVMOBJ) $(GDIR)/bg_syscalls.asm

if(${BUILD_GAME_SO})
   if(${BUILD_BASEGAME})
    add_library("${VM_PREFIX}cgame_${SHLIBNAME}" SHARED
        ${Q3CGOBJ})
    set_target_properties("${VM_PREFIX}cgame_${SHLIBNAME}" PROPERTIES PREFIX "")
    target_compile_options("${VM_PREFIX}cgame_${SHLIBNAME}" PUBLIC ${BASE_CFLAGS} ${BASEGAME_CFLAGS} -DCGAME ${SHLIBCFLAGS} ${OPTIMIZEVM})
   endif()
   if(${BUILD_MISSIONPACK})
#     TARGETS += \
#       $(B)/$(MISSIONPACK)/$(VM_PREFIX)cgame_$(SHLIBNAME) \
#       $(B)/$(MISSIONPACK)/$(VM_PREFIX)game_$(SHLIBNAME)
   endif()
endif()

# #############################################################################
# ## MISSIONPACK CGAME
# #############################################################################

# MPCGOBJ = \
#   $(B)/$(MISSIONPACK)/cgame/cg_main.o \
#   $(B)/$(MISSIONPACK)/cgame/bg_misc.o \
#   $(B)/$(MISSIONPACK)/cgame/bg_pmove.o \
#   $(B)/$(MISSIONPACK)/cgame/bg_slidemove.o \
#   $(B)/$(MISSIONPACK)/cgame/bg_lib.o \
#   $(B)/$(MISSIONPACK)/cgame/bg_tracemap.o \
#   $(B)/$(MISSIONPACK)/cgame/cg_atmospheric.o \
#   $(B)/$(MISSIONPACK)/cgame/cg_console.o \
#   $(B)/$(MISSIONPACK)/cgame/cg_consolecmds.o \
#   $(B)/$(MISSIONPACK)/cgame/cg_newdraw.o \
#   $(B)/$(MISSIONPACK)/cgame/cg_draw.o \
#   $(B)/$(MISSIONPACK)/cgame/cg_drawtools.o \
#   $(B)/$(MISSIONPACK)/cgame/cg_effects.o \
#   $(B)/$(MISSIONPACK)/cgame/cg_ents.o \
#   $(B)/$(MISSIONPACK)/cgame/cg_event.o \
#   $(B)/$(MISSIONPACK)/cgame/cg_field.o \
#   $(B)/$(MISSIONPACK)/cgame/cg_info.o \
#   $(B)/$(MISSIONPACK)/cgame/cg_input.o \
#   $(B)/$(MISSIONPACK)/cgame/cg_localents.o \
#   $(B)/$(MISSIONPACK)/cgame/cg_marks.o \
#   $(B)/$(MISSIONPACK)/cgame/cg_particles.o \
#   $(B)/$(MISSIONPACK)/cgame/cg_players.o \
#   $(B)/$(MISSIONPACK)/cgame/cg_playerstate.o \
#   $(B)/$(MISSIONPACK)/cgame/cg_polybus.o \
#   $(B)/$(MISSIONPACK)/cgame/cg_predict.o \
#   $(B)/$(MISSIONPACK)/cgame/cg_scoreboard.o \
#   $(B)/$(MISSIONPACK)/cgame/cg_servercmds.o \
#   $(B)/$(MISSIONPACK)/cgame/cg_snapshot.o \
#   $(B)/$(MISSIONPACK)/cgame/cg_spawn.o \
#   $(B)/$(MISSIONPACK)/cgame/cg_surface.o \
#   $(B)/$(MISSIONPACK)/cgame/cg_syscalls.o \
#   $(B)/$(MISSIONPACK)/cgame/cg_text.o \
#   $(B)/$(MISSIONPACK)/cgame/cg_unlagged.o \
#   $(B)/$(MISSIONPACK)/cgame/cg_view.o \
#   $(B)/$(MISSIONPACK)/cgame/cg_weapons.o \
#   \
#   $(B)/$(MISSIONPACK)/qcommon/q_math.o \
#   $(B)/$(MISSIONPACK)/qcommon/q_shared.o \
#   $(B)/$(MISSIONPACK)/qcommon/q_unicode.o

# ifeq ($(USE_MISSIONPACK_Q3_UI), 1)
# MPCGOBJ += \
#   $(B)/$(MISSIONPACK)/q3ui/ui_main.o \
#   $(B)/$(MISSIONPACK)/q3ui/ui_addbots.o \
#   $(B)/$(MISSIONPACK)/q3ui/ui_atoms.o \
#   $(B)/$(MISSIONPACK)/q3ui/ui_cinematics.o \
#   $(B)/$(MISSIONPACK)/q3ui/ui_confirm.o \
#   $(B)/$(MISSIONPACK)/q3ui/ui_connect.o \
#   $(B)/$(MISSIONPACK)/q3ui/ui_controls2.o \
#   $(B)/$(MISSIONPACK)/q3ui/ui_credits.o \
#   $(B)/$(MISSIONPACK)/q3ui/ui_demo2.o \
#   $(B)/$(MISSIONPACK)/q3ui/ui_display.o \
#   $(B)/$(MISSIONPACK)/q3ui/ui_gameinfo.o \
#   $(B)/$(MISSIONPACK)/q3ui/ui_ingame.o \
#   $(B)/$(MISSIONPACK)/q3ui/ui_ingame_selectplayer.o \
#   $(B)/$(MISSIONPACK)/q3ui/ui_joystick.o \
#   $(B)/$(MISSIONPACK)/q3ui/ui_loadconfig.o \
#   $(B)/$(MISSIONPACK)/q3ui/ui_menu.o \
#   $(B)/$(MISSIONPACK)/q3ui/ui_mfield.o \
#   $(B)/$(MISSIONPACK)/q3ui/ui_mods.o \
#   $(B)/$(MISSIONPACK)/q3ui/ui_network.o \
#   $(B)/$(MISSIONPACK)/q3ui/ui_options.o \
#   $(B)/$(MISSIONPACK)/q3ui/ui_playermodel.o \
#   $(B)/$(MISSIONPACK)/q3ui/ui_players.o \
#   $(B)/$(MISSIONPACK)/q3ui/ui_playersettings.o \
#   $(B)/$(MISSIONPACK)/q3ui/ui_preferences.o \
#   $(B)/$(MISSIONPACK)/q3ui/ui_qmenu.o \
#   $(B)/$(MISSIONPACK)/q3ui/ui_removebots.o \
#   $(B)/$(MISSIONPACK)/q3ui/ui_saveconfig.o \
#   $(B)/$(MISSIONPACK)/q3ui/ui_selectplayer.o \
#   $(B)/$(MISSIONPACK)/q3ui/ui_serverinfo.o \
#   $(B)/$(MISSIONPACK)/q3ui/ui_servers2.o \
#   $(B)/$(MISSIONPACK)/q3ui/ui_setup.o \
#   $(B)/$(MISSIONPACK)/q3ui/ui_sound.o \
#   $(B)/$(MISSIONPACK)/q3ui/ui_sparena.o \
#   $(B)/$(MISSIONPACK)/q3ui/ui_specifyserver.o \
#   $(B)/$(MISSIONPACK)/q3ui/ui_splevel.o \
#   $(B)/$(MISSIONPACK)/q3ui/ui_sppostgame.o \
#   $(B)/$(MISSIONPACK)/q3ui/ui_spskill.o \
#   $(B)/$(MISSIONPACK)/q3ui/ui_startserver.o \
#   $(B)/$(MISSIONPACK)/q3ui/ui_team.o \
#   $(B)/$(MISSIONPACK)/q3ui/ui_teamorders.o \
#   $(B)/$(MISSIONPACK)/q3ui/ui_video.o

# ifeq ($(USE_MISSIONPACK_MP_HUD), 1)
# MPCGOBJ += \
#   $(B)/$(MISSIONPACK)/ui/ui_shared.o
# endif
# else
# MPCGOBJ += \
#   $(B)/$(MISSIONPACK)/ui/ui_main.o \
#   $(B)/$(MISSIONPACK)/ui/ui_atoms.o \
#   $(B)/$(MISSIONPACK)/ui/ui_gameinfo.o \
#   $(B)/$(MISSIONPACK)/ui/ui_players.o \
#   $(B)/$(MISSIONPACK)/ui/ui_shared.o
# endif

# MPCGVMOBJ = $(MPCGOBJ:%.o=%.asm)

# $(B)/$(MISSIONPACK)/$(VM_PREFIX)cgame_$(SHLIBNAME): $(MPCGOBJ)
# 	$(echo_cmd) "LD $@"
# 	$(Q)$(CC) $(CFLAGS) $(SHLIBLDFLAGS) -o $@ $(MPCGOBJ)

# $(B)/$(MISSIONPACK)/vm/$(VM_PREFIX)cgame.qvm: $(MPCGVMOBJ) $(GDIR)/bg_syscalls.asm $(Q3ASM)
# 	$(echo_cmd) "Q3ASM $@"
# 	$(Q)$(Q3ASM) -o $@ $(MPCGVMOBJ) $(GDIR)/bg_syscalls.asm


# #############################################################################
# ## BASEGAME GAME
# #############################################################################

set(Q3GOBJ
  game/g_main.c
  game/ai_char.c
  game/ai_chat.c
  game/ai_chat_sys.c
  game/ai_cmd.c
  game/ai_dmnet.c
  game/ai_dmq3.c
  game/ai_ea.c
  game/ai_gen.c
  game/ai_goal.c
  game/ai_main.c
  game/ai_move.c
  game/ai_team.c
  game/ai_weap.c
  game/ai_weight.c
  game/ai_vcmd.c
  game/bg_misc.c
  game/bg_pmove.c
  game/bg_slidemove.c
  game/bg_lib.c
  game/bg_tracemap.c
  game/g_active.c
  game/g_arenas.c
  game/g_bot.c
  game/g_botlib.c
  game/g_client.c
  game/g_cmds.c
  game/g_combat.c
  game/g_items.c
  game/g_misc.c
  game/g_missile.c
  game/g_mover.c
  game/g_session.c
  game/g_spawn.c
  game/g_svcmds.c
  game/g_syscalls.c
  game/g_target.c
  game/g_team.c
  game/g_trigger.c
  game/g_unlagged.c
  game/g_utils.c
  game/g_weapon.c
  
  botlib/be_aas_bspq3.c
  botlib/be_aas_cluster.c
  botlib/be_aas_debug.c
  botlib/be_aas_entity.c
  botlib/be_aas_file.c
  botlib/be_aas_main.c
  botlib/be_aas_move.c
  botlib/be_aas_optimize.c
  botlib/be_aas_reach.c
  botlib/be_aas_route.c
  botlib/be_aas_routealt.c
  botlib/be_aas_sample.c
  botlib/be_interface.c
  botlib/l_crc.c
  botlib/l_libvar.c
  botlib/l_log.c
  botlib/l_memory.c
  
  qcommon/q_math.c
  qcommon/q_shared.c)

list(TRANSFORM Q3GOBJ PREPEND code/)

# Q3GVMOBJ = $(Q3GOBJ:%.o=%.asm)

# $(B)/$(BASEGAME)/$(VM_PREFIX)game_$(SHLIBNAME): $(Q3GOBJ)
# 	$(echo_cmd) "LD $@"
# 	$(Q)$(CC) $(CFLAGS) $(SHLIBLDFLAGS) -o $@ $(Q3GOBJ)

if(${BUILD_GAME_SO})
   if(${BUILD_BASEGAME})
    add_library("${VM_PREFIX}game_${SHLIBNAME}" SHARED
        ${Q3GOBJ})
    set_target_properties("${VM_PREFIX}game_${SHLIBNAME}" PROPERTIES PREFIX "")
    target_compile_options("${VM_PREFIX}game_${SHLIBNAME}" PUBLIC ${BASE_CFLAGS} ${BASEGAME_CFLAGS} -DGAME ${SHLIBCFLAGS} ${OPTIMIZEVM})
  endif()
   if(${BUILD_MISSIONPACK})
#     TARGETS += \
#       $(B)/$(MISSIONPACK)/$(VM_PREFIX)cgame_$(SHLIBNAME) \
#       $(B)/$(MISSIONPACK)/$(VM_PREFIX)game_$(SHLIBNAME)
   endif()
endif()

# $(B)/$(BASEGAME)/vm/$(VM_PREFIX)game.qvm: $(Q3GVMOBJ) $(GDIR)/bg_syscalls.asm $(Q3ASM)
# 	$(echo_cmd) "Q3ASM $@"
# 	$(Q)$(Q3ASM) -o $@ $(Q3GVMOBJ) $(GDIR)/bg_syscalls.asm

# #############################################################################
# ## MISSIONPACK GAME
# #############################################################################

# MPGOBJ = \
#   $(B)/$(MISSIONPACK)/game/g_main.o \
#   $(B)/$(MISSIONPACK)/game/ai_char.o \
#   $(B)/$(MISSIONPACK)/game/ai_chat.o \
#   $(B)/$(MISSIONPACK)/game/ai_chat_sys.o \
#   $(B)/$(MISSIONPACK)/game/ai_cmd.o \
#   $(B)/$(MISSIONPACK)/game/ai_dmnet.o \
#   $(B)/$(MISSIONPACK)/game/ai_dmq3.o \
#   $(B)/$(MISSIONPACK)/game/ai_ea.o \
#   $(B)/$(MISSIONPACK)/game/ai_gen.o \
#   $(B)/$(MISSIONPACK)/game/ai_goal.o \
#   $(B)/$(MISSIONPACK)/game/ai_main.o \
#   $(B)/$(MISSIONPACK)/game/ai_move.o \
#   $(B)/$(MISSIONPACK)/game/ai_team.o \
#   $(B)/$(MISSIONPACK)/game/ai_weap.o \
#   $(B)/$(MISSIONPACK)/game/ai_weight.o \
#   $(B)/$(MISSIONPACK)/game/ai_vcmd.o \
#   $(B)/$(MISSIONPACK)/game/bg_misc.o \
#   $(B)/$(MISSIONPACK)/game/bg_pmove.o \
#   $(B)/$(MISSIONPACK)/game/bg_slidemove.o \
#   $(B)/$(MISSIONPACK)/game/bg_lib.o \
#   $(B)/$(MISSIONPACK)/game/bg_tracemap.o \
#   $(B)/$(MISSIONPACK)/game/g_active.o \
#   $(B)/$(MISSIONPACK)/game/g_arenas.o \
#   $(B)/$(MISSIONPACK)/game/g_bot.o \
#   $(B)/$(MISSIONPACK)/game/g_botlib.o \
#   $(B)/$(MISSIONPACK)/game/g_client.o \
#   $(B)/$(MISSIONPACK)/game/g_cmds.o \
#   $(B)/$(MISSIONPACK)/game/g_combat.o \
#   $(B)/$(MISSIONPACK)/game/g_items.o \
#   $(B)/$(MISSIONPACK)/game/g_misc.o \
#   $(B)/$(MISSIONPACK)/game/g_missile.o \
#   $(B)/$(MISSIONPACK)/game/g_mover.o \
#   $(B)/$(MISSIONPACK)/game/g_session.o \
#   $(B)/$(MISSIONPACK)/game/g_spawn.o \
#   $(B)/$(MISSIONPACK)/game/g_svcmds.o \
#   $(B)/$(MISSIONPACK)/game/g_syscalls.o \
#   $(B)/$(MISSIONPACK)/game/g_target.o \
#   $(B)/$(MISSIONPACK)/game/g_team.o \
#   $(B)/$(MISSIONPACK)/game/g_trigger.o \
#   $(B)/$(MISSIONPACK)/game/g_unlagged.o \
#   $(B)/$(MISSIONPACK)/game/g_utils.o \
#   $(B)/$(MISSIONPACK)/game/g_weapon.o \
#   \
#   $(B)/$(MISSIONPACK)/botlib/be_aas_bspq3.o \
#   $(B)/$(MISSIONPACK)/botlib/be_aas_cluster.o \
#   $(B)/$(MISSIONPACK)/botlib/be_aas_debug.o \
#   $(B)/$(MISSIONPACK)/botlib/be_aas_entity.o \
#   $(B)/$(MISSIONPACK)/botlib/be_aas_file.o \
#   $(B)/$(MISSIONPACK)/botlib/be_aas_main.o \
#   $(B)/$(MISSIONPACK)/botlib/be_aas_move.o \
#   $(B)/$(MISSIONPACK)/botlib/be_aas_optimize.o \
#   $(B)/$(MISSIONPACK)/botlib/be_aas_reach.o \
#   $(B)/$(MISSIONPACK)/botlib/be_aas_route.o \
#   $(B)/$(MISSIONPACK)/botlib/be_aas_routealt.o \
#   $(B)/$(MISSIONPACK)/botlib/be_aas_sample.o \
#   $(B)/$(MISSIONPACK)/botlib/be_interface.o \
#   $(B)/$(MISSIONPACK)/botlib/l_crc.o \
#   $(B)/$(MISSIONPACK)/botlib/l_libvar.o \
#   $(B)/$(MISSIONPACK)/botlib/l_log.o \
#   $(B)/$(MISSIONPACK)/botlib/l_memory.o \
#   \
#   $(B)/$(MISSIONPACK)/qcommon/q_math.o \
#   $(B)/$(MISSIONPACK)/qcommon/q_shared.o

# MPGVMOBJ = $(MPGOBJ:%.o=%.asm)

# $(B)/$(MISSIONPACK)/$(VM_PREFIX)game_$(SHLIBNAME): $(MPGOBJ)
# 	$(echo_cmd) "LD $@"
# 	$(Q)$(CC) $(CFLAGS) $(SHLIBLDFLAGS) -o $@ $(MPGOBJ)

# $(B)/$(MISSIONPACK)/vm/$(VM_PREFIX)game.qvm: $(MPGVMOBJ) $(GDIR)/bg_syscalls.asm $(Q3ASM)
# 	$(echo_cmd) "Q3ASM $@"
# 	$(Q)$(Q3ASM) -o $@ $(MPGVMOBJ) $(GDIR)/bg_syscalls.asm


# # Extra dependencies to ensure the git version is incorporated
# ifeq ($(USE_GIT),1)
#   GITVEROBJ = \
#     $(B)/$(BASEGAME)/cgame/cg_main.o \
#     $(B)/$(BASEGAME)/cgame/cg_console.o \
#     $(B)/$(BASEGAME)/game/g_main.o \
#     $(B)/$(MISSIONPACK)/cgame/cg_main.o \
#     $(B)/$(MISSIONPACK)/cgame/cg_console.o \
#     $(B)/$(MISSIONPACK)/game/g_main.o

#   GITVERVMOBJ = $(GITVEROBJ:%.o=%.asm)

#   $(GITVEROBJ) : .git
#   $(GITVERVMOBJ) : .git
# endif


# #############################################################################
# ## GAME MODULE RULES
# #############################################################################

# $(B)/$(BASEGAME)/cgame/bg_%.o: $(GDIR)/bg_%.c
# 	$(DO_CGAME_CC)

# $(B)/$(BASEGAME)/cgame/%.o: $(CGDIR)/%.c
# 	$(DO_CGAME_CC)

# $(B)/$(BASEGAME)/ui/%.o: $(Q3UIDIR)/%.c
# 	$(DO_CGAME_CC)

# $(B)/$(BASEGAME)/mpui/%.o: $(UIDIR)/%.c
# 	$(DO_CGAME_CC)

# $(B)/$(BASEGAME)/cgame/bg_%.asm: $(GDIR)/bg_%.c $(Q3LCC)
# 	$(DO_CGAME_Q3LCC)

# $(B)/$(BASEGAME)/cgame/%.asm: $(CGDIR)/%.c $(Q3LCC)
# 	$(DO_CGAME_Q3LCC)

# $(B)/$(BASEGAME)/ui/%.asm: $(Q3UIDIR)/%.c $(Q3LCC)
# 	$(DO_CGAME_Q3LCC)

# $(B)/$(BASEGAME)/mpui/%.asm: $(UIDIR)/%.c $(Q3LCC)
# 	$(DO_CGAME_Q3LCC)

# $(B)/$(MISSIONPACK)/cgame/bg_%.o: $(GDIR)/bg_%.c
# 	$(DO_CGAME_CC_MISSIONPACK)

# $(B)/$(MISSIONPACK)/cgame/%.o: $(CGDIR)/%.c
# 	$(DO_CGAME_CC_MISSIONPACK)

# $(B)/$(MISSIONPACK)/ui/%.o: $(UIDIR)/%.c
# 	$(DO_CGAME_CC_MISSIONPACK)

# $(B)/$(MISSIONPACK)/q3ui/%.o: $(Q3UIDIR)/%.c
# 	$(DO_CGAME_CC_MISSIONPACK)

# $(B)/$(MISSIONPACK)/cgame/bg_%.asm: $(GDIR)/bg_%.c $(Q3LCC)
# 	$(DO_CGAME_Q3LCC_MISSIONPACK)

# $(B)/$(MISSIONPACK)/cgame/%.asm: $(CGDIR)/%.c $(Q3LCC)
# 	$(DO_CGAME_Q3LCC_MISSIONPACK)

# $(B)/$(MISSIONPACK)/ui/%.asm: $(UIDIR)/%.c $(Q3LCC)
# 	$(DO_CGAME_Q3LCC_MISSIONPACK)

# $(B)/$(MISSIONPACK)/q3ui/%.asm: $(Q3UIDIR)/%.c $(Q3LCC)
# 	$(DO_CGAME_Q3LCC_MISSIONPACK)


# $(B)/$(BASEGAME)/game/%.o: $(GDIR)/%.c
# 	$(DO_GAME_CC)

# $(B)/$(BASEGAME)/game/%.asm: $(GDIR)/%.c $(Q3LCC)
# 	$(DO_GAME_Q3LCC)

# $(B)/$(MISSIONPACK)/game/%.o: $(GDIR)/%.c
# 	$(DO_GAME_CC_MISSIONPACK)

# $(B)/$(MISSIONPACK)/game/%.asm: $(GDIR)/%.c $(Q3LCC)
# 	$(DO_GAME_Q3LCC_MISSIONPACK)

# $(B)/$(BASEGAME)/botlib/%.o: $(BLIBDIR)/%.c
# 	$(DO_GAME_CC)

# $(B)/$(BASEGAME)/botlib/%.asm: $(BLIBDIR)/%.c $(Q3LCC)
# 	$(DO_GAME_Q3LCC)

# $(B)/$(MISSIONPACK)/botlib/%.o: $(BLIBDIR)/%.c
# 	$(DO_GAME_CC_MISSIONPACK)

# $(B)/$(MISSIONPACK)/botlib/%.asm: $(BLIBDIR)/%.c $(Q3LCC)
# 	$(DO_GAME_Q3LCC_MISSIONPACK)

# $(B)/$(BASEGAME)/qcommon/%.o: $(CMDIR)/%.c
# 	$(DO_SHLIB_CC)

# $(B)/$(BASEGAME)/qcommon/%.asm: $(CMDIR)/%.c $(Q3LCC)
# 	$(DO_Q3LCC)

# $(B)/$(MISSIONPACK)/qcommon/%.o: $(CMDIR)/%.c
# 	$(DO_SHLIB_CC_MISSIONPACK)

# $(B)/$(MISSIONPACK)/qcommon/%.asm: $(CMDIR)/%.c $(Q3LCC)
# 	$(DO_Q3LCC_MISSIONPACK)


# #############################################################################
# # MISC
# #############################################################################

# OBJ = $(MPGOBJ) $(Q3GOBJ) $(Q3CGOBJ) $(MPCGOBJ) $(Q3UIOBJ) $(MPUIOBJ) \
#   $(MPGVMOBJ) $(Q3GVMOBJ) $(Q3CGVMOBJ) $(MPCGVMOBJ) $(Q3UIVMOBJ) $(MPUIVMOBJ)
# TOOLSOBJ = $(LBURGOBJ) $(Q3CPPOBJ) $(Q3RCCOBJ) $(Q3LCCOBJ) $(Q3ASMOBJ)

# copyfiles: release
# ifneq ($(BUILD_GAME_SO),0)
#   ifneq ($(BUILD_BASEGAME),0)
# 	-$(MKDIR) -m 0755 $(COPYDIR)/$(BASEGAME)
#   endif
#   ifneq ($(BUILD_MISSIONPACK),0)
# 	-$(MKDIR) -m 0755 $(COPYDIR)/$(MISSIONPACK)
#   endif
# endif

if(${BUILD_GAME_SO})
   if(${BUILD_BASEGAME})
    install(TARGETS ${VM_PREFIX}cgame_${SHLIBNAME} DESTINATION ${COPYDIR}/${BASEGAME})
    install(TARGETS ${VM_PREFIX}game_${SHLIBNAME} DESTINATION ${COPYDIR}/${BASEGAME})
   endif()
#   ifneq ($(BUILD_MISSIONPACK),0)
# 	$(INSTALL) $(STRIP_FLAG) -m 0755 $(BR)/$(MISSIONPACK)/$(VM_PREFIX)cgame_$(SHLIBNAME) \
# 					$(COPYDIR)/$(MISSIONPACK)/.
# 	$(INSTALL) $(STRIP_FLAG) -m 0755 $(BR)/$(MISSIONPACK)/$(VM_PREFIX)game_$(SHLIBNAME) \
# 					$(COPYDIR)/$(MISSIONPACK)/.
#   endif
endif()

# clean: clean-debug clean-release

# clean-debug:
# 	@$(MAKE) clean2 B=$(BD)

# clean-release:
# 	@$(MAKE) clean2 B=$(BR)

# clean2:
# 	@echo "CLEAN $(B)"
# 	@rm -f $(OBJ)
# 	@rm -f $(OBJ_D_FILES)
# 	@rm -f $(TARGETS)

# toolsclean: toolsclean-debug toolsclean-release

# toolsclean-debug:
# 	@$(MAKE) toolsclean2 B=$(BD)

# toolsclean-release:
# 	@$(MAKE) toolsclean2 B=$(BR)

# toolsclean2:
# 	@echo "TOOLS_CLEAN $(B)"
# 	@rm -f $(TOOLSOBJ)
# 	@rm -f $(TOOLSOBJ_D_FILES)
# 	@rm -f $(LBURG) $(DAGCHECK_C) $(Q3RCC) $(Q3CPP) $(Q3LCC) $(Q3ASM)

# distclean: clean toolsclean
# 	@rm -rf $(BUILD_DIR)

# dist:
# 	git archive --format zip --output $(SOURCE_ARCHIVE)-$(VERSION).zip HEAD

# #############################################################################
# # DEPENDENCIES
# #############################################################################

# ifneq ($(B),)
#   OBJ_D_FILES=$(filter %.d,$(OBJ:%.o=%.d))
#   TOOLSOBJ_D_FILES=$(filter %.d,$(TOOLSOBJ:%.o=%.d))
#   -include $(OBJ_D_FILES) $(TOOLSOBJ_D_FILES)
# endif

# .PHONY: all clean clean2 clean-debug clean-release copyfiles \
# 	debug default dist distclean makedirs \
# 	release targets \
# 	toolsclean toolsclean2 toolsclean-debug toolsclean-release \
# 	$(OBJ_D_FILES) $(TOOLSOBJ_D_FILES)

# # If the target name contains "clean", don't do a parallel build
# ifneq ($(findstring clean, $(MAKECMDGOALS)),)
# .NOTPARALLEL:
# endif
