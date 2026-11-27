ROOT := ../..
CROSS ?= $(abspath $(ROOT)/toolchain/bin/alpha-linux-gnu-)
OUT := $(ROOT)/build/kotl-efi
CC = $(CROSS)gcc
LD = $(CROSS)ld
CFLAGS = -std=gnu11 -O2 -g -Wall -Wextra -Werror -ffreestanding -fno-delete-null-pointer-checks -fno-builtin -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables -mno-fp-regs -mcpu=ev67 -mlarge-data -G0 -DKOTL_NATIVE -I$(ROOT)/include -I$(ROOT)/firmware -I.
COMMON = support events variables services protocol device-path boot-options pe image-jump block fat
OBJS = $(OUT)/start.o $(OUT)/division.o $(addprefix $(OUT)/,$(addsuffix .o,$(COMMON))) $(OUT)/platform.o $(OUT)/main.o $(OUT)/nvme.o $(OUT)/display.o $(OUT)/font.o $(OUT)/boot.o $(OUT)/settings.o $(OUT)/menu.o $(OUT)/trap.o $(OUT)/demo.o
.PHONY: all
all: $(OUT)/kotl-efi-flash.bin $(OUT)/esp.img $(OUT)/HELLOAXP64.EFI $(OUT)/RESETAXP64.EFI
$(OUT):
	mkdir -p $@
$(OUT)/%.o: ../%.c $(ROOT)/include/efi-device-path.h ../device-path.h ../platform.h ../services.h ../storage.h $(ROOT)/include/efi.h $(ROOT)/include/efi-storage.h $(ROOT)/include/efi-graphics.h kotl.h Makefile | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/platform.o $(OUT)/main.o $(OUT)/nvme.o $(OUT)/display.o $(OUT)/boot.o $(OUT)/settings.o $(OUT)/menu.o $(OUT)/fdt.o: $(OUT)/%.o: %.c kotl.h settings.h menu.h ../platform.h ../services.h ../storage.h $(ROOT)/include/efi.h $(ROOT)/include/efi-storage.h $(ROOT)/include/efi-graphics.h Makefile | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/%.o: %.S Makefile | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/division.o: ../division.S Makefile | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/start.o: ../start.S Makefile | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/app-entry.o: $(ROOT)/apps/entry.S Makefile | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/app-%.o: $(ROOT)/apps/%.c $(ROOT)/include/efi.h $(ROOT)/include/efi-storage.h $(ROOT)/include/efi-graphics.h kotl.h Makefile | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/selftest.elf: $(OUT)/app-entry.o $(OUT)/app-lib.o $(OUT)/app-selftest.o $(OUT)/fdt.o apps.ld Makefile
	$(LD) --emit-relocs -T apps.ld -o $@ $(filter %.o,$^)
$(OUT)/hello.elf: $(OUT)/app-entry.o $(OUT)/app-lib.o $(OUT)/app-hello.o apps.ld Makefile
	$(LD) --emit-relocs -T apps.ld -o $@ $(filter %.o,$^)
$(OUT)/BOOTAXP64.EFI: $(OUT)/selftest.elf $(ROOT)/scripts/elf-to-pe.py
	python3 $(ROOT)/scripts/elf-to-pe.py $< $@ --base 0x101000000 --relocatable
$(OUT)/HELLOAXP64.EFI: $(OUT)/hello.elf $(ROOT)/scripts/elf-to-pe.py
	python3 $(ROOT)/scripts/elf-to-pe.py $< $@ --base 0x101000000 --relocatable
$(OUT)/demo.S: $(OUT)/BOOTAXP64.EFI
	python3 -c 'from pathlib import Path; Path("$@").write_text(".section .rodata\n.balign 8\n.global demo_pe_start,demo_pe_end\ndemo_pe_start:\n.incbin \"$(abspath $<)\"\ndemo_pe_end:\n.section .note.GNU-stack,\"\",@progbits\n")'
$(OUT)/demo.o: $(OUT)/demo.S
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/firmware.elf: $(OBJS) link.ld
	$(LD) -T link.ld -o $@ $(OBJS)
$(OUT)/firmware.bin: $(OUT)/firmware.elf
	$(CROSS)objcopy -O binary -j .text -j .rodata -j .data -j .sdata -j .got $< $@
$(OUT)/stage1-boot.o: stage1.S Makefile | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/stage1-firmware.o: stage1.c kotl.h Makefile | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/stage1-blob.S: $(OUT)/firmware.bin
	python3 -c 'from pathlib import Path; Path("$@").write_text(".section .rodata\n.balign 8\n.global native_firmware_start,native_firmware_end\nnative_firmware_start:\n.incbin \"$(abspath $<)\"\nnative_firmware_end:\n.section .note.GNU-stack,\"\",@progbits\n")'
$(OUT)/stage1-blob.o: $(OUT)/stage1-blob.S
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/stage1.elf: $(OUT)/stage1-boot.o $(OUT)/stage1-firmware.o $(OUT)/fdt.o $(OUT)/stage1-blob.o stage1.ld
	$(LD) -T stage1.ld -o $@ $(filter %.o,$^)
$(OUT)/kotl-efi-flash.bin: $(OUT)/stage1.elf
	$(CROSS)objcopy -O binary -j .text -j .rodata -j .data -j .got $< $@
$(OUT)/reset.elf: $(OUT)/app-entry.o $(OUT)/app-lib.o $(OUT)/app-reset.o apps.ld Makefile
	$(LD) --emit-relocs -T apps.ld -o $@ $(filter %.o,$^)
$(OUT)/RESETAXP64.EFI: $(OUT)/reset.elf $(ROOT)/scripts/elf-to-pe.py
	python3 $(ROOT)/scripts/elf-to-pe.py $< $@ --base 0x101000000 --relocatable

$(OUT)/esp.img: $(OUT)/BOOTAXP64.EFI $(ROOT)/scripts/make-esp.py
	python3 $(ROOT)/scripts/make-esp.py $< $@

$(OUT)/font.o: font.S font/ascii20.bin font/logo-gray.bin font/sharp-fonts.rle
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: event-apps
event-apps: $(OUT)/EVENTTIMEOUT.EFI $(OUT)/EVENTSERIAL.EFI $(OUT)/EVENTKEY.EFI $(OUT)/EVENTPAIR.EFI $(OUT)/EVENTLEAK.EFI
$(OUT)/event-TIMEOUT.o: $(ROOT)/apps/event-input.c $(ROOT)/include/efi.h | $(OUT)
	$(CC) $(CFLAGS) -DINPUT_KIND=0 -c $< -o $@
$(OUT)/event-TIMEOUT.elf: $(OUT)/app-entry.o $(OUT)/app-lib.o $(OUT)/event-TIMEOUT.o apps.ld Makefile
	$(LD) --emit-relocs -T apps.ld -o $@ $(filter %.o,$^)
$(OUT)/EVENTTIMEOUT.EFI: $(OUT)/event-TIMEOUT.elf $(ROOT)/scripts/elf-to-pe.py
	python3 $(ROOT)/scripts/elf-to-pe.py $< $@ --base 0x101000000 --relocatable
$(OUT)/event-SERIAL.o: $(ROOT)/apps/event-input.c $(ROOT)/include/efi.h | $(OUT)
	$(CC) $(CFLAGS) -DINPUT_KIND=1 -c $< -o $@
$(OUT)/event-SERIAL.elf: $(OUT)/app-entry.o $(OUT)/app-lib.o $(OUT)/event-SERIAL.o apps.ld Makefile
	$(LD) --emit-relocs -T apps.ld -o $@ $(filter %.o,$^)
$(OUT)/EVENTSERIAL.EFI: $(OUT)/event-SERIAL.elf $(ROOT)/scripts/elf-to-pe.py
	python3 $(ROOT)/scripts/elf-to-pe.py $< $@ --base 0x101000000 --relocatable
$(OUT)/event-KEY.o: $(ROOT)/apps/event-input.c $(ROOT)/include/efi.h | $(OUT)
	$(CC) $(CFLAGS) -DINPUT_KIND=2 -c $< -o $@
$(OUT)/event-KEY.elf: $(OUT)/app-entry.o $(OUT)/app-lib.o $(OUT)/event-KEY.o apps.ld Makefile
	$(LD) --emit-relocs -T apps.ld -o $@ $(filter %.o,$^)
$(OUT)/EVENTKEY.EFI: $(OUT)/event-KEY.elf $(ROOT)/scripts/elf-to-pe.py
	python3 $(ROOT)/scripts/elf-to-pe.py $< $@ --base 0x101000000 --relocatable
$(OUT)/event-PAIR.o: $(ROOT)/apps/event-input.c $(ROOT)/include/efi.h | $(OUT)
	$(CC) $(CFLAGS) -DINPUT_KIND=3 -c $< -o $@
$(OUT)/event-PAIR.elf: $(OUT)/app-entry.o $(OUT)/app-lib.o $(OUT)/event-PAIR.o apps.ld Makefile
	$(LD) --emit-relocs -T apps.ld -o $@ $(filter %.o,$^)
$(OUT)/EVENTPAIR.EFI: $(OUT)/event-PAIR.elf $(ROOT)/scripts/elf-to-pe.py
	python3 $(ROOT)/scripts/elf-to-pe.py $< $@ --base 0x101000000 --relocatable
$(OUT)/event-LEAK.o: $(ROOT)/apps/event-input.c $(ROOT)/include/efi.h | $(OUT)
	$(CC) $(CFLAGS) -DINPUT_KIND=4 -c $< -o $@
$(OUT)/event-LEAK.elf: $(OUT)/app-entry.o $(OUT)/app-lib.o $(OUT)/event-LEAK.o apps.ld Makefile
	$(LD) --emit-relocs -T apps.ld -o $@ $(filter %.o,$^)
$(OUT)/EVENTLEAK.EFI: $(OUT)/event-LEAK.elf $(ROOT)/scripts/elf-to-pe.py
	python3 $(ROOT)/scripts/elf-to-pe.py $< $@ --base 0x101000000 --relocatable

.PHONY: variable-apps
variable-apps: $(OUT)/VARWRITE.EFI $(OUT)/VARREAD.EFI $(OUT)/VARDELETE.EFI $(OUT)/VARFAIL.EFI $(OUT)/VAREMPTY.EFI $(OUT)/VARROLLBACK.EFI
$(OUT)/variable-WRITE.o: $(ROOT)/apps/variable-test.c $(ROOT)/include/efi.h $(ROOT)/include/efi-storage.h | $(OUT)
	$(CC) $(CFLAGS) -DVARIABLE_KIND=0 -c $< -o $@
$(OUT)/variable-WRITE.elf: $(OUT)/app-entry.o $(OUT)/app-lib.o $(OUT)/variable-WRITE.o apps.ld Makefile
	$(LD) --emit-relocs -T apps.ld -o $@ $(filter %.o,$^)
$(OUT)/VARWRITE.EFI: $(OUT)/variable-WRITE.elf $(ROOT)/scripts/elf-to-pe.py
	python3 $(ROOT)/scripts/elf-to-pe.py $< $@ --base 0x101000000 --relocatable
$(OUT)/variable-READ.o: $(ROOT)/apps/variable-test.c $(ROOT)/include/efi.h $(ROOT)/include/efi-storage.h | $(OUT)
	$(CC) $(CFLAGS) -DVARIABLE_KIND=1 -c $< -o $@
$(OUT)/variable-READ.elf: $(OUT)/app-entry.o $(OUT)/app-lib.o $(OUT)/variable-READ.o apps.ld Makefile
	$(LD) --emit-relocs -T apps.ld -o $@ $(filter %.o,$^)
$(OUT)/VARREAD.EFI: $(OUT)/variable-READ.elf $(ROOT)/scripts/elf-to-pe.py
	python3 $(ROOT)/scripts/elf-to-pe.py $< $@ --base 0x101000000 --relocatable
$(OUT)/variable-DELETE.o: $(ROOT)/apps/variable-test.c $(ROOT)/include/efi.h $(ROOT)/include/efi-storage.h | $(OUT)
	$(CC) $(CFLAGS) -DVARIABLE_KIND=2 -c $< -o $@
$(OUT)/variable-DELETE.elf: $(OUT)/app-entry.o $(OUT)/app-lib.o $(OUT)/variable-DELETE.o apps.ld Makefile
	$(LD) --emit-relocs -T apps.ld -o $@ $(filter %.o,$^)
$(OUT)/VARDELETE.EFI: $(OUT)/variable-DELETE.elf $(ROOT)/scripts/elf-to-pe.py
	python3 $(ROOT)/scripts/elf-to-pe.py $< $@ --base 0x101000000 --relocatable
$(OUT)/variable-FAIL.o: $(ROOT)/apps/variable-test.c $(ROOT)/include/efi.h $(ROOT)/include/efi-storage.h | $(OUT)
	$(CC) $(CFLAGS) -DVARIABLE_KIND=3 -c $< -o $@
$(OUT)/variable-FAIL.elf: $(OUT)/app-entry.o $(OUT)/app-lib.o $(OUT)/variable-FAIL.o apps.ld Makefile
	$(LD) --emit-relocs -T apps.ld -o $@ $(filter %.o,$^)
$(OUT)/VARFAIL.EFI: $(OUT)/variable-FAIL.elf $(ROOT)/scripts/elf-to-pe.py
	python3 $(ROOT)/scripts/elf-to-pe.py $< $@ --base 0x101000000 --relocatable
$(OUT)/variable-EMPTY.o: $(ROOT)/apps/variable-test.c $(ROOT)/include/efi.h $(ROOT)/include/efi-storage.h | $(OUT)
	$(CC) $(CFLAGS) -DVARIABLE_KIND=4 -c $< -o $@
$(OUT)/variable-EMPTY.elf: $(OUT)/app-entry.o $(OUT)/app-lib.o $(OUT)/variable-EMPTY.o apps.ld Makefile
	$(LD) --emit-relocs -T apps.ld -o $@ $(filter %.o,$^)
$(OUT)/VAREMPTY.EFI: $(OUT)/variable-EMPTY.elf $(ROOT)/scripts/elf-to-pe.py
	python3 $(ROOT)/scripts/elf-to-pe.py $< $@ --base 0x101000000 --relocatable
$(OUT)/variable-ROLLBACK.o: $(ROOT)/apps/variable-test.c $(ROOT)/include/efi.h $(ROOT)/include/efi-storage.h | $(OUT)
	$(CC) $(CFLAGS) -DVARIABLE_KIND=5 -c $< -o $@
$(OUT)/variable-ROLLBACK.elf: $(OUT)/app-entry.o $(OUT)/app-lib.o $(OUT)/variable-ROLLBACK.o apps.ld Makefile
	$(LD) --emit-relocs -T apps.ld -o $@ $(filter %.o,$^)
$(OUT)/VARROLLBACK.EFI: $(OUT)/variable-ROLLBACK.elf $(ROOT)/scripts/elf-to-pe.py
	python3 $(ROOT)/scripts/elf-to-pe.py $< $@ --base 0x101000000 --relocatable

.PHONY: relocation-apps
relocation-apps: $(OUT)/RELOCPARENT.EFI $(OUT)/RELOCCHILD.EFI
$(OUT)/relocate-cross.o: $(ROOT)/apps/relocate-cross.S | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/relocate-child.elf: $(OUT)/app-entry.o $(OUT)/app-lib.o $(OUT)/app-relocate-child.o $(OUT)/relocate-cross.o apps.ld Makefile
	$(LD) --emit-relocs -T apps.ld -o $@ $(filter %.o,$^)
$(OUT)/RELOCCHILD.EFI: $(OUT)/relocate-child.elf $(ROOT)/scripts/elf-to-pe.py
	python3 $(ROOT)/scripts/elf-to-pe.py $< $@ --base 0x101000000 --relocatable
$(OUT)/reloc-child-meta.h: $(OUT)/relocate-child.elf $(ROOT)/scripts/relocation-fixture.py
	python3 $(ROOT)/scripts/relocation-fixture.py $< $@ 0x101000000
$(OUT)/reloc-blob.S: $(OUT)/RELOCCHILD.EFI
	python3 -c 'from pathlib import Path; Path("$@").write_text(".section .rodata\n.balign 8\n.global reloc_child_start,reloc_child_end\nreloc_child_start:\n.incbin \"$(abspath $<)\"\nreloc_child_end:\n.section .note.GNU-stack,\"\",@progbits\n")'
$(OUT)/reloc-blob.o: $(OUT)/reloc-blob.S
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/relocate-parent.o: $(ROOT)/apps/relocate-parent.c $(OUT)/reloc-child-meta.h $(ROOT)/include/efi.h
	$(CC) $(CFLAGS) -I$(OUT) -c $< -o $@
$(OUT)/relocate-parent.elf: $(OUT)/app-entry.o $(OUT)/app-lib.o $(OUT)/relocate-parent.o $(OUT)/reloc-blob.o apps.ld Makefile
	$(LD) --emit-relocs -T apps.ld -o $@ $(filter %.o,$^)
$(OUT)/RELOCPARENT.EFI: $(OUT)/relocate-parent.elf $(ROOT)/scripts/elf-to-pe.py
	python3 $(ROOT)/scripts/elf-to-pe.py $< $@ --base 0x101000000

.PHONY: boot-option-apps
boot-option-apps: $(OUT)/BOOTCONFIG.EFI $(OUT)/BOOTCHECK.EFI
$(OUT)/boot-config.o: $(ROOT)/apps/boot-config.c $(ROOT)/include/efi-device-path.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/boot-check.o: $(ROOT)/apps/boot-check.c $(ROOT)/include/efi-device-path.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/boot-%.elf: $(OUT)/boot-%.o $(OUT)/app-entry.o $(OUT)/app-lib.o apps.ld
	$(LD) --emit-relocs -T apps.ld -o $@ $(OUT)/app-entry.o $(OUT)/app-lib.o $<
$(OUT)/BOOTCONFIG.EFI: $(OUT)/boot-config.elf $(ROOT)/scripts/elf-to-pe.py
	python3 $(ROOT)/scripts/elf-to-pe.py $< $@ --base 0x101000000 --relocatable
$(OUT)/BOOTCHECK.EFI: $(OUT)/boot-check.elf $(ROOT)/scripts/elf-to-pe.py
	python3 $(ROOT)/scripts/elf-to-pe.py $< $@ --base 0x101000000 --relocatable

$(OUT)/image-jump.o: ../image-jump.S Makefile | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
boot-option-apps: $(OUT)/EXITTEST.EFI
$(OUT)/exit-test.o: $(ROOT)/apps/exit-test.c $(ROOT)/include/efi.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/exit-test.elf: $(OUT)/exit-test.o $(OUT)/app-entry.o $(OUT)/app-lib.o apps.ld
	$(LD) --emit-relocs -T apps.ld -o $@ $(OUT)/app-entry.o $(OUT)/app-lib.o $<
$(OUT)/EXITTEST.EFI: $(OUT)/exit-test.elf $(ROOT)/scripts/elf-to-pe.py
	python3 $(ROOT)/scripts/elf-to-pe.py $< $@ --base 0x101000000 --relocatable
