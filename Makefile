# The P4's debug console: tools/p4port.sh finds it by the P4's USB hub
# port, so the C6's tty (also an Espressif USB-serial/JTAG unit) is never
# opened by mistake. P4_CONSOLE overrides it and is checked the same way;
# an rfc2217:// URL (a badge forwarded over the network) is taken as given.
# PORT is not read on purpose: other projects default it to /dev/ttyACM0,
# which on a Tanmatsu is often the C6.
P4_CONSOLE ?=
P4_FIND = tools/p4port.sh $(if $(P4_CONSOLE),--check '$(P4_CONSOLE)')
# In a recipe: the console into $$console, or stop.
CONSOLE_SH = $(if $(PORT),echo "note: PORT is not used here; P4_CONSOLE picks the console" >&2;) \
	console="$$($(P4_FIND))" || exit 1;
# pyserial, from the badgelink checkout's virtualenv.
PY := badgelink/tools/.venv/bin/python
# Empty: badgelink talks to the P4 over USB directly (16d0:0f9a). On a
# Tanmatsu /dev/ttyACM* is the ESP32-C6 radio coprocessor, and opening
# it resets the radio and crashes the running badge -- so install / run
# never touch it unless BADGELINKPORT is set on purpose (a serial port,
# or host:port for a TCP proxy).
BADGELINKPORT ?=

SHELL := /usr/bin/env bash

# App installation settings
APP_SLUG_NAME ?= com.annejan.portals
APP_INSTALL_BASE_PATH ?= /int/apps/
APP_INSTALL_PATH = $(APP_INSTALL_BASE_PATH)$(APP_SLUG_NAME)

# ESP-IDF tools path (needed for the RISC-V cross-compiler)
IDF_PATH ?= $(shell cat .IDF_PATH 2>/dev/null || echo `pwd`/esp-idf)
IDF_TOOLS_PATH ?= $(shell cat .IDF_TOOLS_PATH 2>/dev/null || echo `pwd`/esp-idf-tools)
IDF_VERSION ?= v6.0.2
IDF_GITHUB_ASSETS ?= dl.espressif.com/github_assets

export IDF_TOOLS_PATH
export IDF_GITHUB_ASSETS

BUILD ?= build

# The chamber textures. Generated (tools/make_textures.py) and committed,
# so a clone builds without Pillow; `make textures` regenerates them.
TEXTURES := $(patsubst textures/%,%,$(wildcard textures/*.png))

MAKEFLAGS += --silent

####

.PHONY: all
all: build

.PHONY: build
build: check
	@echo "=== Building app.so ==="
	mkdir -p $(BUILD)
	cd $(BUILD) && cmake .. && make
	@echo "=== Build complete: $(BUILD)/app.so ==="

# ---------------------------------------------------------------------
# Host checks: no badge, a second to run. `build` depends on `check`, so
# a broken invariant stops the build that broke it.
#
#   make check   portal maths, placement, and a scripted player solving
#                every chamber with real shots and physics
#   make shots   the game's renderer through a software rasterizer:
#                build/shots/*.png, to look at the portal passes
#
# The engine settings come out of CMakeLists.txt, so the host draws with
# the near plane and horizon the badge does.
# ---------------------------------------------------------------------
HOSTCC      ?= cc
HOST_SRCS   := main/level.c main/portal.c main/physics.c main/player.c main/game.c main/demo.c main/chamber.c main/draft.c main/recording.c main/attract.c main/cine.c main/pack.c main/review.c main/story.c main/desk.c \
               $(BUILD)/generated/chambers_builtin.c
ENGINE_DEFS := $(shell sed -n 's/^add_compile_definitions(\([A-Z_0-9]*=[0-9.f]*\))/-D\1/p' CMakeLists.txt)
HOST_ENGINE := -Isynthengine3D/host/shims -Isynthengine3D/host -Isynthengine3D/include

.PHONY: check shots textures icons chambers_c host_shot host_movie movie host_tas tas host_review
# The built-in chambers, as C (the badge build makes its own copy, CMakeLists.txt).
chambers_c:
	python3 tools/embed_chambers.py chambers $(BUILD)/generated/chambers_builtin.c

# The game's own render.c through a small software rasterizer: `make shots`
# draws with it, and `make check` runs its self-test (portal views not
# painted over, no frame left with holes).
host_shot: chambers_c
	mkdir -p $(BUILD)
	$(HOSTCC) -O2 -Wall -Wextra $(HOST_ENGINE) -Imain $(ENGINE_DEFS) tests/host_shot.c main/render.c $(HOST_SRCS) -lm -o $(BUILD)/host_shot

check: chambers_c host_shot
	mkdir -p $(BUILD)
	BUILD="$(BUILD)" $(BUILD)/host_shot selftest
	$(HOSTCC) -O1 -g -Wall -Wextra -Werror -Imain tests/host_test.c $(HOST_SRCS) -lm -o $(BUILD)/host_test
	BUILD="$(BUILD)" $(BUILD)/host_test
	$(HOSTCC) -O1 -g -Wall -Wextra -Werror -Imain tests/host_review.c $(HOST_SRCS) -lm -o $(BUILD)/host_review
	$(BUILD)/host_review selftest
	# input.c against the badge's own headers (after the system's, so they
	# shadow nothing) and tests/shims for the ESP-IDF bits those want.
	$(HOSTCC) -O1 -g -Wall -Wextra -Werror -Itests/shims $(HOST_ENGINE) -Imain -idirafter include tests/host_input.c -lm -o $(BUILD)/host_input
	$(BUILD)/host_input
	# SAM (third_party/sam) speaking every built-in story: GLaDOS's voice.
	$(HOSTCC) -O1 -g -Imain -fcommon -w -c third_party/sam/sam.c -o $(BUILD)/sam_sam.o
	$(HOSTCC) -O1 -g -fcommon -w -c third_party/sam/render.c -o $(BUILD)/sam_render.o
	$(HOSTCC) -O1 -g -fcommon -w -c third_party/sam/reciter.c -o $(BUILD)/sam_reciter.o
	$(HOSTCC) -O1 -g -fcommon -w -c third_party/sam/debug.c -o $(BUILD)/sam_debug.o
	$(HOSTCC) -O1 -g -Wall -Wextra -Werror -Imain -fcommon tests/host_speech.c main/speech.c $(HOST_SRCS) $(BUILD)/sam_*.o -lm -o $(BUILD)/host_speech
	$(BUILD)/host_speech
	tests/p4port_test.sh

# Needs Pillow for the PNGs. Each run's PPMs are converted and removed.
shots: host_shot
	mkdir -p $(BUILD)/shots
	rm -f $(BUILD)/shots/*.ppm
	BUILD="$(BUILD)" $(BUILD)/host_shot
	python3 -c "import glob, os; from PIL import Image; [(Image.open(f).save(f[:-4] + '.png'), os.remove(f)) for f in glob.glob('$(BUILD)/shots/*.ppm')]"

# A chamber's solution as a film with sound (tools/make_movie.py; needs
# Pillow and ffmpeg). DEMO is a chamber id; CHAMBERS a directory of
# chamber files, as on the SD card. make movie DEMO=12-redirection
DEMO     ?= 12-redirection
MOVIE    ?= $(BUILD)/$(DEMO).mp4
host_movie: chambers_c
	mkdir -p $(BUILD)/movie
	$(HOSTCC) -O2 -fcommon -w -Imain -Itests/movie_shims $(HOST_ENGINE) -Itests/shims $(ENGINE_DEFS) \
		tests/host_movie.c main/render.c main/sound.c main/speech.c $(HOST_SRCS) \
		synthengine3D/src/music_procedural.c synthengine3D/src/se_voice.c synthengine3D/src/audio_dsp.c \
		third_party/sam/sam.c third_party/sam/render.c third_party/sam/reciter.c third_party/sam/debug.c \
		-lm -o $(BUILD)/movie/host_movie

movie: host_movie
	python3 tools/make_movie.py $(DEMO) --mp4 $(MOVIE) $(if $(CHAMBERS),--chambers $(CHAMBERS)) $(if $(GIF),--gif $(GIF))

# The tool-assisted runs in tas/: each chamber's fastest known route, played
# at 50 steps a second, against its solution (tools/tas.py). TASFILM=x.mp4
# films them, with a timer.
host_tas: chambers_c
	mkdir -p $(BUILD)
	$(HOSTCC) -O2 -Wall -Wextra -Werror -Imain tests/host_tas.c $(HOST_SRCS) -lm -o $(BUILD)/host_tas

# Play a chamber file's solution and cheese routes, judged as a story
# round judges them (review.h).
host_review: chambers_c
	mkdir -p $(BUILD)
	$(HOSTCC) -O2 -Wall -Wextra -Werror -Imain tests/host_review.c $(HOST_SRCS) -lm -o $(BUILD)/host_review

tas: host_tas $(if $(TASFILM),host_movie)
	python3 tools/tas.py $(if $(TASFILM),--film $(TASFILM))

# The story packs in dlc/ (tools/dlc.py, main/pack.h): `make dlc` checks
# every chamber is solved and builds the packs for the card in build/dlc;
# `make dlc-upload` puts them in /sd/portals/dlc/, with their solutions as
# recordings; `make dlc-movies` films each whole story (build/dlc/<pack>.mp4).
.PHONY: dlc dlc-upload dlc-movies dlc-zip story-film
dlc: host_tas host_review
	python3 tools/dlc.py check
	python3 tools/dlc.py card

# A desk story's day as a film: make story-film PACK=human-in-the-loop
PACK ?= human-in-the-loop
story-film: host_review host_movie
	mkdir -p $(BUILD)/dlc
	python3 tools/story_film.py dlc/$(PACK) --mp4 $(BUILD)/dlc/$(PACK)-story.mp4

# Each card folder as a zip, to unzip into /sd/portals/dlc/: for a release.
dlc-zip: dlc
	python3 tools/dlc.py zip

dlc-movies: host_movie
	python3 tools/dlc.py films

textures:
	python3 tools/make_textures.py

icons:
	python3 tools/make_icons.py

# SynthEngine3D, the 3D engine: not part of the template, added per app as a
# git submodule (CMakeLists.txt builds it when synthengine3D/ is there, and is
# untouched otherwise). Run this once in a new app that wants 3D; afterwards a
# fresh clone needs `git clone --recursive` or `git submodule update --init`.
ENGINE_URL ?= git@github.com:nullislandspace/synthengine3D.git
ENGINE_REF ?= main

.PHONY: engine
engine:
	if test -d synthengine3D; then \
	  echo "synthengine3D/ is already there -- 'git submodule update --remote synthengine3D' updates it"; exit 1; \
	fi
	git submodule add -b $(ENGINE_REF) $(ENGINE_URL) synthengine3D
	git submodule update --init --recursive synthengine3D
	@echo "=== SynthEngine3D added. Commit .gitmodules and synthengine3D, then #include \"synthengine3d.h\" ==="

# Device tests (main/testkit/devtest.h, tools/testrun.py): the scripted
# demos in main/demo.c -- c1walk, c1loop, and every chamber's solution by
# its id (03-fling, ...) -- on the badge.
#
#   make cycle   TEST="perf scene=c1walk secs=6"           build, install, run, test
#   make cycle   TEST="shots scene=c1walk ms=1500,2900"    shots at exact instants
#   make cycle   TEST="..." TESTFLAGS=--fetch               ... and download the PNGs (slow)
#   make testrefs    TEST="shots ..."                       store the shot hashes as references
#   make testcompare TEST="shots ..."                       compare against them
#   make testrun TEST="..."                                 the app is already running, in debug mode
#   make recover                                            after a crash or a hang
#
# install / run need BadgeLink mode; the console needs debug mode. Starting
# the app switches the badge to debug mode by itself, so `cycle` runs it and
# then waits for the P4's console to appear (tools/p4port.sh, which learns
# the P4's USB port during install). The app runs the test and goes back to
# the launcher by itself.
TEST ?=
TESTFLAGS ?=

.PHONY: testrun cycle testrefs testcompare recover wait_console
wait_console:
	for i in $$(seq 1 30); do $(P4_FIND) >/dev/null 2>&1 && exit 0; sleep 1; done; \
	$(P4_FIND) >/dev/null || true; \
	echo "the P4's console did not appear in 30 s"; exit 1

testrun:
	$(CONSOLE_SH) BADGELINKPORT="$(BADGELINKPORT)" $(PY) -u tools/testrun.py --port "$$console" $(TESTFLAGS) -- $(TEST)

cycle: build install run
	sleep 4
	$(MAKE) wait_console
	$(MAKE) testrun

testrefs:
	$(MAKE) cycle TESTFLAGS="--capture-refs"

testcompare:
	$(MAKE) cycle TESTFLAGS="--compare"

recover:
	$(CONSOLE_SH) $(PY) tools/recover.py --port "$$console"

# Badgelink
.PHONY: badgelink
badgelink:
	rm -rf badgelink
	#git clone https://github.com/badgeteam/esp32-component-badgelink.git badgelink
	git clone https:///github.com/nullislandspace/esp32-component-badgelink.git badgelink
	cd badgelink/tools; ./install.sh

# The badgelink connection: --tcp for host:port (a TCP proxy), --port for a
# serial device -- whose /dev/serial/by-path name has colons in it too.
BADGELINK_CONN := $(if $(BADGELINKPORT),$(if $(shell echo '$(BADGELINKPORT)' | grep -E '^[^/]+:[0-9]+$$'),--tcp,--port) $(BADGELINKPORT))

.PHONY: install
install: build
	@echo "=== Installing to device ==="
	# BadgeLink mode: the moment to learn the P4's USB port (tools/p4port.sh).
	tools/p4port.sh --learn || true
	@echo "Creating directory $(APP_INSTALL_PATH)..."
	cd badgelink/tools; ./badgelink.sh $(BADGELINK_CONN) fs mkdir $(APP_INSTALL_PATH) || true
	@echo "Uploading metadata.json..."
	cd badgelink/tools; ./badgelink.sh $(BADGELINK_CONN) fs upload $(APP_INSTALL_PATH)/metadata.json ../../metadata/metadata.json
	@echo "Uploading icon16.png..."
	cd badgelink/tools; ./badgelink.sh $(BADGELINK_CONN) fs upload $(APP_INSTALL_PATH)/icon16.png ../../metadata/icon16.png
	@echo "Uploading icon32.png..."
	cd badgelink/tools; ./badgelink.sh $(BADGELINK_CONN) fs upload $(APP_INSTALL_PATH)/icon32.png ../../metadata/icon32.png
	@echo "Uploading icon64.png..."
	cd badgelink/tools; ./badgelink.sh $(BADGELINK_CONN) fs upload $(APP_INSTALL_PATH)/icon64.png ../../metadata/icon64.png
	@echo "Uploading app.so..."
	cd badgelink/tools; ./badgelink.sh $(BADGELINK_CONN) fs upload $(APP_INSTALL_PATH)/app.so $(abspath $(BUILD))/app.so
	@echo "Uploading textures..."
	cd badgelink/tools; ./badgelink.sh $(BADGELINK_CONN) fs mkdir $(APP_INSTALL_PATH)/textures || true
	for t in $(TEXTURES); do \
	  (cd badgelink/tools; ./badgelink.sh $(BADGELINK_CONN) fs upload $(APP_INSTALL_PATH)/textures/$$t ../../textures/$$t) || exit 1; \
	done
	@echo "=== Installation complete ==="

GRACELOADER_SLUG ?= at.cavac.graceloader

# The TAS onto the card as a recording, for Esc -> Watch a recording; and
# the times the badge played it in, back (tas/README.md).
.PHONY: tas-upload tas-result
tas-upload: tas
	cd badgelink/tools; ./badgelink.sh $(BADGELINK_CONN) fs mkdir /sd/portals || true
	cd badgelink/tools; ./badgelink.sh $(BADGELINK_CONN) fs mkdir /sd/portals/recordings || true
	cd badgelink/tools; ./badgelink.sh $(BADGELINK_CONN) fs upload /sd/portals/recordings/tas.txt $(abspath $(BUILD))/tas-recording.txt

dlc-upload: dlc
	cd badgelink/tools; ./badgelink.sh $(BADGELINK_CONN) fs mkdir /sd/portals || true
	cd badgelink/tools; ./badgelink.sh $(BADGELINK_CONN) fs mkdir /sd/portals/dlc || true
	cd badgelink/tools; ./badgelink.sh $(BADGELINK_CONN) fs mkdir /sd/portals/recordings || true
	# Each upload tried three times: badgelink times out now and then.
	cd badgelink/tools; up() { for t in 1 2 3; do ./badgelink.sh $(BADGELINK_CONN) fs upload "$$1" "$$2" && return 0; sleep 1; done; return 1; }; \
	  for s in $(abspath dlc)/*/; do p=$$(basename $$s); d=$(abspath $(BUILD))/dlc/$$p; \
	  ./badgelink.sh $(BADGELINK_CONN) fs mkdir /sd/portals/dlc/$$p || true; \
	  for sub in $$(cd $$d && find . -mindepth 1 -type d | sort); do \
	    ./badgelink.sh $(BADGELINK_CONN) fs mkdir /sd/portals/dlc/$$p/$${sub#./} || true; done; \
	  for f in $$(cd $$d && find . -type f -name '*.txt' | sort); do \
	    up /sd/portals/dlc/$$p/$${f#./} $$d/$${f#./} || exit 1; done; done

tas-result:
	cd badgelink/tools; ./badgelink.sh $(BADGELINK_CONN) fs download /sd/portals/tas-times.txt $(abspath $(BUILD))/tas-times.txt
	cat $(BUILD)/tas-times.txt

.PHONY: run
run:
	cd badgelink/tools; ./badgelink.sh $(BADGELINK_CONN) start $(GRACELOADER_SLUG) $(APP_INSTALL_PATH)/app.so

# USB mode switching
#
# The device's USB peripheral is in one of two modes: BadgeLink (USB_DEVICE),
# which is what install / run need, or flash-and-monitor (USB_DEBUG). An
# `install` that fails to reach the device usually means the launcher left it
# in debug mode -- `make mode_badgelink` puts it back.
#
# Ask the firmware (in USB_DEBUG mode) to switch its USB into BadgeLink mode
# by sending the token "BADGELINK\n" on the USB-serial/JTAG peripheral. The
# launcher listens for it (see ../tanmatsu-launcher/main/usb_device.c), so
# this only works against firmware that implements the listener.
#
# The console is the P4's (tools/p4port.sh), or P4_CONSOLE: the P4's tty, or
# an rfc2217:// URL pointing at ../tanmatsu-badgefs/rfc2217proxy when the
# device is forwarded over the network.
.PHONY: mode_badgelink
mode_badgelink:
	$(CONSOLE_SH) $(PY) -c "import serial, sys; s=serial.serial_for_url(sys.argv[1], timeout=1, do_not_open=True); s.open(); s.rts=False; s.dtr=False; s.write(b'BADGELINK\n'); s.flush(); sys.stdout.write(s.read(128).decode(errors='replace')); s.close()" "$$console"

# The other direction: ask the firmware (in BadgeLink mode) to switch its USB
# back to flash/monitor mode, through BadgeLink's own `mode` command. Uses the
# badgelink checkout that `make badgelink` creates, and the same BADGELINK_CONN
# as install / run, so it follows a networked device too.
BADGELINK_SH := badgelink/tools/badgelink.sh

.PHONY: mode_debug
mode_debug:
	if [ ! -x "$(BADGELINK_SH)" ]; then \
	  echo "$(BADGELINK_SH) not found -- run 'make badgelink' first"; \
	  exit 1; \
	fi; \
	echo "Using $(BADGELINK_SH)"; \
	"$(BADGELINK_SH)" $(BADGELINK_CONN) mode debug

APP_REPO_PATH ?= ../app-repository/$(APP_SLUG_NAME)

.PHONY: apprepo
apprepo: build
	@echo "=== Updating app repository ==="
	mkdir -p $(APP_REPO_PATH)
	cp metadata/metadata.json $(APP_REPO_PATH)/metadata.json
	cp metadata/icon16.png $(APP_REPO_PATH)/icon16.png
	cp metadata/icon32.png $(APP_REPO_PATH)/icon32.png
	cp metadata/icon64.png $(APP_REPO_PATH)/icon64.png
	cp LICENSE $(APP_REPO_PATH)/LICENSE
	cp $(BUILD)/app.so $(APP_REPO_PATH)/app.so
	mkdir -p $(APP_REPO_PATH)/textures
	cp textures/*.png $(APP_REPO_PATH)/textures/
	@echo "=== App repository updated at $(APP_REPO_PATH) ==="

# Preparation

.PHONY: prepare
prepare: sdk

.PHONY: sdk
sdk:
	if test -d "$(IDF_PATH)"; then echo -e "ESP-IDF target folder exists!\r\nPlease remove the folder or un-set the environment variable."; exit 1; fi
	if test -d "$(IDF_TOOLS_PATH)"; then echo -e "ESP-IDF tools target folder exists!\r\nPlease remove the folder or un-set the environment variable."; exit 1; fi
	git clone --recursive --branch "$(IDF_VERSION)" https://github.com/espressif/esp-idf.git "$(IDF_PATH)" --depth=1 --shallow-submodules
	cd "$(IDF_PATH)"; git submodule update --init --recursive
	cd "$(IDF_PATH)"; bash install.sh all

.PHONY: reinstallsdk
reinstallsdk:
	cd "$(IDF_PATH)"; bash install.sh all

.PHONY: removesdk
removesdk:
	rm -rf "$(IDF_PATH)"
	rm -rf "$(IDF_TOOLS_PATH)"

.PHONY: refreshsdk
refreshsdk: removesdk sdk

# Verification

.PHONY: verify
verify: build
	@echo "=== Verifying symbols ==="
	@MISSING=$$(comm -23 \
	  <(nm -D --undefined-only $(BUILD)/app.so | awk '{print $$2}' | sort -u) \
	  <(nm -D --defined-only fakelib/liball.so | awk '{print $$3}' | sort -u)); \
	if [ -n "$$MISSING" ]; then \
	  echo "ERROR: app.so requires symbols not in fakelib:"; \
	  echo "$$MISSING"; \
	  exit 1; \
	else \
	  echo "All symbols satisfied."; \
	fi

# Cleaning

.PHONY: clean
clean:
	rm -rf $(BUILD)

.PHONY: fullclean
fullclean: clean

# Formatting

.PHONY: format
format:
	# Not main/testkit/ or crt0.c: they come from the template, and restyling
	# them would make every merge from upstream conflict.
	find main/ tests/ -path main/testkit -prune -o \( -iname '*.h' -o -iname '*.c' \) ! -name crt0.c -print | xargs clang-format -i
