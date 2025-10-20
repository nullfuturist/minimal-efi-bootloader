# minimal-efi-bootloader
Loads alpine linux with a short EFI bootloader written in C. Should work for other distros with minimal adaption.

Requires qemu-system and OVMF for EFI emulation. Can be used as system bootloader by placing the compiled .efi file as /EFI/BOOT/bootx64.efi on a fat-formatted disk and placing the other required files vmlinuz, modloop-lts, initramfs and apks directory in appropriate locations as demonstrated by the makefile. All use on own responsibility.

The EFI functionality is makes use of code from https://github.com/tqh/efi-example.
