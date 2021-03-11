# Building on Windows

I suppose if you had WSL installed, you would have already followed the Linux instructions :).

This will guide you through the installation of [MSYS2](https://www.msys2.org/) which will provide a Linux-like environment on Windows. 

Afaik Windows nowadays is 64 bit so these steps describe the 64 bit path. If you happen to be using 32 bit Windows, just use the matching versions of MinGW terminal / MSYS2 packages.

## Steps

1. Install [MSYS2](https://www.msys2.org/) from https://www.msys2.org/.
    If you already have MSYS2 installed, you can skip this step.

    At the end of the installation, you will be asked if you want to open
    an MSYS2 prompt. Please choose no.

2. Open an MSYS2 MinGW 64 bit prompt. This should open a terminal window.

3. Update MSYS2 as the installer may be outdated. From the terminal, run
    ```
    pacman -Syu
    ```

    If your installation of MSYS2 is very outdated, you may need to run the command multiple times. `pacman` will tell you if it needs to be run again.

4. Install the tools to fetch and build the project.
    ```
    pacman -S git make python3 mingw-w64-x86_64-arm-none-eabi-toolchain mingw64/mingw-w64-x86_64-dfu-util
    ```

You should now be set up to build the software from MSYS2 using the instructions on the project [README](../README.md).


