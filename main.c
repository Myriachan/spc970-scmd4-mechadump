#include "libcdvd-common.h"
#include <debug.h>
#include <dirent.h>
#include <inttypes.h>
#include <iopcontrol.h>
#include <kernel.h>
#include <libcdvd.h>
#include <loadfile.h>
#include <sbv_patches.h>
#include <sifrpc.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

struct TocSubQ {
  uint8_t adr : 4;
  uint8_t control : 4;
  uint8_t tno;
  uint8_t point;
  uint8_t min;
  uint8_t sec;
  uint8_t frame;
  uint8_t zero;
  uint8_t pmin;
  uint8_t psec;
  uint8_t pframe;
  /* uint16_t crc;  but PS2 sceCdGetToc doesn't provide the CRC. */
};

struct MechaconVersion
{
  uint8_t major;
  uint8_t minor;
  uint8_t region;
};

/* C11 _Static_assert */
_Static_assert(sizeof(struct TocSubQ) == 10, "TocSubQ size mismatch");

// Macros for loading embedded IOP modules
#define IRX_DEFINE(mod)                                                                                                                              \
  extern unsigned char mod##_irx[] __attribute__((aligned(16)));                                                                                     \
  extern uint32_t size_##mod##_irx

// Defines moduleList entry for embedded and external modules
#define INT_MODULE(mod) {#mod, NULL, mod##_irx, &size_##mod##_irx}

IRX_DEFINE(iomanX);
IRX_DEFINE(fileXio);
IRX_DEFINE(bdm);
IRX_DEFINE(bdmfs_fatfs);
IRX_DEFINE(usbd_mini);
IRX_DEFINE(usbmass_bd_mini);

typedef struct ModuleListEntry {
  char *name;         // Module name
  char *path;         // Module path for external modules
  unsigned char *irx; // Pointer to IRX module
  uint32_t *size;     // IRX size
} ModuleListEntry;

// List of modules to load
static ModuleListEntry moduleList[] = {
    INT_MODULE(iomanX),    INT_MODULE(fileXio),         //
    INT_MODULE(bdm),       INT_MODULE(bdmfs_fatfs),     //
    INT_MODULE(usbd_mini), INT_MODULE(usbmass_bd_mini), //
};
#define MODULE_COUNT sizeof(moduleList) / sizeof(ModuleListEntry)

// Initializes IOP modules
int initModules() {
  int ret = 0;
  int iopret = 0;

  // Initialize the RPC manager and reboot the IOP with OSDSYS modules
  sceSifInitRpc(0);
  while (!SifIopReset("rom0:UDNL", 0)) {
  };
  while (!SifIopSync()) {
  };

  // Initialize the RPC manager
  sceSifInitRpc(0);

  // Apply patches required to load modules from EE RAM
  sbv_patch_enable_lmb();
  sbv_patch_disable_prefix_check();
  sbv_patch_fileio();

  // Load modules
  for (int i = 0; i < MODULE_COUNT; i++) {
    ret = 0;
    iopret = 0;

    ret = SifExecModuleBuffer(moduleList[i].irx, *moduleList[i].size, 0, NULL, &iopret);
    if (ret >= 0)
      ret = 0;
    if (iopret == 1)
      ret = iopret;

    if (ret) {
      scr_printf("\n\t\tERROR: Failed to initialize module %s: %d\n", moduleList[i].name, ret);
      return ret;
    }
  }
  return 0;
}

void GetMechaconVersion(struct MechaconVersion* version) {
  uint8_t resultBytes[16];
  memset(resultBytes, 0xCC, sizeof(resultBytes));

  /* S command 0x03 subcommand 0x00 = get Mechacon version */
  if (sceCdApplySCmd(0x03, "\x00", 1, resultBytes)) {
    version->major = resultBytes[1];
    version->minor = resultBytes[2];
    version->region = resultBytes[0];
  } else {
    version->major = 0;
    version->minor = 0;
    version->region = 0;
  }
}

void MechaconVersionToString(char *buffer, size_t size, const struct MechaconVersion* version) {
  snprintf(buffer, size, "%d.%02d.%02d", version->major, version->minor, version->region);
}

/* NOTE: Values are returned in BCD because that's their native format. */
int GetTocLeadOutStart(const uint8_t *toc, size_t tocSize, uint8_t *minute, uint8_t *second, uint8_t *frame) {
  *minute = 0x00;
  *second = 0x00;
  *frame = 0x00;

  for (; tocSize >= sizeof(struct TocSubQ); tocSize -= sizeof(struct TocSubQ), toc += sizeof(struct TocSubQ)) {
    struct TocSubQ empty = {0};
    if (memcmp(toc, &empty, sizeof(empty)) == 0)
      break;

    struct TocSubQ entry;
    memcpy(&entry, toc, sizeof(entry));

    if (entry.adr != 0x1) /* normal TOC entry */
      continue;
    if (entry.tno != 0x00) /* lead-in is marked track 0x00.  lead-out would be 0xAA. */
      continue;
    if (entry.zero != 0)
      continue;

    if (entry.point != 0xA2) /* end of lead-out, the value we want */
      continue;

    /* min, sec, frame are the time within lead-in itself; we want the "p" versions. */
    *minute = entry.pmin;
    *second = entry.psec;
    *frame = entry.pframe;
    return 1;
  }

  return 0;
}

void DumpData(FILE *outputFile, FILE *outputBinfile, uint32_t address, uint32_t size) {
  if (address > UINT32_MAX - size) {
    scr_printf("Error: Overflow sector range");
    return;
  }
  if (address + size > 0x1000000) {
    scr_printf("Error: Out of range for SPC970");
    return;
  }
  if (size < 3) {
    scr_printf("Error: Size must be at least 3");
    return;
  }

  fprintf(outputFile, "start_sector %06" PRIX32 "\n", address);

  while (size > 0) {
    uint32_t sector;

    if (size < 3)
      sector = address - (3 - size);
    else
      sector = address;

    uint32_t bytesValid = (size < 3) ? size : 3;
    uint32_t binOffset = 4 - bytesValid;

    scr_setXY(0, 18);
    scr_printf("seeking sector %06" PRIX32 "         \n", sector);

    /* Because we're possibly seeking to a ridiculously sector number here,
       in order to avoid the drive literally banging its head against a wall,
       we need to do an sceCdBreak as soon as possible! */
    int seekResult = sceCdSeek(sector);

    int breakResult = sceCdBreak();
    int syncResult = sceCdSync(0);

    uint8_t resultBytes[16];
    memset(resultBytes, 0xCC, sizeof(resultBytes));
    int scmdResult = sceCdApplySCmd(0x04, "", 0, resultBytes);
    sceCdSync(0);

    fprintf(outputFile, "result %06" PRIX32 " %d %d %d %d %02X %02X %02X %02X\n", sector, seekResult, scmdResult, breakResult, syncResult,
            resultBytes[0], resultBytes[1], resultBytes[2], resultBytes[3]);

    /* resultBytes[0] is the command status; ROM data starts at resultBytes[1].
       For normal reads we write 3 bytes (1..3). For tail reads we write only
       the new trailing byte(s), skipping the overlap. */
    fwrite(&resultBytes[binOffset], 1, bytesValid, outputBinfile);

    if (size < 3)
      break;
    address += 3;
    size -= 3;
  }
}

uint8_t BcdToBinary(uint8_t bcd) { return (uint8_t)(((bcd >> 4) * 10) + (bcd & 0x0F)); }

int main(void) {
  static const struct {
    int type;
    int ok;
    const char *name;
  } s_mediaTypes[] = {
      {SCECdGDTFUNCFAIL, 0, "Unknown"},
      {SCECdNODISC, 0, "No disc"},
      {SCECdDETCT, 0, "Detecting"},
      {SCECdDETCTCD, 0, "Detecting"},
      {SCECdDETCTDVDS, 0, "Detecting (DVD single-layer)"},
      {SCECdDETCTDVDD, 0, "Detecting (DVD dual-layer)"},
      {SCECdPSCD, 1, "PS1 CD (data only)"},
      {SCECdPSCDDA, 1, "PS1 CD (data + audio)"},
      {SCECdPS2CD, 1, "PS2 CD (data only)"},
      {SCECdPS2CDDA, 1, "PS2 CD (data + audio)"},
      {SCECdPS2DVD, 0, "PS2 DVD"},
      {SCECdDVDVR, 0, "DVD-VR"},
      {SCECdCDDA, 1, "CD audio"},
      {SCECdDVDV, 0, "DVD video"},
  };

  uint8_t discType;
  int discTypeOK;
  const char *discTypeString;
  size_t i;
  struct MechaconVersion mechaconVersion;

  char mechaconVersionString[20];
  uint8_t toc[2064];

  uint8_t leadOutMinute, leadOutSecond, leadOutFrame;
  uint32_t leadOutLba;

  FILE *outputFile;
  FILE *outputBinfile;
  FILE *outputRamfile;

  init_scr();
  scr_setCursor(0);
  scr_setbgcolor(0xFF000000);
  scr_clear();
  scr_printf("SPC970 TOC-based Mechacon dumper tool by Myria & pcm720\n");
  scr_printf("SCMD 0x04 read exploit and invalid TOC trick by Libby\n");

  scr_printf("Initializing modules\n");
  initModules();
  scr_printf("Initializing libcdvd\n");
  sceCdInit(SCECdINIT);

  GetMechaconVersion(&mechaconVersion);
  MechaconVersionToString(mechaconVersionString, sizeof(mechaconVersionString) / sizeof(mechaconVersionString[0]), &mechaconVersion);
  scr_printf("Mechacon version: %s\n", mechaconVersionString);

  if (mechaconVersion.major >= 5) {
    scr_printf("ERROR: Only SPC970 Mechacons are supported; this is a Dragon.\n");
    sleep(5);
    return 0;
  }

  discType = sceCdGetDiskType();
  if ((discType >= SCECdNODISC) && (discType < SCECdUNKNOWN)) {
    scr_printf("Waiting for disc\n");
    while ((discType >= SCECdNODISC) && (discType < SCECdUNKNOWN)) {
      sleep(1);
      discType = sceCdGetDiskType();
    }
  }

  discTypeOK = s_mediaTypes[0].ok;
  discTypeString = s_mediaTypes[0].name;

  for (i = 0; i < sizeof(s_mediaTypes) / sizeof(s_mediaTypes[0]); i++) {
    if (s_mediaTypes[i].type == discType) {
      discTypeOK = s_mediaTypes[i].ok;
      discTypeString = s_mediaTypes[i].name;
    }
  }

  scr_printf("Disc type: %02" PRIX8 " (%s)\n", discType, discTypeString);

  if (!discTypeOK) {
    scr_printf("ERROR: Only CDs are supported\n");
    sleep(5);
    return 0;
  }

  scr_printf("Getting TOC\n");
  memset(toc, 0xCC, sizeof(toc));
  if (!sceCdGetToc(toc)) {
    scr_printf("ERROR: Get TOC failed\n");
    sleep(5);
    return 0;
  }

  if (!GetTocLeadOutStart(toc, sizeof(toc), &leadOutMinute, &leadOutSecond, &leadOutFrame)) {
    scr_printf("ERROR: Parse TOC failed\n");
    sleep(5);
    return 0;
  }
  scr_printf("TOC start of lead-out: %02" PRIX8 ":%02" PRIX8 ":%02" PRIX8 "\n", leadOutMinute, leadOutSecond, leadOutFrame);
  leadOutMinute = BcdToBinary(leadOutMinute);
  leadOutSecond = BcdToBinary(leadOutSecond);
  leadOutFrame = BcdToBinary(leadOutFrame);

  /* This is the calculation done by Mechacon to compute the highest LBA
     that read and seek operations will be allowed to access (this minus 1).
     It's what the malformed table of contents is for: getting this value
     to underflow to 0xFFFFFFFF. */
  leadOutLba = ((((uint32_t)leadOutMinute * 60) + (uint32_t)leadOutSecond) * 75) + (uint32_t)leadOutFrame - (2 * 75);
  if (leadOutLba < 0x1000000) {
    scr_printf("WARNING: TOC max %08" PRIX32 " too small; probably will fail to dump ROM\n", leadOutLba);
  } else {
    scr_printf("Calculated TOC max 0x%08" PRIX32 " looks OK\n", leadOutLba);
  }

  scr_printf("Waiting for usb:/\n");
  DIR *dir;
  while (1) {
    dir = opendir("usb0:/");
    if (dir) {
      closedir(dir);
      break;
    }
    sleep(1);
  }

  scr_printf("Opening usb0:/mechadump* for writing\n");
  outputFile = fopen("usb0:/mechadump.txt", "wb");
  if (!outputFile) {
    scr_printf("ERROR: Opening mechadump.txt failed\n");
    sleep(5);
    return 0;
  }
  outputBinfile = fopen("usb0:/mechadump.bin", "wb");
  if (!outputBinfile) {
    fclose(outputFile);
    scr_printf("ERROR: Opening mechadump.bin failed\n");
    sleep(5);
    return 0;
  }
  outputRamfile = fopen("usb0:/mechadump-ram.bin", "wb");
  if (!outputRamfile) {
    fclose(outputFile);
    fclose(outputBinfile);
    scr_printf("ERROR: Opening mechadump.bin failed\n");
    sleep(5);
    return 0;
  }

  /* Dump RAM. */
  DumpData(outputFile, outputRamfile, 0x000000, 0x3000);
  /* Dump ROM.
     The S command 0x04 handler uses 16-bit pointer arithmetic, so we can't read e.g. 0xFCFFFF
     and expect to get 0xFCFFFF-0xFD0002; we'd get 0xFC0000 and 0xFC0001 again. */
  DumpData(outputFile, outputBinfile, 0xFC0000, 0x10000);
  DumpData(outputFile, outputBinfile, 0xFD0000, 0x10000);
  DumpData(outputFile, outputBinfile, 0xFE0000, 0x10000);
  DumpData(outputFile, outputBinfile, 0xFF0000, 0x10000);

  fclose(outputFile);
  outputFile = NULL;
  fclose(outputBinfile);
  outputBinfile = NULL;
  fclose(outputRamfile);
  outputRamfile = NULL;

  scr_setXY(0, 20);
  scr_printf("Done; hanging\n");

  sceCdStop();

  SleepThread();
  SifExitRpc();
  return 0;
}
