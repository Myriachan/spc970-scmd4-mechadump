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
 * A CD burner capable of doing "DAO RAW" CD-R burning.
   * Only some CD-R drives can burn in this mode.
 * A blank CD-R.
 * A way to boot .elf files on your PS2.  OSDMenu, FMCB, OpenTuna, etc.
 * A FAT32-formatted flash drive compatible with PS2 homebrew such as this.
   * Alternatively, some SD cards will work with some USB adapters.
 * Either a machine running Linux and macOS, or Windows with Cygwin installed.
   * This is needed for compiling and running [cdrdao](https://cdrdao.sourceforge.net/).

## How does this exploit work?
Libby found a bug in the SPC970 Mechacon's PS2 CDVD "S" command `0x04`.

The `0x04` command appears to mean something like "get the sector number of the last read error".  Though it seems to be for handling read errors, the variable this function reads is also the variable where the "seek" "N" command stores the sector number you asked to seek to.

As far as we can surmise, the C code for the two functions is something like the below.  

```c
uint32_t g_error_lsn;

// N command 0x05
void ncmd_seek_or_pause()
{
    // In the 2000s, nobody followed pointer aliasing rules.
    *(uint32_t*) &g_command_params[0] = *(uint32_t*) ncmd_params;

    // Adds 150 for CDs and 0x30000 for DVDs.
    *(uint32_t*) &g_command_params[4] = convert_lsn_to_physical(*(uint32_t*) &g_command_params[0]);

    // Why we need to burn a custom disc.
    if (*(uint32_t*) &g_command_params[0] >= g_start_of_leadout)
        goto error;

    //...
    g_seek_target = *(uint32_t*) &g_command_params[0];
    g_error_lsn = *(uint32_t*) &g_command_params[0];
    //...
}

// S command 0x04
void scmd_get_error_lsn()
{
    uint8_t result;
    if (g_num_command_params == 0)
    {
        result = 0x00;
        if (!(g_some_status & SOME_STATUS_BIT_0))
        {
            result = 0x82;
        }
    }
    else
    {
        result = 0x81;
    }

    // 0x06D = DSP register for FIFO to send IOP S command results.
    write_dsp_register(0x06D, result);

    if (result == 0x00)
    {
        // Command accepted.  Note that SPC970 and Dragon ARM7TDMI are little-endian.
        // BUG: This should say &g_error_lsn on the right!
        uint8_t* __far read_from = g_error_lsn;
        write_dsp_register(0x06D, read_from[0]);
        write_dsp_register(0x06D, read_from[1]);
        write_dsp_register(0x06D, read_from[2]);
    }
    // Tell DSP that S command completed.
    clear_dsp_register_bit(0x06C, 7);
}
```

Because this is C and not C++, loading a pointer with an integer value without a cast is only a warning, not an error.  Either the warning was ignored or a programmer naively cast it to a pointer to shut the warning up.  Assuming that our hypothesis is correct, anyway.

So to exploit the bug to read memory, we request a seek to the sector number matching the SPC970 address we want to read, then issue command 0x04.  The Mechacon then replies with the 3 bytes at that address!

This bug was fixed on Dragon; it's correctly `&g_error_lsn` on Dragon chips.  I guess they saw the warning.

## The `g_start_of_leadout` check
On SPC970 Mechacons, the ROM is 256 KiB at `0xFC0000` for 1.x and 2.x, or 192 KiB at `0xFD0000` for version 3.x.

In order for `ncmd_seek_or_pause` to set `g_error_lsn` to the value we want, we need `g_start_of_leadout` to be at least `0x0100000`.  `g_start_of_leadout` is equal to the number of logical sectors on the inserted disc, as specified by the "table of contents".

The problem is, at 2048 bytes per sector for both CD and DVD, `0x01000000` sectors is 32 GiB, far more than even a dual-layer DVD has.  We have to burn a disc that lies about its size in its table of contents.

On DVDs, there is a sector number for lead-out that could be used.  However, this value is written automatically by DVD burners, so burning a disc to get around this would require modified firmware.

On CDs, a CD burning mode called "DAO RAW" allows the PC to write whatever it wants to the CD-R's table of contents.  However, a new problem emerges: the encoding of the `0xA2` "point" in the ToC is a minutes:seconds:frames value with each part encoded as binary-coded decimal.  Because of the BCD conversion algorithm in the Mechacon validating its input, the maximum value we can use is 99:59:74, which comes out to a `g_start_of_leadout` of `0x0006DD39`, not enough.

Well, it turns out that CD-ROMs have two seconds of "pre-gap" between the end of lead-in and the first logical sector, sector 0.  Track 1 starts at MSF 00:02:00.  The algorithm to convert an absolute time on a CD to a CD-ROM logical sector number is:

```c
(((minutes * 60) + seconds) * 75) + frames - (2 * 75)
```

Well... Libby had an idea: underflow it with that 150 subtraction!  If we use a hacked CD ToC that says lead-out starts at 00:01:74, this formula underflows to `0xFFFFFFFF`!  Now we can read any address in the SPC970's 24-bit address space.  Because of DAO RAW mode on many CD-R drives, we can burn such a hacked lead-out start time without needing to modify burner firmware.
