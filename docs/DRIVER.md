There are some reports that games will work better with the Thrustmaster Windows
drivers installed.

# Installing the Windows drivers inside a proton prefix

1. Install [protontricks](https://github.com/Matoking/protontricks).

2. Download the
   [Thrustmaster drivers](https://support.thrustmaster.com/en/product/t300rs-en/).

3. Look up the ID of your game with `protontricks -s NAME`. For example:
   ```shell
   $ protontricks -s Dirt
   Found the following games:
   DiRT 3 Complete Edition (321040)
   DiRT 4 (421020)
   DiRT Rally (310560)
   DiRT Rally 2.0 (690790)

   To run Protontricks for the chosen game, run: $ protontricks APPID COMMAND

   NOTE: A game must be launched at least once before Protontricks can find the
   game.
   ```

4. Run the driver installer with protontricks.
   ```shell
   protontricks -c 'wine ~/Downloads/DRIVER_INSTALL_EXE' APPID
   ```

   Dirt 4 for example:
   ```shell
   protontricks -c 'wine ~/Downloads/2022_TTRS_1.exe' 421020
   ```

Note that `protontricks` may change the working directory, so relative paths may
end up not working as expected.

# Installing the Windows drivers manually

Assuming you've already downloaded the installer:

```shell
WINE_PREFIX=/path/to/prefix wine ./DRIVER_INSTALL_EXE
```

[Lutris](https://lutris.net/) is an excellent way to keep track of which
non-steam games exist in which prefixes, and allows you to run executables in
prefixes from a GUI if that's more your thing.
