# NUSspli
Install content directly from the Nintendo Update Servers to your Wii U.

[![build](https://github.com/V10lator/NUSspli/actions/workflows/master.yml/badge.svg)](https://github.com/V10lator/NUSspli/actions/workflows/master.yml)

# Features
- Download titles from Nintendos servers (NUS).
- Install the downloaded titles to external or internal storage.
- Includes a keygen!
- Create fake tickets at will or if not found.
- Shows the download speed.
- On screen keyboard.
- Can download anything available on the NUS.
- Custom folder names for downloaded titles.
- Auto update.

# Install
To install the app, download and unzip the contents of the [latest release](https://github.com/V10lator/NUSspli/releases) and depending on how you will run the app, follow the next steps:

### Aroma
- Move NUSspli.wuhb to (SD:/wiiu/apps/).
- Run it from the HOME Menu.

### Channel
- Move the folder to (SD:/install/) and install it with WUPInstaller.
- Run it from the HOME Menu.

# Building
- Use `docker build -t nussplibuilder .` to build the container
- Use `docker run --rm -v ${PWD}:/project nussplibuilder python3 build.py` to build NUSspli

# Info
this NUSspli fork is based on [NUSsspli](https://github.com/V10lator/NUSspli) by V10lator.
