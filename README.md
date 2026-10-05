# SPC970 Mechacon Dumper using S-command 0x04
This tool dumps the Mechacon firmware from SPC970-based PlayStation 2 systems.
The Mechacon is the microcontroller in charge of controlling the disk drive
hardware (**Mecha**nics **Con**troller) and MagicGate encryption.

> [!CAUTION]
> **THIS TOOL MIGHT DAMAGE YOUR PLAYSTATION 2.** It intentionally asks the DVD
> drive to do a physically impossible motion that theoretically could damage
> the drive sled or motor.  The tool immediately aborts the request after making
> it, but there's no guarantee that this prevents all possible damage.
> 
> **USE AT YOUR OWN RISK.**

The PS2 has two major variants of Mechacon: one using an SPC970 microcontroller
and one using an ARM7TDMI microcontroller.  SPC970s were used for the SCPH-10000
series through SCPH-39000, whereas ARM7TDMI chips—code named "Dragon"—were
used on SCPH-50000 and later.

## Which kind do I have?
The easy way to tell: If the system has a FireWire port, it uses an SPC970
Mechacon.  If your system doesn't have a FireWire port, it has an ARM7TDMI
Mechacon.

The technique to dumping the Mechacon firmware is very different between SPC970
and ARM7TDMI Mechacons.  This project only works on SPC970.  If you want to dump
the ARM7TDMI version, see the [Mechadump](https://github.com/Myriachan/mechadump)
repository.

## What will this let me do?
If you aren't a developer or reverse engineer, probably nothing.  This helps
understanding these 26-year-old systems in a way we had not before, but for
typical users it won't be useful.

## How is this different from SPC970-MechaLIBerator?
[SPC970-MechaLIBerator](https://github.com/Libbers/SPC970-MechaLIBerator) is
another program that dumps SPC970 Mechacons.  It has the same end effect as this
one, but works differently.

MechaLIBerator only seems to work on Mechacon 2.x and 3.x versions.  1.x
versions seem to be incompatible with MechaLIBerator's dumping technique.  This
tool should work on all SPC970 Mechacons.

## What do I need?
 * A PS2 with an SPC970 Mechacon.
 * A CD burner capable of doing "DAO RAW" CD-R burning.
   * Only some CD-R drives can burn in this mode.
 * A blank CD-R.
 * A way to boot .elf files on your PS2: OSDMenu, Free McBoot, OpenTuna, etc.
 * A FAT32-formatted flash drive compatible with PS2 homebrew such as this.
   * Alternatively, FAT32-formatted SD cards in some USB adapters will work with PS2.
 * A way to run [cdrdao](https://cdrdao.sourceforge.net/).  See OS table below.

## cdrdao operating systems
You need one of these operating systems in order to run cdrdao:

 * Linux: Natively and not Windows Subsystem for Linux.
   * WSL _might_ work if you use an external USB-attached burner and configure WSL
   to pass the USB port through to Linux.  You're on your own if you want to try this.
 * macOS: Requires [brew](https://brew.sh/).
 * Windows: cdrdao can be compiled under [Cygwin](https://www.cygwin.com/).
   * You need a bunch of packages and it's quite annoying to get all the ones you need.
   I managed to get it working and burned a working disc that way.

You will be compiling cdrdao yourself, not using a premade package, because of the need to patch it.

## How to use
This requires knowing your way around open-source UNIX software.

1. Either `make` the spc970-scmd4-mechadump project yourself using
   [PS2DEV](https://github.com/ps2dev/ps2dev), or download one of the Release
   versions' .elf files.
1. Acquire the source code of the [1.2.6 release of cdrdao](https://github.com/cdrdao/cdrdao/releases/tag/rel_1_2_6)
   and extract it somewhere.
1. Using the UNIX utility `patch`, apply [cdrdao-libby-toc-hack.patch] to the
   cdrdao 1.2.6 source code.
1. Build cdrdao:
   ```sh
   cd /wherever/cdrdao
   test -f configure || ./autogen
   ./configure
   make -j 4
   ```
1. Insert a blank CD-R into your burner.
1. Find out your burner's device name:
   ```sh
   /wherever/cdrdao/dao/cdrdao scanbus
   ```
1. Do a test run of the burning process, replacing `/dev/meow` with your device name.
   `silence.toc` is a file provided in this package.
   ```sh
   /wherever/cdrdao/dao/cdrdao simulate --device /dev/meow --driver generic-mmc-raw --libby-hack -n /wherever/spc-scmd4-mechadump/silence.toc
   ```
   * You can add e.g. `--speed 8` to set burn speed to 8x.
   * `--libby-hack` is what enables hacking the ToC.
1. If that succeeded, eject the blank disc, reinsert it, and burn for real:
   ```sh
   /wherever/cdrdao/dao/cdrdao write --device /dev/meow --driver generic-mmc-raw --libby-hack -n /wherever/spc-scmd4-mechadump/
   ```
1. Place `spc-scmd4-mechadump.elf` onto your USB stick.
1. Put the USB stick into one of your PS2's USB ports.
1. Boot your PS2 into the ELF loader of your choice (probably wLaunchELF).
1. While in the ELF loader menu, insert the burned CD into the PS2.
   * Having the disc in the drive during boot will make the CD audio player come up.
1. Run `spc-scmd4-mechadump.elf`.
1. You'll see a sector number count up from `000000` to `002FFF` then from `FC0000` to `FFFFFF`.
   * This takes about 20 minutes.
   * You will hear little noises made by your drive because it will start to seek then abort
     the seek thousands of times.  This is why we have a damage warning.
1. When it finishes, it'll say "Done; hanging".  Hold down the PS2's Reset button
   to turn off the system.
1. Insert the USB stick into your computer.  These are the files it makes:
   * `mechadump.bin`: the Mechacon ROM!
   * `mechadump.txt`: a log file of all the commands executed.
   * `mechadump-ram.bin` the Mechacon's RAM at the time of dumping.

## How does this exploit work?
Libby found a bug in the SPC970 Mechacon's PS2 CDVD "S" command `0x04`.

The `0x04` command appears to mean something like "get the sector number of the
last read error".  Though it seems to be for handling read errors, the variable
this function reads is also the variable where the "seek" "N" command stores
the sector number you asked to seek to.

### The code handling N command 0x05 and S command 0x04
As far as we can surmise, the C code for the two functions is something like the
below.  `// BUG:` marks the bug that we're exploiting to read the Mechacon memory.

```c
uint32_t g_seek_target;
uint32_t g_error_lsn;

// N command 0x05: Seek
void ncmd_seek_or_pause()
{
    //...
    // Validate that the requested sector is in range.  This check is why we need to
    // burn a custom disc.
    if (*(uint32_t*) &g_command_params[0] >= g_start_of_leadout)
        goto eror;

    //...
    // Save the requested sector number to global variables.
    g_seek_target = *(uint32_t*) &g_command_params[0];
    g_error_lsn = *(uint32_t*) &g_command_params[0];
    //...
}

// S command 0x04: GetErrorLsn
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

    // 0x06D = DSP register of the FIFO to send S command results to the IOP.
    write_dsp_register(0x06D, result);

    if (result == 0x00)
    {
        // Command accepted.
        //
        // Give the 3 low bytes of the g_error_lsn variable back to the IOP.
        // Note that SPC970 and the Dragon ARM7 chips are little-endian.
        //
        // BUG: This should say &g_error_lsn on the right!
        uint8_t* read_from = (uint8_t*) g_error_lsn;
        write_dsp_register(0x06D, read_from[0]);
        write_dsp_register(0x06D, read_from[1]);
        write_dsp_register(0x06D, read_from[2]);
    }
    // Tell DSP that the S command completed.
    clear_dsp_register_bit(0x06C, 7);
}
```

`g_error_lsn`'s value is being used as the pointer to read, rather than
sending `g_error_lsn` itself!  This is a read-anywhere primitive if we can
control `g_error_lsn`—and we can, thanks to the seek command.

Our current hypothesis about the bug is that because they had to cast to
`uint8_t*` in order to send the 3 bytes of the value of `g_error_lsn`, it
suppressed a warning that the compiler would've given for writing an
integer value to a pointer.  A simple missing `&` would be enough to cause
this bug.

This bug was fixed on Dragon; it's correctly `&g_error_lsn` on Dragon chips.
My guess is that the bug was noticed during porting because ARM chips have
data abort exceptions for bad hardware addresses, unlike the SPC970.  S
command `0x04` would've crashed the ARM7 Mechacon if `g_error_lsn >= 0x44000`
by reading past the end of ROM.

There is one problem with our read primitive, though: our sector ID must be
less than `g_start_of_leadout`.

### Bypassing the `g_start_of_leadout` check
On SPC970 Mechacons, the ROM is 256 KiB at `0xFC0000` for 1.x and 2.x, and
192 KiB at `0xFD0000` for version 3.x.  (However, we always dump 256 KiB
from `0xFC0000`; we think that the ROM might always actually be 256 KiB and
just not use the first bank.)

In order for `ncmd_seek_or_pause` to set `g_error_lsn` to the value we want,
we need `g_start_of_leadout` to be at least `0x0100000` so that the seek
request isn't rejected before writing `g_error_lsn`.

`g_start_of_leadout` is equal to the number of logical sectors on the
inserted disc, as specified by the "table of contents".  The problem is, at
2048 bytes per sector for both CD and DVD, `0x01000000` sectors is 32 GiB,
far more than even a dual-layer DVD has.  We have to burn a disc that lies
about its size in its table of contents.

On DVDs, there is a sector number for lead-out that could be used.  However,
this value is written automatically by DVD burners, so burning a disc to get
around this would require modified DVD burner firmware.

On CDs, a CD burning mode called "DAO RAW" allows the PC to write whatever
it wants to the CD-R's table of contents.  However, a new problem emerges:
the encoding of the start of lead-out in the ToC is a minutes:seconds:frames
value with each part encoded as binary-coded decimal.  Because the Mechacon
validates BCD values, the maximum value we can use is `99:59:74`, which
comes out to a `g_start_of_leadout` of `0x0006DD39`.  This is not enough.

Well, it turns out that CD-ROMs have two seconds of "pre-gap" between the
end of lead-in and the first logical sector, sector 0.  Track 1 starts at
MSF 00:02:00.  The algorithm to convert an absolute time on a CD to a CD-ROM
logical sector number is:

```c
(((minutes * 60) + seconds) * 75) + frames - (2 * 75)
```

Well... Libby had another idea: underflow it with that 150 subtraction!
If we use a hacked CD ToC that says lead-out starts at `00:01:74`, this
formula underflows to `0xFFFFFFFF`.  Now we can read any address in the
SPC970's 24-bit address space.  Because of DAO RAW mode on many CD-R drives,
we can burn such a hacked lead-out start time without needing to modify
burner firmware.

### Burning an invalid table of contents
The open-source project [cdrdao](https://cdrdao.sourceforge.net/) knows how
to burn in DAO RAW mode.  However, its generation of the table of contents
is hardcoded and not taken as input.  We patch cdrdao's source code so that
when it makes the `0xA2` "point" in the table of contents, cdrdao writes it
as `00:01:74`.

Dumping SPC Mechacons with this technique thus requires compiling cdrdao
from source.

### Preventing drive damage
The Mechacon code isn't the smartest.  If you have a hacked table of
contents that's seen as ridiculously large then request a seek to a very
large sector number, the Mechacon will happily tell its drive head to
physically move a meter away.  It's not physically possible, so the drive
will end up literally banging its head against a wall.

That's bad, so to avoid that, we abort the seek immediately after we request
it using the `sceCdBreak` command.  The drive head doesn't have enough time
to gain speed before it's told to stop.  Hopefully it's enough.  The
`g_error_lsn` value got set by the `sceCdSeek` and doesn't get cleared from
aborting. 

### Credits
 * [Myria](https://github.com/Myriachan): wrote this tool, cdrdao patch and documentation
 * [Libby](https://github.com/Libbers): found the two exploits used by this tool and
   [the exploit](https://github.com/Libbers/SPC970-MechaLIBerator) that made the first
   SPC970 Mechacon dumps 
 * [pcm720](https://github.com/pcm720): code reorganization, testing
 * [l_oliveira](https://github.com/7l-oliveira): testing, history, hardware information
 * [DiscoStarslayer](https://github.com/DiscoStarslayer):
   [SPC970 decompiler](https://github.com/DiscoStarslayer/ghidra-spc970), organization,
   helped with early dumps
