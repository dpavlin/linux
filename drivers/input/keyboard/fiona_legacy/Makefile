#
# Makefile for the Fiona Legacy Keyboard driver
#

obj-$(CONFIG_FIONA_LEGACY_KBD)                  += fiona_legacy.o
fiona_legacy-objs	:= fiona_keyboard.o fiona_eventmap.o

ifeq ($(CONFIG_FIONA_LEGACY_DEBUG),y)
EXTRA_CFLAGS += -DDEBUG
endif
