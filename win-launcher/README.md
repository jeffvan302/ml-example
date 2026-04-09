# Python Launch Helper

A Windows desktop application that manages Python runtime environments and PyTorch installations for machine learning projects.

## Purpose

This launcher simplifies the setup and management of Python environments with PyTorch. Instead of manually configuring Python, conda environments, and PyTorch wheels for different hardware accelerators, the launcher automates the process with sensible defaults and intelligent detection.

Key scenarios:
- Setting up a new development machine with the correct Python and PyTorch stack
- Switching between CUDA, ROCm, XPU, and CPU PyTorch builds based on available hardware
- Managing portable Python runtimes that don't require system-wide installation
- Creating isolated named environments for different projects

## Features

### Python Environment Management
- **Portable runtime**: Downloads and manages Python in a local `runtime/` folder — no system-wide installation required
- **Existing Python**: Uses a Python installation already present on the machine
- **Machine install**: Downloads and installs Python from python.org if needed
- **Conda environments**: Creates named conda environments when Miniconda is available

### PyTorch Accelerator Support
Automatically detects available hardware and offers the appropriate PyTorch build:
- **NVIDIA CUDA**: For NVIDIA GPUs via cu129 wheels
- **Intel XPU**: For Intel Arc GPUs via official PyTorch XPU wheels
- **AMD ROCm**: For AMD GPUs via ROCm wheels (requires ROCm markers)
- **CPU**: Default fallback for systems without GPU acceleration

### Intelligent Defaults
- Auto-detects available GPU hardware and pre-selects the appropriate accelerator
- Remembers settings in `run.cfg` after first successful launch
- CPU is always the default selection for safety

### Miniconda Integration
- Suggests installing Miniconda if no conda installation is detected
- One-click Miniconda installation with automatic environment setup
- Installs to project-local `miniconda3/` directory

### Dark Terminal Theme
The launcher uses a dark theme inspired by terminal/matrix aesthetics:
- Dark gray background (#1E1E1E) matching the splash screen
- High-contrast light text for readability
- Green accent color for visual elements
- Modern Segoe UI font for clean typography

## Building

### Prerequisites
- Visual Studio 2022 with C++ build tools
- Windows SDK

### Build Command

```
rebuild-launcher.bat
```

This compiles `src/main.cpp` with resources (`src/resources.rc`) and produces `launch.exe` in the project root. The build requires:
- Visual Studio 2022 Developer Command Prompt
- Windows SDK with resource compiler (rc.exe)

### Output
- `launch.exe` — The compiled launcher executable
- `src/resources.rc` — Resource definitions (icon)
- `src/app.ico` — Application icon (extracted from splash.png)

## Project Structure

```
tester/
├── launch.exe              # Compiled launcher
├── rebuild-launcher.bat    # Build script
├── src/
│   ├── main.cpp            # Main application source
│   ├── splash.png          # Splash screen image
│   ├── resources.rc        # Resource definitions
│   └── app.ico             # Application icon
├── runtime/                # Created on first run
│   ├── python-base/        # Portable Python installation
│   ├── python/             # Virtual environment
│   ├── cache/              # Downloaded installers
│   └── logs/               # Operation logs
└── run.cfg                 # Saved configuration (created after first launch)
```

## Usage

1. Place `launch.exe` in your project root alongside `requirements.txt` (optional)
2. Run `launch.exe`
3. Select your Python source and environment type
4. Choose the PyTorch accelerator matching your hardware (CPU is default)
5. Select a launch target (Python script or custom command)
6. Enable "Launch in terminal" if your script needs console output (checked by default)
7. Click "Save && Launch"

The launcher will:
- Download and set up Python if using portable runtime
- Install the selected PyTorch variant
- Install packages from `requirements.txt` if present
- Launch your script with the configured environment (with or without terminal depending on the checkbox)

## Configuration

### run.cfg
After the first successful launch, settings are persisted to `run.cfg` in the project root. This file records:
- Selected Python version and source
- Environment type and path
- PyTorch accelerator choice
- Launch target

Delete `run.cfg` to reset and reconfigure from scratch.

### requirements.txt
Place a `requirements.txt` file next to `launch.exe` to have packages automatically installed. The launcher filters torch packages and uses the configured PyTorch index URL instead.

### Log Files
Operation logs are written to `runtime/logs/launcher.log` for debugging issues.
