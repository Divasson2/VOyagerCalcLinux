# VoyagerCalc tools

## convert_16c_memory.py: old HP-16C memory files to the 206-slot layout

VoyagerCalc 0.7.7 opens HP-16C memory files in two layouts only: stock (29 slots) and the
206-slot layout of the EAEA, C9C9 and ECEC firmware. Files saved with the retired 176-slot
builds (B4B4, B5B5, B7B7, E9E9, E8E8 and the early E3E3 multi) no longer load. This script
moves their registers and program into the 206-slot layout. It can also convert a stock
file (`--from stock`).

Requirements: Python 3.8 or newer, standard library only. MIT licence, VoyagerCalc's own code.

```
python3 convert_16c_memory.py OLD.16c                 # writes OLD-206.16c (layout detected)
python3 convert_16c_memory.py OLD.16c -o NEW.16c
python3 convert_16c_memory.py --from stock STOCK.16c  # a stock file needs --from stock
python3 convert_16c_memory.py --check *.16c           # only report the layout of each file
python3 convert_16c_memory.py --help
```

On Windows use `py convert_16c_memory.py ...` from the `tools` folder of the installation.

- The original file is never changed, and an existing output is not overwritten (unless `--force`).
- Only the 16C register/program pool moves. The stack, flags, word size, program pointers,
  the fixed constant at +7F0 and the rest of the 2048 bytes are copied unchanged.
- A raw memory file has no layout tag. The tool decides from where the data sits, and stops
  with exit status 3 when it cannot be sure (a stock file and a sparse 206-slot file can look
  alike). Then give `--from`.
- It refuses 12C / 15C files, files saved while a program was running, and anything that
  would lose data. B3B3 test-build files (aliased pool) cannot be recognised from the file and
  must not be converted.
- Open the result with an EAEA, C9C9 or ECEC firmware in 16C mode (File ▸ Open Memory File…).

`test_convert_16c_memory.py` is its self-test (synthetic images, all 1000 program lengths).
