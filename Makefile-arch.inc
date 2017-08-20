ifndef ARCH

# Detect architecture if ARCH is not defined
MACHINE = $(shell gcc -dumpmachine)
ifneq (,$(findstring linux,$(MACHINE)))
	# Linux
	ARCH = lin
else ifneq (,$(findstring apple,$(MACHINE)))
	# Mac
	ARCH = mac
else ifneq (,$(findstring mingw,$(MACHINE)))
	# Windows
	ARCH = win
else
$(error Could not detect architecture. Specify manually with `make ARCH=...`)
endif

endif