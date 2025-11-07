/* user_diskio.c — SPI-based SD card driver for FatFS (STM32F401RE, SPI1, CS=PA4)
 * Place in: FATFS/Target/user_diskio.c
 */

#include "main.h"          // ✅ auto-includes spi.h and gpio.h
#include "fatfs.h"
#include "diskio.h"
#include "user_diskio.h"
#include <string.h>


/* ==== Pin / SPI bindings (edit if your wiring differs) ==== */
extern SPI_HandleTypeDef hspi1;
#define SD_SPI            hspi1
#define SD_CS_GPIO_PORT   GPIOA
#define SD_CS_PIN         GPIO_PIN_4

/* ==== Macros ==== */
#define CS_LOW()          HAL_GPIO_WritePin(SD_CS_GPIO_PORT, SD_CS_PIN, GPIO_PIN_RESET)
#define CS_HIGH()         HAL_GPIO_WritePin(SD_CS_GPIO_PORT, SD_CS_PIN, GPIO_PIN_SET)

/* Card type flags (from ChaN FatFS) */
#define CT_MMC            0x01    /* MMC ver 3 */
#define CT_SD1            0x02    /* SD ver 1 */
#define CT_SD2            0x04    /* SD ver 2 */
#define CT_SDC            (CT_SD1|CT_SD2)
#define CT_BLOCK          0x08    /* Block addressing */

/* ==== Locals ==== */
static volatile DSTATUS Stat = STA_NOINIT;
static BYTE CardType = 0;

/* ==== SPI helpers ==== */
static uint8_t spi_xfer(uint8_t d)
{
  uint8_t r;
  HAL_SPI_TransmitReceive(&SD_SPI, &d, &r, 1, HAL_MAX_DELAY);
  return r;
}

static void spi_send_multi(const uint8_t *buf, uint32_t len)
{
  HAL_SPI_Transmit(&SD_SPI, (uint8_t*)buf, len, HAL_MAX_DELAY);
}

static void spi_recv_multi(uint8_t *buf, uint32_t len)
{
  /* transmit dummy 0xFFs while receiving */
  memset(buf, 0xFF, len);                 /* optional – HAL will not read from tx buf in RX only */
  HAL_SPI_Receive(&SD_SPI, buf, len, HAL_MAX_DELAY);
}

static void send_dummy_clocks(uint32_t nbytes)
{
  uint8_t ff = 0xFF;
  for (uint32_t i = 0; i < nbytes; i++)
    HAL_SPI_Transmit(&SD_SPI, &ff, 1, HAL_MAX_DELAY);
}

static int wait_ready(uint32_t timeout_ms)
{
  uint32_t t0 = HAL_GetTick();
  uint8_t d;
  do {
    d = spi_xfer(0xFF);
    if (d == 0xFF) return 1;
  } while ((HAL_GetTick() - t0) < timeout_ms);
  return 0;
}

static void deselect_card(void)
{
  CS_HIGH();
  spi_xfer(0xFF); /* 1 extra clock */
}

static int select_card(uint32_t timeout_ms)
{
  CS_LOW();
  /* Wait until card drives DO high (sends 0xFF) */
  if (wait_ready(timeout_ms)) return 1;
  deselect_card();
  return 0;
}

/* Send a command packet (cmd is 0x40|cmd_index). Returns R1. */
static uint8_t send_cmd(uint8_t cmd, uint32_t arg)
{
  uint8_t buf[6];
  uint8_t r1;
  uint8_t crc = 0x01;             /* dummy CRC, except for CMD0/CMD8 */
  if (cmd == (0x40+0))  crc = 0x95; /* CMD0 needs valid CRC */
  if (cmd == (0x40+8))  crc = 0x87; /* CMD8 needs valid CRC */

  if (!select_card(500)) return 0xFF;

  buf[0] = cmd;
  buf[1] = (uint8_t)(arg >> 24);
  buf[2] = (uint8_t)(arg >> 16);
  buf[3] = (uint8_t)(arg >> 8);
  buf[4] = (uint8_t)(arg);
  buf[5] = crc;

  spi_send_multi(buf, 6);

  /* Skip stuff byte for CMD12 (stop) per spec */
  if (cmd == (0x40+12)) spi_xfer(0xFF);

  /* Wait for a response (MSB=0) within 10 bytes */
  for (int i = 0; i < 10; i++) {
    r1 = spi_xfer(0xFF);
    if (!(r1 & 0x80)) break;
  }
  return r1;
}

/* Receive a data block (len = 512) into buff. */
static int rcvr_datablock(uint8_t *buff, uint32_t len, uint32_t timeout_ms)
{
  uint32_t t0 = HAL_GetTick();
  uint8_t token;

  do {
    token = spi_xfer(0xFF);
    if (token == 0xFE) break;            /* Start block token */
  } while ((HAL_GetTick() - t0) < timeout_ms);

  if (token != 0xFE) return 0;

  /* Read data + 2-byte CRC (discard) */
  spi_recv_multi(buff, len);
  spi_xfer(0xFF);
  spi_xfer(0xFF);
  return 1;
}

/* Transmit a data block (len = 512) with token. token=0xFE for single block. */
static int xmit_datablock(const uint8_t *buff, uint8_t token)
{
  if (!wait_ready(500)) return 0;

  spi_xfer(token);
  spi_send_multi(buff, 512);

  /* Dummy CRC */
  spi_xfer(0xFF);
  spi_xfer(0xFF);

  /* Receive data response */
  uint8_t resp = spi_xfer(0xFF);
  if ((resp & 0x1F) != 0x05) return 0; /* 0bxxxxx0101 means accepted */

  return 1;
}

/* ==== FatFS required low-level functions (USER_*) ==== */

/* Initialize the drive */
DSTATUS USER_initialize (BYTE pdrv)
{
  if (pdrv != 0) return STA_NOINIT;

  /* CS high and 80 dummy clocks */
  deselect_card();
  send_dummy_clocks(10);

  CardType = 0;

  /* Enter Idle state with CMD0 */
  if (send_cmd(0x40+0, 0) != 0x01) { deselect_card(); return STA_NOINIT; }

  /* Check SD version via CMD8 */
  uint8_t ty = 0;
  uint8_t ocr[4];
  if (send_cmd(0x40+8, 0x000001AA) == 0x01) {
    /* Receive R7 trailing bytes */
    ocr[0] = spi_xfer(0xFF);
    ocr[1] = spi_xfer(0xFF);
    ocr[2] = spi_xfer(0xFF);
    ocr[3] = spi_xfer(0xFF);
    if (ocr[2] == 0x01 && ocr[3] == 0xAA) {
      /* Wait for leaving idle with ACMD41(HCS) */
      uint32_t t0 = HAL_GetTick();
      while (send_cmd(0x40+55, 0) <= 1 && (HAL_GetTick() - t0) < 1000) {
        if (send_cmd(0x40+41, 1UL<<30) == 0) break; /* HCS bit */
      }
      if ((HAL_GetTick() - t0) < 1000) {
        /* Read OCR via CMD58 to check CCS */
        if (send_cmd(0x40+58, 0) == 0) {
          for (int i=0;i<4;i++) ocr[i] = spi_xfer(0xFF);
          ty = (ocr[0] & 0x40) ? (CT_SD2 | CT_BLOCK) : CT_SD2; /* SDv2 */
        }
      }
    }
  } else {
    /* SDv1 or MMC */
    uint8_t cmd;
    if (send_cmd(0x40+55, 0) <= 1 && send_cmd(0x40+41, 0) <= 1) {
      ty = CT_SD1;  /* SDv1 */
      cmd = 0x40+41;
    } else {
      ty = CT_MMC;  /* MMCv3 */
      cmd = 0x40+1;
    }
    /* Wait for leaving idle */
    uint32_t t0 = HAL_GetTick();
    while ((send_cmd(cmd, 0) != 0) && ((HAL_GetTick() - t0) < 1000));
    /* Set block length to 512 for SDSC/MMC */
    if ((HAL_GetTick() - t0) < 1000 || send_cmd(0x40+16, 512) == 0) {
      /* ok */
    } else {
      ty = 0;
    }
  }

  CardType = ty;
  deselect_card();

  if (ty) {
    Stat &= ~STA_NOINIT;
  } else {
    Stat = STA_NOINIT;
  }

  return Stat;
}

/* Get drive status */
DSTATUS USER_status (BYTE pdrv)
{
  if (pdrv != 0) return STA_NOINIT;
  return Stat;
}

/* Read sector(s) */
DRESULT USER_read (BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
  if (pdrv != 0 || count == 0) return RES_PARERR;
  if (Stat & STA_NOINIT)        return RES_NOTRDY;

  /* Convert LBA to byte address if needed */
  if (!(CardType & CT_BLOCK)) sector *= 512;

  if (count == 1) {
    /* CMD17: single block read */
    if (send_cmd(0x40+17, sector) == 0 && rcvr_datablock(buff, 512, 200)) {
      deselect_card();
      return RES_OK;
    }
  } else {
    /* Multiple blocks: CMD18 then loop */
    if (send_cmd(0x40+18, sector) == 0) {
      UINT n = count;
      do {
        if (!rcvr_datablock(buff, 512, 200)) break;
        buff += 512;
      } while (--n);
      /* Stop transmission */
      send_cmd(0x40+12, 0);
      deselect_card();
      return (n ? RES_ERROR : RES_OK);
    }
  }

  deselect_card();
  return RES_ERROR;
}

/* Write sector(s) */
#if _USE_WRITE
DRESULT USER_write (BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{
  if (pdrv != 0 || count == 0) return RES_PARERR;
  if (Stat & STA_NOINIT)        return RES_NOTRDY;

  if (!(CardType & CT_BLOCK)) sector *= 512;

  if (count == 1) {
    /* CMD24: single block write */
    if (send_cmd(0x40+24, sector) == 0) {
      if (xmit_datablock(buff, 0xFE)) {  /* data token */
        deselect_card();
        return RES_OK;
      }
    }
  } else {
    /* Optional: implement CMD25 multi-block write.
       For simplicity, do repeated single writes (slower but works). */
    for (UINT i = 0; i < count; i++) {
      if (send_cmd(0x40+24, sector + (CardType & CT_BLOCK ? i : i*512)) != 0) {
        deselect_card();
        return RES_ERROR;
      }
      if (!xmit_datablock(buff + i*512, 0xFE)) {
        deselect_card();
        return RES_ERROR;
      }
    }
    deselect_card();
    return RES_OK;
  }

  deselect_card();
  return RES_ERROR;
}
#endif /* _USE_WRITE */

/* I/O control (sync, sector size, erase block size, etc.) */
#if _USE_IOCTL
DRESULT USER_ioctl (BYTE pdrv, BYTE cmd, void *buff)
{
  if (pdrv != 0) return RES_PARERR;
  if (Stat & STA_NOINIT) return RES_NOTRDY;

  DRESULT res = RES_ERROR;

  switch (cmd) {

    case CTRL_SYNC:
      if (select_card(500)) { deselect_card(); res = RES_OK; }
      break;

    case GET_SECTOR_SIZE:
      *(WORD*)buff = 512;
      res = RES_OK;
      break;

    case GET_BLOCK_SIZE:
      /* Erase block size in sectors (not used by most apps) */
      *(DWORD*)buff = 1;
      res = RES_OK;
      break;

    case GET_SECTOR_COUNT:
      /* (Optional) Parse CSD via CMD9 to get real count.
         Returning RES_PARERR will keep FatFS functional but size unknown. */
      res = RES_PARERR;
      break;

    default:
      res = RES_PARERR;
  }

  return res;
}
#endif /* _USE_IOCTL */

/* The driver structure FatFS links to */
Diskio_drvTypeDef USER_Driver =
{
  USER_initialize,
  USER_status,
  USER_read,
#if _USE_WRITE == 1
  USER_write,
#endif
#if _USE_IOCTL == 1
  USER_ioctl,
#endif
};
