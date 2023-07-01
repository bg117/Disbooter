# Disbooter

Destroys your computer's ability to boot.

**Untested.** Use at your own risk.

Disbooter may or may not be malware; it does not modify any of your files. However:

- For GPT: it replaces the signature `EFI PART` at LBA 1 with `0x0000000000000000`

- For MBR: it replaces the signature `0xAA55` at offset 510 with `0x0000`

## Flags

`--rollback-mbr-signature`: Returns the `0xAA55` signature of the MBR. Has no effect on GPT

`--rollback-gpt-signature`: Returns the `EFI PART` signature of GPT. Has no effect on MBR