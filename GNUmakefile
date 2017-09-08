SUBDIRS := func

LIB_TYPE    := shared
LIB         := lib$(PACKAGE)
LIBCXXFILES := $(wildcard *.cxx)
JOBFILES    := $(wildcard *.fcl)

include SoftRelTools/standard.mk
include SoftRelTools/arch_spec_art.mk
include SoftRelTools/arch_spec_root.mk

override CPPFLAGS := $(CPPFLAGS) -I$(NOVADAQ_INC) -I$(NUTOOLS_INC)

LIBLINK := \
-L$(SRT_PRIVATE_CONTEXT)/lib/$(SRT_SUBDIR) \
-L$(SRT_PUBLIC_CONTEXT)/lib/$(SRT_SUBDIR) \
-l$(PACKAGE) \
-l$(PACKAGE)Func

override LIBLIBS += \
$(LOADLIBES) \
-L$(ART_LIB) \
-L$(SRT_PRIVATE_CONTEXT)/lib/$(SRT_SUBDIR) \
-L$(SRT_PUBLIC_CONTEXT)/lib/$(SRT_SUBDIR)
