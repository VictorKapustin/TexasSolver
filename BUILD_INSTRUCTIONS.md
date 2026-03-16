# Building TexasSolver GUI

This guide explains how to build the `TexasSolverGui.exe` from the command line after making changes to the source code.

## Requirements
- **Qt 5.15.2 (MinGW 64-bit)** or similar installed.
- **Qt Command Prompt** (e.g., "Qt 5.15.2 (MinGW 8.1.0 64-bit)").
- Ensure the `PATH` environment variable contains the Qt `bin` directory and MinGW `bin` directory.

## Build Steps

1. Open a terminal and ensure Qt and MinGW are in your `PATH`:
   ```powershell
   $env:PATH = "C:\Qt\5.15.2\mingw81_64\bin;C:\Qt\Tools\mingw810_64\bin;" + $env:PATH
   ```
2. Navigate to the project root directory:
   ```cmd
   cd c:\TexasSolver
   ```
3. Generate the Makefile in release configuration:
   ```cmd
   qmake TexasSolverGui.pro "CONFIG+=release"
   ```
4. Build the executable using multiple cores (e.g., `-j8`) for speed:
   ```cmd
   mingw32-make release -j8
   ```

Release uses `-O3 -march=native` by default.  
To enable LTO explicitly, add `enable_lto` in qmake config:
```cmd
qmake TexasSolverConsole.pro "CONFIG+=release enable_lto"
```
If your MinGW linker reports widespread `multiple definition` errors, remove `enable_lto` and keep `-O3 -march=native` for that build.

### Building CLI (Console) Version

If you need the terminal-based version (TexasSolverConsole.exe):

1. Generate the Makefile for the console project:
   ```cmd
   qmake TexasSolverConsole.pro "CONFIG+=release"
   ```
2. Build the executable:
   ```cmd
   mingw32-make release -j8
   ```

### PGO Build (Optional)

For profile-guided optimization in CLI:

1. Build profile-generate binary:
   ```cmd
   qmake TexasSolverConsole.pro "CONFIG+=release enable_lto pgo_generate"
   mingw32-make release -j8
   ```
2. Run representative benchmark scripts to produce profile data.
3. Rebuild profile-use binary:
   ```cmd
   qmake TexasSolverConsole.pro "CONFIG+=release enable_lto pgo_use"
   mingw32-make release -j8
   ```

The final `TexasSolverGui.exe` or `TexasSolverConsole.exe` will be in the `release\` folder.

## Deployment (Standalone Package)

To ensure the app runs with all optimizations and multi-threading (OpenMP) support:

1. Create a `deploy` folder and copy the executable:
   ```cmd
   mkdir deploy
   copy release\TexasSolverGui.exe deploy\
   ```
2. Run the Qt deployment tool:
   ```cmd
   windeployqt deploy\TexasSolverGui.exe
   ```
3. **Manually copy the OpenMP library** (required for the solver speed):
   ```cmd
   copy C:\Qt\Tools\mingw810_64\bin\libgomp-1.dll deploy\
   ```

You can now run `TexasSolverGui.exe` from the `deploy` folder.
