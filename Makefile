ARCH := x86_64
INCDIR := headers
COMMON := glue/$(ARCH)/relocation_func.o glue/$(ARCH)/start_func.o
CFLAGS := -I. -I$(INCDIR) -I$(INCDIR)/$(ARCH) -DGNU_EFI_USE_MS_ABI \
          -fPIC -fshort-wchar -ffreestanding -fno-stack-protector \
          -maccumulate-outgoing-args -Wall -m64 -mno-red-zone -Werror
LDFLAGS := -T glue/$(ARCH)/elf_efi.lds -Bsymbolic -shared -nostdlib -znocombreloc

%.efi: %.so
	objcopy -j .text -j .sdata -j .data -j .dynamic -j .dynsym \
	      -j .rel -j .rela -j .reloc -S --target=efi-app-$(ARCH) $< $@

%.so: %.o $(COMMON)
	$(LD) $(LDFLAGS) -o $@ $^ $$($(CC) $(CFLAGS) -print-libgcc-file-name)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

initramfs:
	cd initramfs-contents && find . | cpio -o -H newc | gzip > ../initramfs

disk.qcow2: bootloader.efi initramfs
	@echo "Creating boot disk image..."
	@dd if=/dev/zero of=disk.img bs=1M count=256 status=none
	@mkfs.fat -F32 disk.img > /dev/null 2>&1
	@mkdir -p mnt
	@sudo mount -t vfat -o loop disk.img mnt
	@sudo mkdir -p mnt/EFI/BOOT
	@sudo cp bootloader.efi mnt/EFI/BOOT/BOOTX64.EFI
	@sudo cp vmlinuz mnt/vmlinuz
	@sudo cp initramfs mnt/initramfs
	@sudo cp modloop-lts mnt/modloop-lts
	@sudo cp -r apks mnt/apks
	@sudo umount mnt
	@rmdir mnt
	@qemu-img convert -f raw -O qcow2 disk.img disk.qcow2
	@rm disk.img
	@echo "Boot disk created: disk.qcow2"

data.qcow2:
	@echo "Creating data disk..."
	@qemu-img create -f qcow2 data.qcow2 10G
	@echo "Data disk created: data.qcow2"

run-bootloader: bootloader.efi initramfs
	@if [ ! -f disk.qcow2 ]; then $(MAKE) disk.qcow2; fi
	@if [ ! -f data.qcow2 ]; then $(MAKE) data.qcow2; fi
	@echo "Running QEMU..."
	@qemu-system-x86_64 \
	  -m 4G \
	  -hda disk.qcow2 \
	  -hdb data.qcow2 \
	  -drive if=pflash,format=raw,file=/usr/share/OVMF/OVMF_CODE.fd \
	  -serial stdio

clean:
	rm -f *.efi *.so *.o $(COMMON) disk.img initramfs
	@sudo umount mnt 2>/dev/null || true
	@rmdir mnt 2>/dev/null || true

clean-all: clean
	rm -f disk.qcow2 data.qcow2

.PHONY: clean clean-all initramfs run-bootloader
