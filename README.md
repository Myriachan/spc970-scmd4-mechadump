# SPC970 Mechacon Dumper using S-command 0x04
This tool dumps the Mechacon firmware from SPC970-based PlayStation 2 systems.  The Mechacon is the microcontroller in charge of controlling the disk drive hardware ("Mecha"nics "Con"troller).  It also decrypts MagicGate-encrypted PS2 programs such as the DVD player.

The PS2 has two major variants of Mechacon: one using an SPC970 microcontroller and one using an ARM7TDMI microcontroller.  SPC970s were used for the SCPH-10000 series through SCPH-39000, whereas ARM7TDMI chips--code named "Dragon"--were used on SCPH-50000 and later.

## Which kind do I have?
The easy way to tell: If the system has a FireWire port, it uses an SPC970 Mechacon.  If your system doesn't have a FireWire port, it has an ARM7TDMI Mechacon.

The technique to dumping the Mechacon firmware is very different between SPC970 and ARM7TDMI Mechacons.  This project only works on SPC970.  If you want to dump the ARM7TDMI version, see the [Mechadump](https://github.com/Myriachan/mechadump) repository.

## What will this let me do?
If you aren't a developer or reverse engineer, probably nothing.  This helps understanding these 26-year-old systems in a way we had not before, but for typical users it won't be useful.

## How is this different from SPC970-MechaLIBerator?
[SPC970-MechaLIBerator](https://github.com/Libbers/SPC970-MechaLIBerator) is another program that dumps SPC970 Mechacons.  It has the same end effect as this one, but works differently.

MechaLIBerator only seems to work on Mechacon 2.x and 3.x versions.  1.x versions seem to be incompatible with MechaLIBerator's dumping technique.  This one should work on all SPC970 Mechacons.

## What do I need?
 * A PS2 with an SPC970 Mechacon.
 * A CD burner capable of doing "DAO RAW" CD-R burning.  Only some CD-R drives can burn in this mode.  This is unfortunately a hard requirement due to the need for a CD with an invalid table of contents.
 * A blank CD-R.
 * A way to boot .elf files on your PS2.  OSDMenu, FMCB, OpenTuna, etc.
 * A FAT32-formatted flash drive compatible with PS2 homebrew such as this.  Also, SD cards will work with some USB adapters.
