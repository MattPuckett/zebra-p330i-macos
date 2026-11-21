# Zebra P330i driver for macOS (Apple Silicon)

A CUPS driver that lets a **Zebra P330i** ID card printer print from modern macOS, including Apple Silicon Macs. Zebra never shipped a macOS driver for the P330i, and the printer reached end of support years ago.

It is a small native C filter (`rastertoepcl`) plus a PPD. Once installed, the printer shows up like any other printer and you can print from any app.

**Status:** tested on a P330i (firmware V2.00.00) with a YMCKO ribbon, from macOS 27 on an M3 Mac over USB. Full-color cards with resin black and full-card overlay print correctly and eject.

## Requirements

- macOS (Apple Silicon or Intel)
- Xcode Command Line Tools for `clang`: `xcode-select --install`
- Zebra P330i connected over USB
- A genuine **i Series** ribbon. The P330i reads the ribbon's RFID tag and won't print color without it.

## Install

```bash
git clone https://github.com/MattPuckett/zebra-p330i-macos.git
cd zebra-p330i-macos
sudo ./install.sh
```

The script builds the filter and installs it to `/Library/Printers/Zebra/P330i/`. It also installs the PPD and adds a queue named `Zebra_P330i` for the printer it finds on USB. Set `QUEUE=Name` to call the queue something else.

To remove the driver: `sudo ./uninstall.sh`

## Printing

Print from any app to **Zebra P330i Card Printer**, with the paper size set to **CR80 ID Card (full bleed)**.

The page is the printer's full print area: 1030 × 646 dots at 300 dpi, which is 247.2 × 155.04 pt. That's slightly larger than a CR80 card, so the design bleeds off the edges. Design in landscape at that size and the pixels map 1:1 to printer dots.

From the command line:

```bash
lp -d Zebra_P330i card.pdf
lp -d Zebra_P330i -n 5 card.pdf                  # 5 copies
lp -d Zebra_P330i -o zeBlackExtract=Off card.pdf
```

### Options

These are under **Printer Features** in the print dialog, or can be passed with `-o`:

| Option | Values | Default | Notes |
|---|---|---|---|
| `zeRibbon` | `YMCKO`, `K` | `YMCKO` | `K` = monochrome resin ribbon |
| `zeOverlay` | `On`, `Off` | `On` | Full-card clear overlay (YMCKO only) |
| `zeBlackExtract` | `Off`, `Low`, `Medium`, `High` | `Medium` | Sends near-black pixels to the resin K panel for crisp text and barcodes |
| `zeMonoThreshold` | `96`, `128`, `160`, `192` | `128` | Black cut-off when printing with a `K` ribbon |

> **Tested path:** YMCKO with overlay **On**. The overlay-off and `K`-ribbon paths end with a different eject command (a blank `I`), and that hasn't been verified on hardware yet. Reports welcome.

### Test card

```bash
python3 -m pip install pillow
python3 tools/make_testcard.py
lp -d Zebra_P330i testcard.pdf
```

The test card checks:
- **Orientation:** TOP LEFT and BOTTOM RIGHT labels.
- **Edge registration:** a border around the card.
- **Panel mapping:** C, M, Y, R, G, B and K swatches.
- **Resin black:** the title, the triangle and the K swatch.
- **Tonal range:** color and gray gradients.

## How it works

The filter converts CUPS RGB raster into EPCL, the Eltron/Zebra card printer language:

- **Yellow, magenta and cyan** panels: uncompressed `GS` blocks, 10 × 2 tiles of 103 × 323 dots. Three reserved byte values in the image data are nudged one step to avoid the printer's parser.
- **Black resin** panel: 1-bit `G`/`O` rows.
- **Overlay**: an all-on `vG`/`vO` bitmap.
- **Print and eject**: `IS 0/1/2`, `I 10`, `IV 1`, `MO`.

The encoding is ported from **[pyepcl](https://github.com/BryceSchroeder/pyepcl)** by Bryce Schroeder, which was tested on the P430i. Its output was verified byte-identical against pyepcl. Thanks, Bryce.

## Troubleshooting

Most problems with a long-idle P330i are hardware, not the driver. **Always start with the printer's own self-test**, which involves no computer at all:

1. Switch the power off.
2. Hold the front panel button while switching the power back on.
3. Release the button when the LCD shows **SELF TEST**.

If the self-test card isn't clean and doesn't eject, fix the printer first.

| Symptom | Likely cause / fix |
|---|---|
| **OUT OF RIBBON** with film left, or prints yellow then stops | Most likely the **ribbon take-up belt** has worn and slips (see below). Other causes: an expired ribbon, the ribbon loaded wrong (it must come off the **top** of the supply spindle and feed to the **top** of the take-up spindle), or a dirty ribbon sensor reflector. |
| Solid black or yellow blocks with a sharp vertical edge | The ribbon is out of position, so a full-coverage pass lands on the wrong color panel. Same causes as above. |
| Colors on the wrong panels (black text prints yellow, magenta prints cyan) | Same: ribbon positioning. A card stuck in the path and overprinted by several jobs looks like this too. |
| Card doesn't eject; next job jams | Power off, raise the print head and clear the **whole** card path, from the feeder through the head to the exit rollers. |
| MECHANICAL ERROR | Card jam or misfeed (check the card thickness lever), or the ribbon sensor needs calibrating. |

### Ribbon take-up belt (the usual culprit)

The take-up spool is driven by a small rubber O-ring belt, Zebra part **105909-038**, from the ribbon motor pulley up to the ribbon pulley. After years of sitting it hardens and slips.

- To get to it, remove the outer cover. The belt is on the side mechanism, next to the gear train.
- The service manual's section 5.27 (*Replacing the Ribbon O-Ring Drive Belt*) gives the routing:
  - Seat the belt on the ribbon pulley first.
  - Pass it **around both sides of the Upper Cam**.
  - Roll it onto the ribbon motor pulley.
  - It must run **inside the shield**.
- **A standard O-ring works as the replacement.** On the printer this was written for, a **63.2 mm ID × 2.4 mm** FKM (Viton) O-ring fixed it. Larger sizes (70.2 mm and 77.2 mm ID) slipped.

### Ribbon shelf life

Card printer ribbons are rated for about **a year** in storage, kept cool and dry. A ribbon several years old can print badly and fail to sync. Rewinding a used ribbon also confuses its panel count.

### Ribbon sensor calibration

Zebra's fix for ribbon-sensing problems on this model has two steps:

1. Clean the silver reflective strip at the bottom of the card path, under the sensor's red light on the right side of the print head.
2. Recalibrate.

The Windows driver's `!SA` command can be sent from macOS as a raw job. Do it with the ribbon loaded, the head latched and the card path empty:

```bash
printf '\033!SA\r' > /tmp/cal.epcl
lp -d Zebra_P330i -o raw /tmp/cal.epcl
```

## Limitations

- Each card is about 2 MB of uncompressed data, so allow 30–60 s per card over USB.
- No magnetic stripe or smart card encoding.
- No use of the printer's built-in fonts or barcodes. Everything prints as a raster image, so design barcodes into your artwork.
- CUPS lists PPD-based drivers as deprecated, but they still work on current macOS.

## License

The repo is split-licensed:

| Files | License |
|---|---|
| `rastertoepcl.c` (the CUPS filter) | **LGPL-3.0-or-later**: see `COPYING.LESSER` and `COPYING`. Its EPCL encoding is ported from [pyepcl](https://github.com/BryceSchroeder/pyepcl) (LGPL-3.0), so it carries pyepcl's license. |
| Everything else: PPD, install/uninstall scripts, test card generator, docs | **MIT**: see `LICENSE` |

In practice, you can use, install and redistribute the driver freely. The LGPL only asks that if you **modify `rastertoepcl.c` and distribute it**, you share those changes under the same license.
