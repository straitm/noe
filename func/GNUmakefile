PACKAGE     := noeFunc
LIB_TYPE    := shared
LIB         := lib$(PACKAGE)
LIBCXXFILES := $(wildcard *.cxx)

BINLIBS = -l$(PACKAGE) $(ROOTLIBS) $(ROOTGLIBS)

override CPPFLAGS :=  `pkg-config --cflags gtk+-2.0`

LOADLIBES += -lgtk-x11-2.0 -lcairo

########################################################################
include SoftRelTools/standard.mk
include SoftRelTools/arch_spec_root.mk
