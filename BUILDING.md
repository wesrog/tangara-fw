# Building and flashing

NOTE: The development environment for Linux requires running external pre-compiled binaries linked against GLIBC. This means they will not run on Linux distros that use alternative libc (e.g. musl).

0. For Linux, the following packages are needed to run the development env. Note that package names are from Debian, and may vary on other distros.

- cmake
- libusb-1.0
- python3-venv

1. Make sure you've got all of the submodules in this repo correctly initialised:

```
git submodule update --init --recursive
```

2. If this is your first time setting up the repo, then you will need to install
the ESP-IDF tools. You can consult the [ESP-IDF docs](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/linux-macos-setup.html)
for more detailed instructions, but the TL;DR is that you'll want to run
something like this:

```
./lib/esp-idf/install.sh esp32
```

3. As a final piece of setup, you will need to source the env file in this repo
to correctly set up your environment for building.

```
. ./.env
```

There is also a `.env.fish` for fish users.

4. You can now build the project using `idf.py build`. Or to flash the project
onto your board, something like:

```
idf.py -p /dev/serial/by-id/usb-cool_tech_zone_Tangara_* -b 1000000 flash
```

(give or take the correct serial port)

### Faster flashing while developing

`idf.py flash` rewrites every image on the device (about 7.5 MB, which takes
80 to 100 seconds) even if only one Lua file changed. For test builds you can
use `tools/quickflash.py` instead. It builds the project, asks the device for a
checksum of each 64 KiB chunk of flash, and only writes the chunks that differ:

```
tools/quickflash.py -p /dev/serial/by-id/usb-cool_tech_zone_Tangara_*
```

When nothing has changed this takes about 13 seconds, most of which is spent on
the checksums. It compares against what is actually on the device, so it's
safe to mix with `idf.py flash`. If the comparison fails for any reason, it
writes everything. It refuses to start while another esptool, `idf.py flash`
or `idf.py monitor` is running, since two programs using the port at once
corrupt the flash write. Use `--no-build` to skip the build, `--full` to
write everything, and `-b` to change the baud rate (default 1500000; 2000000
also works, 3000000 doesn't).

If `idf.py flash` complains about the serial port not being readable, you'll need to add your user account to either the `dialout` or `uucp` groups, try `dialout` first.  After, logout of your current session then try to flash again.

# Running tests

See `TESTING.md` for an overview of how to write and run our on-device test suite.

# VSCode setup

When using the Espressif IDF extension, you may want to set the following in your settings.json file:
```
  "idf.espIdfPath": "${workspaceFolder}/lib/esp-idf",
  "idf.espIdfPathWin": "${workspaceFolder}/lib/esp-idf"
```

# LSP (clangd) setup

A regular build will generate `build/compile_commands.json`, which clangd will
automatically pick up. However, there are a couple of additional steps to get
everything to play nicely.

First, create a `.clangd` file in the this directory, with contents like:

```
CompileFlags:
  Add: [
    -D__XTENSA__,
    --target=mipsel-linux-gnu,
  ]
  Remove: [
    -fno-tree-switch-conversion,
    -mlongcalls,
    -fstrict-volatile-bitfields,
  ]
```

You may need to tweak the `target` flag until `clangd` is happy to build.

If you get errors involving missing C++ includes, then you may need to edit
your editor's LSP invocation to include `--query-driver=**`.

You should then get proper LSP integration via clangd.

# ESP-IDF configuration

Espressif exposes a large collection of configuration options[1] for its
framework; you can use `idf.py menuconfig` to generate a custom `sdkconfig`
file, eg. to change the logging level.

To avoid needing to select the same set of options every time you regenerate
the sdkconfig, you can also set some defaults in `sdkconfig.local`; this is not
tracked in git, and is ideal for local / per-checkout changes.

1. https://docs.espressif.com/projects/esp-idf/en/release-v3.3/api-reference/kconfig.html
