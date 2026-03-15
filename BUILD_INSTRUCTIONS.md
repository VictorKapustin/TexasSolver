# Building TexasSolver GUI

This guide explains how to build the `TexasSolverGui.exe` from the command line after making changes to the source code.

## Requirements
- **Qt 5.15.2 (MinGW 64-bit)** or similar installed.
- **Qt Command Prompt** (e.g., "Qt 5.15.2 (MinGW 8.1.0 64-bit)").
- Ensure the `PATH` environment variable contains the Qt `bin` directory and MinGW `bin` directory.

## Build Steps

1. Open the **Qt Command Prompt** (or a standard terminal where Qt and MinGW are in the `PATH`).
2. Navigate to the project root directory:
   ```cmd
   cd c:\TexasSolver
   ```
3. Generate the Makefile in release configuration using `qmake`:
   ```cmd
   qmake TexasSolverGui.pro "CONFIG+=release"
   ```
4. Build the executable using `mingw32-make`:
   ```cmd
   mingw32-make release
   ```
   *(Note: This might take a few minutes as it recompiles the source code).*

5. Once completed successfully, the final `TexasSolverGui.exe` will be located in the `release\` folder.

## Deployment (Standalone Package)

To distribute the executable or run it without Qt installed:
1. Create a `deploy` folder (if it doesn't exist).
2. Copy `release\TexasSolverGui.exe` to the `deploy` folder.
3. Run the Qt deployment tool to generate the necessary `.dll` files next to the executable:
   ```cmd
   windeployqt deploy\TexasSolverGui.exe
   ```
4. Because the solver uses OpenMP (`-fopenmp`), `windeployqt` might miss the GNU OpenMP library. You must manually copy it from your MinGW `bin` directory:
   ```cmd
   copy C:\Qt\Tools\mingw810_64\bin\libgomp-1.dll deploy\
   ```

You can now run `TexasSolverGui.exe` from the `deploy` folder.
