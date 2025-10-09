# Snake Game for DTEK-V

A two-player Snake game implementation for the DTEK-V board with VGA output and 7-segment score displays.

## Features
- Single-player and multiplayer modes
- VGA graphics output (320x240)
- 7-segment display score tracking
- Switch-based controls

## Prerequisites
- DTEK-V toolchain installed
- `dtekv-run` utility
- JTAG connection to DTEK-V board
- Make

## Compilation

```bash
make
```

This generates `main.bin` which can be loaded onto the board or onto an emulator such as [this one](https://dtekv.fritiof.dev/)

## Running on Board

```bash
dtekv-run main.bin
```

**Note for Windows/WSL users:** You may need to use `usbipd` to pass through the USB JTAG connection to WSL first.

## Controls

### Menu
- **SW0**: Toggle between 1-player and 2-player mode
- **BTN0**: Start game

### Single Player
- **SW0-1**: Direction control (00=Up, 01=Down, 10=Left, 11=Right)

### Multiplayer
- **Player 1 (Right)**: SW0-1
- **Player 2 (Left)**: SW8-9

## Game Objective
Eat green food squares to grow your snake as big as possible. Avoid walls and colliding with yourself (or the other player in multiplayer mode).