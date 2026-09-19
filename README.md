# RandomBG
A watchface for the Pebble time 2 Smartwatch. Designed with rotating images, bold time, and date, battery, and BT connection status.  C code is AI slop, but the images were processed in Gimp.

## Features

- 8 user-replaceable background images, rotate randomly (Number of img's can be varied, see below)
- Time (large mono font) Simple day date
- Battery meter bar along the bottom edge
- Bluetooth disconnect indicator (red BT badge in the bar)
- Two background-change modes: time interval or wrist flick, selectable from app settings

## Building

Originally built w/ CloudPebble or you use download the SDK & tools yourself:

Requires [pebble-tool](https://developer.rebble.io/developer.pebble.com/sdk/install/index.html) and the Pebble SDK.

```bash
pebble build
pebble install --phone <your-phone-ip>
```

## Replacing the background images

1. Create 8 PNG files named `bg_0.png` through `bg_7.png`
2. Place them in `resources/images/`
3. Rebuild and reinstall

**Image requirements:**

| Property | Value |
|---|---|
| Dimensions | 200 × 228 px |
| Colour depth | 8-bit palette or 24-bit RGB |
| Alpha channel | Not required (omit for best results) |
| Format | PNG |

The SDK converts your images to the Pebble 64-colour palette at build time.
Images do not need to be pre-dithered but dithering before import gives more
predictable results.

## Reducing the number of images

Edit `NUM_BACKGROUNDS` in `src/main.c`, shorten the `s_bg_resource_ids` array
to match, and remove the unused entries from `package.json`.

## Settings

Open the Pebble app → your watchface → gear icon.

| Setting | Options |
|---|---|
| Change image on | Time interval (1–60 min) / Wrist flick |

Settings are saved on the watch and persist across reboots.



## Version history

- **1.0.0** — Initial release


**This is my first pebble face so please provide adequate feedback!**
Originally built w/ CloudPebble.
