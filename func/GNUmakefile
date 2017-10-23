PACKAGE     := $(shell basename `dirname \`pwd\``)Func
LIB_TYPE    := shared
LIB         := lib$(PACKAGE)
LIBCXXFILES := $(wildcard *.cxx)

override CPPFLAGS := -O3 -ffast-math -Wall -Wextra `pkg-config --cflags gtk+-2.0`

override LIBLIBS += -lgtk-x11-2.0 -lcairo

include SoftRelTools/standard.mk
