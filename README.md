# Disbooter

Destroys your computer's ability to boot.

Disbooter may or may not be malware; it does not modify any of your files. However:

    - For GPT: it renames ESP\EFI to ESP\DISBOOTED

    - For MBR: it replaces the signature 0xAA55 at offset 510 with 0x0000

## Flags

--rollback-mbr-signature: Returns the 0xAA55 signature of the MBR. Has no effect on GPT
--rollback-efi-folder: Renames ESP\DISBOOTED to ESP\EFI