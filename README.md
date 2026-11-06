# MicroWeb DOS web browser
MicroWeb is a web browser for DOS that runs as a 16-bit real mode application and is designed to run on minimal hardware.

![Screenshot](screenshot.gif)

## Minimum requirements
To run you will need:
* Intel 8088 or compatible CPU
* CGA, EGA, VGA, Hercules or Tandy 1000 compatible graphics
* A network interface (it is possible to use your machine's serial port with the EtherSLIP driver)
* A mouse is desirable but not 100% required
* 640K RAM is desirable but can run with less
* Extra memory via XMS or EMS can be used if available and is recommended for loading heavier web pages and images

## Limitations
* HTTP only (See HTTPS limitations below)
* GIF, baseline JPEG and non-interlaced PNG images are rendered. PNG supports palette and grayscale (1/2/4/8-bit), RGB, grayscale-alpha and RGBA (8-bit), all five row filters, and transparency.
* Partial PNG alpha uses ordered coverage in color modes; monochrome composites against the page background. Adam7 and 16-bit PNG pixels are unsupported, while their dimensions are still loaded for layout.
* PNG rows are limited to 4096 bytes before filtering, and rendered images to 1024 pixels wide by 4096 high, subject to available RAM. A bounded workspace of about 40K is shared with the other image decoders. The local `examples/png.htm` sample needs its accompanying PNG files and `tandy.gif`.
* No CSS or Javascript
* Large or complex pages may be truncated if there is not enough RAM available
* Mouse cursor is currently not visible in Hercules mode

Mouse wheels scroll three text lines per notch when the installed DOS mouse
driver supports CuteMouse WheelAPI 1.0. Older mouse drivers continue to work
without wheel scrolling. In DOSBox-X, disable wheel-to-key mapping with
`mouse_wheel_key=0` to use its native wheel API.

## Keyboard shortcuts
| Key				           | Shortcut                                                                    |
|-------------------|-----------------------------------------------------------------------------|
| Escape            | Exit                                                                        |
| F2                | Invert screen colours in monochrome modes (useful for old LCD displays)     |
| F3                | Toggle title bar / status bar visibility                                    |
| F5                | Reload page                                                                 |
| F6 / Ctrl+L       | Focus address bar                                                           |
| Tab / Shift+Tab   | Cycle through selectable page elements                                      |
| Enter             | Follow link / press button                                                  |
| Cursor up/down    | Scroll page                                                                 |
| Page up/down      | Scroll page in large increments                                             |
| Home              | Jump to start of page                                                       |
| End               | Jump to end of page                                                         |

## Supported video modes
MicroWeb supports a wide range of different video modes. You will be asked on startup to select a mode and one will be suggested based on your detected hardware.
* 640x200 monochrome (CGA)				
* 640x200 inverse monochrome (Palmtop CGA)
* 320x200 4 colours (CGA)
* 320x200 16 colours (Composite CGA)
* 640x200 16 colours (EGA)		
* 640x350 monochrome (EGA)				
* 640x350 16 colours (EGA)				
* 640x480 monochrome (VGA)				
* 640x480 16 colours (VGA)				
* 320x200 256 colours (VGA)				
* 720x348 monochrome (Hercules)		
* 640x400 monochrome (Olivetti M24)
* 640x400 monochrome (Toshiba T3100)
* 240x128 monochrome (HP 95LX)
* 640x200 16 colours (Amstrad PC1512)
* 320x200 16 colours (Tandy 1000, native RGBI)
* 640x200 16 colours (Tandy Video II / TGA-II, experimental)

On a normal 640K Tandy 1000 EX/HX, run `TANDY` (or `TANDY http://68k.news`)
for native 320x200 16-colour graphics. Keep `TANDY.BAT`, `VRESERVE.COM`,
`MICROWEB.EXE`, and the `.dat` files together. The launcher reserves the
16K of video RAM that the Tandy BIOS otherwise leaves available to DOS,
then selects mode `t`. The reservation uses about 18K and stays until reboot.
MicroWeb checks it before entering BIOS mode 09h, so launching directly
with `-video=t` without protected video memory refuses safely.

This mode uses the Tandy's four-bank packed-pixel video memory and RGBI
palette. The supplied helper supports DOS 3 or newer and the original
640K layout (BIOS reports 624K). It refuses other layouts or occupied video
memory instead of taking another program's allocation. GIFs that already use
the exact 16 RGBI colours keep those colours without added dithering. The
example `examples/tandy.htm` includes a native-palette GIF; for local pages
with relative links or images, supply a full path such as
`TANDY C:/MICROWEB/EXAMPLES/TANDY.HTM`.

For the later **TGA-II / Video II** hardware, start from 80-column colour
DOS text mode and use `MICROWEB -video=u` (optionally followed by a URL or
full local path). This selects a separate 640x200, 16-colour RGBI backend
with a linear A000 framebuffer and software cursor. Existing mode letters,
including EX/HX mode `t`, are unchanged. BIOS mode 0Ah is only four colours;
the new backend programs the Video II registers directly rather than
mistaking 0Ah for a 16-colour mode.

The TGA-II path is **experimental and not yet validated on real SL/TL/RL
hardware or a Video II emulator**. It requires the Tandy Video II firmware
signature/configuration table and refuses VGA/RLX, original Tandy hardware,
and generic DOSBox Tandy emulation. Its memory guard verifies every paragraph
of the entire 64K aperture against a freshly tagged DOS-owned allocation,
reserves that block for the session, and releases it after restoring text
mode. It does not need `VRESERVE.COM` or a TSR. Only configurations where the
lower 64K video page can be fully owned this way are accepted. Occupied,
partially mapped, dedicated/expanded, and unknown video-memory layouts refuse
safely instead of assuming that RAM is available. Try a standard CGA mode
if the TGA-II ownership check refuses your configuration.

Host regressions cover the production identification/reservation/register
code and complete 640x200 rendering, including scrolling, images, text,
cursor restoration and memory guards (`tests/run-tga2.sh` and
`tests/run-tga2-surface.sh`). These simulated I/O tests do not establish
physical timing, monitor output or actual mouse-driver compatibility.

## Command line options
You can use a URL as an argument to load a specific page on startup. This can also be a path to a local html file. 

| Option    | Effect
|-----------|-------
| -invert   | Start monochrome modes with inverted screen colours (useful for some LCD monitors)
| -noems    | Disable EMS memory usage
| -noxms    | Disable XMS memory usage
| -noimages | Disables image decoders - useful for very low memory setups
| -useswap  | Experimental disk swapping mode to increase available memory
| -video=#  | Skip the video mode selection menu and choose a mode ahead of time, e.g. -video=a
 
For example `MICROWEB -noems http://68k.news` will load the 68k.news website on startup but disable the EMS routines

## HTTPS limitations
TLS encryption is currently not supported which means that only HTTP servers can be accessed directly. There are some options for HTTPS sites:
* Use a proxy server such as [retro-proxy](https://github.com/DrKylstein/retro-proxy) which converts HTTPS to HTTP. You can configure a proxy server by setting the HTTP_PROXY environment variable before running MicroWeb. e.g. `SET HTTP_PROXY=192.168.0.50:8000`
* Use the [FrogFind!](http://www.frogfind.com) web service to view a stripped down version of a site. If MicroWeb is redirected to an HTTPS site then it will generate a FrogFind link for your convenience.

## Getting started
Check out the [releases page](https://github.com/jhhoward/MicroWeb/releases) which will include a pre-built binary. Also available are FreeDOS boot disk images for 360K and 720K floppies, which are configured to work with a NE2000 network adapter. These boot images can be used in an emulator such as [PCem](https://pcem-emulator.co.uk/).

## Network setup
MicroWeb uses Michael Brutman's [mTCP networking library](http://www.brutmanlabs.org/mTCP/) for the network stack. You will need a DOS packet driver relevant to your network interface. You can read more about configuring DOS networking [here](http://brutmanlabs.org/Dos_Networking/dos_networking.html)

## Build instructions
To build you will need the [OpenWatcom 1.9 C++ compiler](https://sourceforge.net/projects/openwatcom/files/open-watcom-1.9/). 
On Windows, use OpenWatcom's wmake to build the makefile in the project/DOS folder.

On Linux, install the Linux-hosted [Open Watcom v2 tools](https://github.com/open-watcom/open-watcom-v2/releases), then run from the repository root:

```sh
WATCOM=/path/to/watcom tools/build-dos.sh
```

This requires a native C++ compiler for the mTCP runtime patcher. The executable, `VRESERVE.COM`, and `TANDY.BAT` are written to `build/dos`; pass an optional output directory to the script to use a different location. The Linux build uses the same large memory model, DOS overlays, and `-0` (8086/8088) instruction target as the Windows build. Copy the executable and the repository's `.dat` files together when running it.

