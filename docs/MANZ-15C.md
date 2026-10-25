# HP-15C program files (.15c, Torsten Manz simulator format)

File ▸ Import Program (.15c)… / Export Program (.15c)… (HP-15C only). Independent implementation (src/Manz15C.*,
src/ProgramEntry15.*), written from the file format only; no code from the GPL simulator is used.

- Export: UTF-16LE with BOM, CRLF line ends, one step per line `   %03d {%12s } %s` (step, keycodes, mnemonic).
- Import: only the braced keycodes are read; text outside braces (comments, mnemonics) is ignored. UTF-16LE/BE BOM,
  UTF-8 (with or without BOM) and Windows-1252 files are accepted.
- Import keys the program into the emulated calculator (f CLEAR PRGM, then every step in program mode), checks the
  resulting listing, and restores the previous memory if anything fails or the program does not fit.
- Tests: tests/tst_manz.cpp; set MANZ_PROGRAMS to the simulator's Programs folder for the 100-file round trip
  (MANZ_KEY_ALL=1 keys all of them into the emulator, default 12).
