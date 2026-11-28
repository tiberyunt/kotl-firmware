.DEFAULT_GOAL := kotl-efi
CROSS ?= $(CURDIR)/toolchain/bin/alpha-linux-gnu-
CC = $(CROSS)gcc
LD = $(CROSS)ld
PYTHON ?= python3
QEMU ?= $(abspath $(KOTL_QEMU))
CFLAGS = -std=gnu11 -O2 -g -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables -mno-fp-regs -mcpu=ev67 -mlarge-data -G0 -Iinclude -Ifirmware
FW_OBJS = build/start.o build/platform.o build/support.o build/events.o build/variables.o build/services.o build/pe.o build/image-jump.o build/main.o build/protocol.o build/device-path.o build/block.o build/ata.o build/fat.o build/demo.o
.PHONY: all run shell test clean toolchain
all: build/alpha-efi.elf build/BOOTAXP64.EFI build/HELLOAXP64.EFI
build:
	mkdir -p build
build/%.o: firmware/%.c include/efi-device-path.h firmware/device-path.h include/efi.h include/efi-storage.h firmware/platform.h firmware/services.h firmware/storage.h | build
	$(CC) $(CFLAGS) -c $< -o $@
build/start.o: firmware/start.S | build
	$(CC) $(CFLAGS) -c $< -o $@
build/app-entry.o: apps/entry.S | build
	$(CC) $(CFLAGS) -c $< -o $@
build/app-%.o: apps/%.c include/efi.h include/efi-storage.h | build
	$(CC) $(CFLAGS) -c $< -o $@
build/selftest.elf: build/app-entry.o build/app-lib.o build/app-selftest.o apps/link.ld Makefile
	$(LD) --emit-relocs -T apps/link.ld -o $@ build/app-entry.o build/app-lib.o build/app-selftest.o
build/hello.elf: build/app-entry.o build/app-lib.o build/app-hello.o apps/link.ld Makefile
	$(LD) --emit-relocs -T apps/link.ld -o $@ build/app-entry.o build/app-lib.o build/app-hello.o
build/HELLOAXP64.EFI: build/hello.elf scripts/elf-to-pe.py
	$(PYTHON) scripts/elf-to-pe.py $< $@ --relocatable
build/BOOTAXP64.EFI: build/selftest.elf scripts/elf-to-pe.py
	$(PYTHON) scripts/elf-to-pe.py $< $@ --relocatable
build/demo.S: build/BOOTAXP64.EFI
	$(PYTHON) -c 'from pathlib import Path; Path("$@").write_text(".section .rodata\n.balign 8\n.global demo_pe_start, demo_pe_end\ndemo_pe_start:\n.incbin \"build/BOOTAXP64.EFI\"\ndemo_pe_end:\n")'
build/demo.o: build/demo.S
	$(CC) $(CFLAGS) -c $< -o $@
build/alpha-efi.elf: $(FW_OBJS) firmware/link.ld
	$(LD) -T firmware/link.ld -o $@ $(FW_OBJS)
run: all
	$(QEMU) -M clipper -cpu ev67 -m 128M -smp 1 -nographic -nic none -kernel build/alpha-efi.elf
shell: all
	$(QEMU) -M clipper -cpu ev67 -m 128M -smp 1 -nographic -nic none -kernel build/alpha-efi.elf -append shell
test: all
	$(PYTHON) tests/smoke.py --qemu $(QEMU)
clean:
	rm -f build/*.o build/*.elf build/*.EFI build/demo.S build/serial-*.log
toolchain:
	./scripts/fetch-sources.sh
	./scripts/build-toolchain.sh

build/alpha-esp.img: build/BOOTAXP64.EFI scripts/make-esp.py
	$(PYTHON) scripts/make-esp.py $< $@
.PHONY: disk run-disk
disk: build/alpha-esp.img
run-disk: all disk
	$(QEMU) -M clipper -cpu ev67 -m 128M -smp 1 -nographic -nic none -kernel build/alpha-efi.elf -append disk -drive if=ide,index=0,format=raw,file=build/alpha-esp.img,snapshot=on

KOTL_QEMU ?= ../kotl-qemu/build/qemu-kotl/qemu-system-alpha
.PHONY: qemu-kotl
qemu-kotl:
	$(PYTHON) scripts/use-kotl-qemu.py "$(KOTL_QEMU)"

.PHONY: kotl-efi run-kotl-efi shell-kotl-efi test-kotl-efi
kotl-efi:
	$(MAKE) -C firmware/kotl
run-kotl-efi: qemu-kotl kotl-efi
	build/qemu-kotl/qemu-system-alpha -M kotl-ev9,part=z1-lite,settings-file=$(KOTL_SETTINGS),variables-file=$(KOTL_VARIABLES) -m 128M -smp 8,sockets=1,cores=4,threads=2 -nographic -nic none -bios build/kotl-efi/kotl-efi-flash.bin -drive if=none,id=boot,format=raw,file=build/kotl-efi/esp.img,snapshot=on -device nvme,drive=boot,serial=KOTLBOOT
shell-kotl-efi: qemu-kotl kotl-efi
	build/qemu-kotl/qemu-system-alpha -M kotl-ev9,firmware-args=shell,settings-file=$(KOTL_SETTINGS),variables-file=$(KOTL_VARIABLES) -m 128M -nographic -nic none -bios build/kotl-efi/kotl-efi-flash.bin -drive if=none,id=boot,format=raw,file=build/kotl-efi/esp.img,snapshot=on -device nvme,drive=boot,serial=KOTLBOOT
test-kotl-efi: qemu-kotl kotl-efi
	$(PYTHON) -u tests/kotl_efi.py

.PHONY: run-kotl-efi-display test-kotl-ui
# Override with e.g. DISPLAY_BACKEND=none and add -vnc 127.0.0.1:1 to the QEMU command for a remote viewer.
DISPLAY_BACKEND ?= cocoa
KOTL_DISPLAY_WIDTH ?= 1024
KOTL_DISPLAY_HEIGHT ?= 768
KOTL_DISPLAY_DEVICE = bochs-display,addr=2,vgamem=32M,refresh_rate=50000,xres=$(KOTL_DISPLAY_WIDTH),yres=$(KOTL_DISPLAY_HEIGHT)
KOTL_SETTINGS ?= build/kotl-efi/settings.bin
KOTL_VARIABLES ?= build/kotl-efi/variables.bin
run-kotl-efi-display: qemu-kotl kotl-efi
	build/qemu-kotl/qemu-system-alpha -M kotl-ev9,part=z1-lite,settings-file=$(KOTL_SETTINGS),variables-file=$(KOTL_VARIABLES) -m 128M -smp 8,sockets=1,cores=4,threads=2 -display $(DISPLAY_BACKEND) -serial stdio -monitor none -nic none -bios build/kotl-efi/kotl-efi-flash.bin -device $(KOTL_DISPLAY_DEVICE) -drive if=none,id=boot,format=raw,file=build/kotl-efi/esp.img,snapshot=on -device nvme,drive=boot,serial=KOTLBOOT,addr=1
test-kotl-ui: qemu-kotl kotl-efi
	$(PYTHON) -u tests/kotl_ui.py

.PHONY: test-kotl-setup
test-kotl-setup: qemu-kotl kotl-efi
	$(PYTHON) -u tests/kotl_setup.py

.PHONY: event-apps
event-apps: build/EVENTTIMEOUT.EFI build/EVENTSERIAL.EFI build/EVENTKEY.EFI build/EVENTPAIR.EFI build/EVENTLEAK.EFI
build/event-TIMEOUT.o: apps/event-input.c include/efi.h | build
	$(CC) $(CFLAGS) -DINPUT_KIND=0 -c $< -o $@
build/event-TIMEOUT.elf: build/app-entry.o build/app-lib.o build/event-TIMEOUT.o apps/link.ld Makefile
	$(LD) --emit-relocs -T apps/link.ld -o $@ $(filter %.o,$^)
build/EVENTTIMEOUT.EFI: build/event-TIMEOUT.elf scripts/elf-to-pe.py
	python3 scripts/elf-to-pe.py $< $@ --relocatable
build/event-SERIAL.o: apps/event-input.c include/efi.h | build
	$(CC) $(CFLAGS) -DINPUT_KIND=1 -c $< -o $@
build/event-SERIAL.elf: build/app-entry.o build/app-lib.o build/event-SERIAL.o apps/link.ld Makefile
	$(LD) --emit-relocs -T apps/link.ld -o $@ $(filter %.o,$^)
build/EVENTSERIAL.EFI: build/event-SERIAL.elf scripts/elf-to-pe.py
	python3 scripts/elf-to-pe.py $< $@ --relocatable
build/event-KEY.o: apps/event-input.c include/efi.h | build
	$(CC) $(CFLAGS) -DINPUT_KIND=2 -c $< -o $@
build/event-KEY.elf: build/app-entry.o build/app-lib.o build/event-KEY.o apps/link.ld Makefile
	$(LD) --emit-relocs -T apps/link.ld -o $@ $(filter %.o,$^)
build/EVENTKEY.EFI: build/event-KEY.elf scripts/elf-to-pe.py
	python3 scripts/elf-to-pe.py $< $@ --relocatable
build/event-PAIR.o: apps/event-input.c include/efi.h | build
	$(CC) $(CFLAGS) -DINPUT_KIND=3 -c $< -o $@
build/event-PAIR.elf: build/app-entry.o build/app-lib.o build/event-PAIR.o apps/link.ld Makefile
	$(LD) --emit-relocs -T apps/link.ld -o $@ $(filter %.o,$^)
build/EVENTPAIR.EFI: build/event-PAIR.elf scripts/elf-to-pe.py
	python3 scripts/elf-to-pe.py $< $@ --relocatable
build/event-LEAK.o: apps/event-input.c include/efi.h | build
	$(CC) $(CFLAGS) -DINPUT_KIND=4 -c $< -o $@
build/event-LEAK.elf: build/app-entry.o build/app-lib.o build/event-LEAK.o apps/link.ld Makefile
	$(LD) --emit-relocs -T apps/link.ld -o $@ $(filter %.o,$^)
build/EVENTLEAK.EFI: build/event-LEAK.elf scripts/elf-to-pe.py
	python3 scripts/elf-to-pe.py $< $@ --relocatable

.PHONY: variable-apps
variable-apps: build/VARWRITE.EFI build/VARREAD.EFI build/VARDELETE.EFI build/VARFAIL.EFI build/VAREMPTY.EFI build/VARROLLBACK.EFI
build/variable-WRITE.o: apps/variable-test.c include/efi.h include/efi-storage.h | build
	$(CC) $(CFLAGS) -DVARIABLE_KIND=0 -c $< -o $@
build/variable-WRITE.elf: build/app-entry.o build/app-lib.o build/variable-WRITE.o apps/link.ld Makefile
	$(LD) --emit-relocs -T apps/link.ld -o $@ $(filter %.o,$^)
build/VARWRITE.EFI: build/variable-WRITE.elf scripts/elf-to-pe.py
	python3 scripts/elf-to-pe.py $< $@ --relocatable
build/variable-READ.o: apps/variable-test.c include/efi.h include/efi-storage.h | build
	$(CC) $(CFLAGS) -DVARIABLE_KIND=1 -c $< -o $@
build/variable-READ.elf: build/app-entry.o build/app-lib.o build/variable-READ.o apps/link.ld Makefile
	$(LD) --emit-relocs -T apps/link.ld -o $@ $(filter %.o,$^)
build/VARREAD.EFI: build/variable-READ.elf scripts/elf-to-pe.py
	python3 scripts/elf-to-pe.py $< $@ --relocatable
build/variable-DELETE.o: apps/variable-test.c include/efi.h include/efi-storage.h | build
	$(CC) $(CFLAGS) -DVARIABLE_KIND=2 -c $< -o $@
build/variable-DELETE.elf: build/app-entry.o build/app-lib.o build/variable-DELETE.o apps/link.ld Makefile
	$(LD) --emit-relocs -T apps/link.ld -o $@ $(filter %.o,$^)
build/VARDELETE.EFI: build/variable-DELETE.elf scripts/elf-to-pe.py
	python3 scripts/elf-to-pe.py $< $@ --relocatable
build/variable-FAIL.o: apps/variable-test.c include/efi.h include/efi-storage.h | build
	$(CC) $(CFLAGS) -DVARIABLE_KIND=3 -c $< -o $@
build/variable-FAIL.elf: build/app-entry.o build/app-lib.o build/variable-FAIL.o apps/link.ld Makefile
	$(LD) --emit-relocs -T apps/link.ld -o $@ $(filter %.o,$^)
build/VARFAIL.EFI: build/variable-FAIL.elf scripts/elf-to-pe.py
	python3 scripts/elf-to-pe.py $< $@ --relocatable
build/variable-EMPTY.o: apps/variable-test.c include/efi.h include/efi-storage.h | build
	$(CC) $(CFLAGS) -DVARIABLE_KIND=4 -c $< -o $@
build/variable-EMPTY.elf: build/app-entry.o build/app-lib.o build/variable-EMPTY.o apps/link.ld Makefile
	$(LD) --emit-relocs -T apps/link.ld -o $@ $(filter %.o,$^)
build/VAREMPTY.EFI: build/variable-EMPTY.elf scripts/elf-to-pe.py
	python3 scripts/elf-to-pe.py $< $@ --relocatable
build/variable-ROLLBACK.o: apps/variable-test.c include/efi.h include/efi-storage.h | build
	$(CC) $(CFLAGS) -DVARIABLE_KIND=5 -c $< -o $@
build/variable-ROLLBACK.elf: build/app-entry.o build/app-lib.o build/variable-ROLLBACK.o apps/link.ld Makefile
	$(LD) --emit-relocs -T apps/link.ld -o $@ $(filter %.o,$^)
build/VARROLLBACK.EFI: build/variable-ROLLBACK.elf scripts/elf-to-pe.py
	python3 scripts/elf-to-pe.py $< $@ --relocatable

.PHONY: test-events test-variables
test-events: all event-apps qemu-kotl kotl-efi
	$(MAKE) -C firmware/kotl event-apps
	$(PYTHON) -u tests/events.py
test-variables: all variable-apps qemu-kotl kotl-efi
	$(MAKE) -C firmware/kotl variable-apps
	$(PYTHON) -u tests/variables.py

.PHONY: relocation-apps
relocation-apps: build/RELOCPARENT.EFI build/RELOCCHILD.EFI
build/relocate-cross.o: apps/relocate-cross.S | build
	$(CC) $(CFLAGS) -c $< -o $@
build/relocate-child.elf: build/app-entry.o build/app-lib.o build/app-relocate-child.o build/relocate-cross.o apps/link.ld Makefile
	$(LD) --emit-relocs -T apps/link.ld -o $@ $(filter %.o,$^)
build/RELOCCHILD.EFI: build/relocate-child.elf scripts/elf-to-pe.py
	python3 scripts/elf-to-pe.py $< $@ --relocatable
build/reloc-child-meta.h: build/relocate-child.elf scripts/relocation-fixture.py
	python3 scripts/relocation-fixture.py $< $@ 0xfffffc0001000000
build/reloc-blob.S: build/RELOCCHILD.EFI
	python3 -c 'from pathlib import Path; Path("$@").write_text(".section .rodata\n.balign 8\n.global reloc_child_start,reloc_child_end\nreloc_child_start:\n.incbin \"$(abspath $<)\"\nreloc_child_end:\n.section .note.GNU-stack,\"\",@progbits\n")'
build/reloc-blob.o: build/reloc-blob.S
	$(CC) $(CFLAGS) -c $< -o $@
build/relocate-parent.o: apps/relocate-parent.c build/reloc-child-meta.h include/efi.h
	$(CC) $(CFLAGS) -Ibuild -c $< -o $@
build/relocate-parent.elf: build/app-entry.o build/app-lib.o build/relocate-parent.o build/reloc-blob.o apps/link.ld Makefile
	$(LD) --emit-relocs -T apps/link.ld -o $@ $(filter %.o,$^)
build/RELOCPARENT.EFI: build/relocate-parent.elf scripts/elf-to-pe.py
	python3 scripts/elf-to-pe.py $< $@

.PHONY: test-relocations
test-relocations: all relocation-apps qemu-kotl kotl-efi
	$(MAKE) -C firmware/kotl relocation-apps
	$(PYTHON) -u tests/relocations.py


build/image-jump.o: firmware/image-jump.S
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: test-kotl-pal
test-kotl-pal: qemu-kotl kotl-efi
	$(MAKE) -C firmware/kotl pal-test-apps
	$(PYTHON) -u tests/kotl_pal.py

KOTL_PAL_MODEL ?= pal
.PHONY: run-kotl-pal
run-kotl-pal: qemu-kotl kotl-efi
	$(MAKE) -C firmware/kotl pal-test-disks
	build/qemu-kotl/qemu-system-alpha -M kotl-ev9 -m 128M -smp 4,sockets=1,cores=4,threads=1 -nographic -nic none -bios build/kotl-efi/kotl-efi-flash.bin -drive if=none,id=boot,format=raw,file=build/kotl-efi/pal-$(KOTL_PAL_MODEL)-esp.img,snapshot=on -device nvme,drive=boot,serial=KOTLPAL,addr=1
