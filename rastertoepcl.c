/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * rastertoepcl - CUPS raster filter for the Zebra P330i card printer (EPCL)
 *
 * Copyright (C) 2026 Matthew Puckett
 *
 * Converts 8-bit RGB CUPS raster (1030 x 646 @ 300 dpi, one page per card)
 * into EPCL using the uncompressed GS (YMC), G/O (resin K) and vG/vO
 * (overlay) command sequence. The EPCL encoding is ported from pyepcl
 * (https://github.com/BryceSchroeder/pyepcl, LGPL-3.0) and verified
 * byte-identical to its output.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or (at
 * your option) any later version. See COPYING.LESSER and COPYING.
 *
 * Options (PPD / -o):
 *   zeRibbon=YMCKO|K       ribbon type (K = monochrome resin)
 *   zeOverlay=On|Off       full-card overlay panel (YMCKO only)
 *   zeBlackExtract=Off|Low|Medium|High
 *                          route near-black pixels to the resin K panel
 *                          (crisp text/barcodes) instead of composite YMC
 *   zeMonoThreshold=0..255 luminance cut for K ribbon (default 128)
 *
 * History:
 *   1.0.1  Overlay-off / K-ribbon jobs end with a blank "I" (print + eject).
 *   1.0.0  Initial release.
 */

#include <cups/cups.h>
#include <cups/ppd.h>
#include <cups/raster.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CARD_W 1030
#define CARD_H 646
#define K_BYTES 81 /* ceil(646/8) */

static unsigned char img[CARD_H][CARD_W][3];
static unsigned char kmask[CARD_H][CARD_W];
static volatile sig_atomic_t canceled = 0;

static void on_term(int sig) { (void)sig; canceled = 1; }

static const char *get_opt(const char *name, int num_options,
                           cups_option_t *options, ppd_file_t *ppd,
                           const char *def)
{
  const char *v = cupsGetOption(name, num_options, options);
  if (v) return v;
  if (ppd) {
    ppd_choice_t *c = ppdFindMarkedChoice(ppd, name);
    if (c) return c->choice;
  }
  return def;
}

/* GS payload escaping, as pyepcl found works on hardware: nudge the three
 * reserved byte values by one instead of '['-escaping them. */
static inline unsigned char gs_byte(unsigned char v)
{
  if (v == 0x5B) return 0x5A; /* '[' */
  if (v == 0x0D) return 0x0E; /* CR  */
  if (v == 0x1B) return 0x1C; /* ESC */
  return v;
}

/* Bitmap row escaping for O / vO lines: '[' prefix. */
static void put_escaped(const unsigned char *d, int n)
{
  for (int i = 0; i < n; i++) {
    if (d[i] == '[' || d[i] == '\r' || d[i] == 0x1B) putchar('[');
    putchar(d[i]);
  }
}

/* Pack card column i (646 dots, top->bottom reversed) LSB-first, then reverse
 * byte order, matching np.flip(np.packbits(K[i], bitorder='little')). */
static void pack_k_row(int i, unsigned char out[K_BYTES], int all_on)
{
  unsigned char packed[K_BYTES];
  memset(packed, 0, sizeof(packed));
  for (int j = 0; j < CARD_H; j++) {
    int on = all_on ? 1 : kmask[CARD_H - 1 - j][i];
    if (on) packed[j >> 3] |= (unsigned char)(1u << (j & 7));
  }
  for (int m = 0; m < K_BYTES; m++) out[m] = packed[K_BYTES - 1 - m];
}

static void emit_card(int ribbon_k, int overlay)
{
  unsigned char row[K_BYTES];

  fputs("\x1B" "F\r\x1B" "vF\r", stdout);
  fputs("\x1B$LD 0 0\r\x1B$LD 0 1\r\x1B$LD 0 2\r", stdout);

  if (!ribbon_k) {
    /* YMC: 10 x 2 tiles of 103 x 323, Y=0 (from B), M=1 (G), C=2 (R) */
    for (int x = 0; x < CARD_W && !canceled; x += 103)
      for (int y = 0; y < CARD_H; y += 323)
        for (int p = 0; p < 3; p++) {
          int chan = 2 - p;
          printf("\x1BGS %d 2 %d %d 103 323 ", p, x, y);
          for (int a = 0; a < 103; a++) {
            int sx = x + 102 - a;
            for (int b = 0; b < 323; b++) {
              int sy = y + b;
              unsigned char v = kmask[sy][sx] ? 0 : (unsigned char)(255 - img[sy][sx][chan]);
              putchar(gs_byte(v));
            }
          }
          putchar('\r');
        }
  }

  /* Resin black */
  fputs("\x1BG 0 0 0 81 1030 1\r", stdout);
  for (int n = 0; n < CARD_W; n++) {
    pack_k_row(CARD_W - 1 - n, row, 0);
    fputs("\x1BO", stdout);
    put_escaped(row, K_BYTES);
    putchar('\r');
  }

  if (!ribbon_k) {
    if (overlay) {
      fputs("\x1BvG 0 0 0 81 1030 1\r", stdout);
      for (int n = 0; n < CARD_W; n++) {
        pack_k_row(CARD_W - 1 - n, row, 1);
        fputs("\x1BvO", stdout);
        put_escaped(row, K_BYTES);
        putchar('\r');
      }
    }
    fputs("\x1BIS 0\r\x1BIS 1\r\x1BIS 2\r", stdout);
    if (overlay) fputs("\x1BI 10\r\x1BIV 1\r\x1BMO\r", stdout);
    else fputs("\x1BI\r", stdout); /* blank = print and eject */
  } else {
    fputs("\x1BI\r", stdout); /* blank = print and eject */
  }
  fflush(stdout);
}

int main(int argc, char *argv[])
{
  int fd = 0, copies, page = 0;
  cups_raster_t *ras;
  cups_page_header2_t h;
  cups_option_t *options = NULL;
  int num_options;
  ppd_file_t *ppd = NULL;

  setbuf(stderr, NULL);
  if (argc < 6 || argc > 7) {
    fprintf(stderr, "Usage: %s job-id user title copies options [file]\n", argv[0]);
    return 1;
  }
  if (argc == 7 && (fd = open(argv[6], O_RDONLY)) < 0) {
    perror("ERROR: Unable to open raster file");
    return 1;
  }
  signal(SIGTERM, on_term);

  copies = atoi(argv[4]);
  if (copies < 1) copies = 1;

  num_options = cupsParseOptions(argv[5], 0, &options);
  if (getenv("PPD") && (ppd = ppdOpenFile(getenv("PPD"))) != NULL) {
    ppdMarkDefaults(ppd);
    cupsMarkOptions(ppd, num_options, options);
  }

  int ribbon_k = !strcasecmp(get_opt("zeRibbon", num_options, options, ppd, "YMCKO"), "K");
  int overlay = !strcasecmp(get_opt("zeOverlay", num_options, options, ppd, "On"), "On");
  const char *bx = get_opt("zeBlackExtract", num_options, options, ppd, "Medium");
  int kthresh = !strcasecmp(bx, "Off") ? -1 : !strcasecmp(bx, "Low") ? 16
              : !strcasecmp(bx, "High") ? 80 : 48;
  int mono_thresh = atoi(get_opt("zeMonoThreshold", num_options, options, ppd, "128"));
  if (mono_thresh <= 0 || mono_thresh > 255) mono_thresh = 128;

  fprintf(stderr, "DEBUG: ribbon=%s overlay=%d blackextract=%d mono_thresh=%d copies=%d\n",
          ribbon_k ? "K" : "YMCKO", overlay, kthresh, mono_thresh, copies);

  ras = cupsRasterOpen(fd, CUPS_RASTER_READ);
  unsigned char *line = NULL;

  while (!canceled && cupsRasterReadHeader2(ras, &h)) {
    page++;
    fprintf(stderr, "DEBUG: page %d: %ux%u bpc=%u bpp=%u cs=%u order=%u\n", page,
            h.cupsWidth, h.cupsHeight, h.cupsBitsPerColor, h.cupsBitsPerPixel,
            h.cupsColorSpace, h.cupsColorOrder);
    if (h.cupsBitsPerColor != 8 || h.cupsColorOrder != CUPS_ORDER_CHUNKED ||
        (h.cupsBitsPerPixel != 24 && h.cupsBitsPerPixel != 8)) {
      fputs("ERROR: Unsupported raster format (need 8-bit RGB or gray)\n", stderr);
      break;
    }
    int gray = (h.cupsBitsPerPixel == 8);
    int whiteval = (gray && h.cupsColorSpace == CUPS_CSPACE_K) ? 0 : 255;
    free(line);
    line = malloc(h.cupsBytesPerLine);
    memset(img, 255, sizeof(img));

    for (unsigned y = 0; y < h.cupsHeight && !canceled; y++) {
      if (cupsRasterReadPixels(ras, line, h.cupsBytesPerLine) < 1) break;
      if (y >= CARD_H) continue;
      for (unsigned x = 0; x < h.cupsWidth && x < CARD_W; x++) {
        if (gray) {
          unsigned char g = line[x];
          if (whiteval == 0) g = 255 - g;
          img[y][x][0] = img[y][x][1] = img[y][x][2] = g;
        } else {
          memcpy(img[y][x], line + 3 * x, 3);
        }
      }
      if ((y & 63) == 0)
        fprintf(stderr, "INFO: Rendering card %d (%u%%)\n", page, 100 * y / h.cupsHeight);
    }

    for (int y = 0; y < CARD_H; y++)
      for (int x = 0; x < CARD_W; x++) {
        unsigned char *p = img[y][x];
        if (ribbon_k) {
          int lum = (299 * p[0] + 587 * p[1] + 114 * p[2]) / 1000;
          kmask[y][x] = lum < mono_thresh;
        } else {
          kmask[y][x] = kthresh >= 0 && p[0] <= kthresh && p[1] <= kthresh && p[2] <= kthresh;
        }
      }

    for (int c = 0; c < copies && !canceled; c++) {
      fprintf(stderr, "PAGE: %d 1\n", page);
      fprintf(stderr, "INFO: Printing card %d copy %d/%d\n", page, c + 1, copies);
      emit_card(ribbon_k, overlay);
    }
  }

  free(line);
  cupsRasterClose(ras);
  if (fd) close(fd);
  if (ppd) ppdClose(ppd);
  cupsFreeOptions(num_options, options);

  if (page == 0) { fputs("ERROR: No pages found\n", stderr); return 1; }
  fputs("INFO: Ready to print.\n", stderr);
  return 0;
}
