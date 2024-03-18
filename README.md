# cesu8
CESU-8 to UTF-8 converter (and vice verse)

CESU-8 is a nonstandard UTF-8 encoding, see https://en.wikipedia.org/wiki/CESU-8

Some tools, mostly ones from Windows, Java and Oracle, may generate such files even though they are invalid UTF-8: They contain codes in the D800-DFFF code range. A pair of these codes, called surrogate pairs, are used in UTF-16 to encode code points above U+10000. Encoding them in UTF-8 is considered invalid by the Unicode standard and generates error in many XML readers and some other UTF-8 processing tools, e.g. iconv.

This small tool can be used to convert CESU-8 files to standard UTF-8. It can detect unpaired surrogates, too, that are also invalid in UTF-8, and can convert them to a question mark.
Another possible use of this tool is to generate CESU-8 encoded files, mainly to use them in Oracle databases.

## Building cesu8
Use your C compiler to compile the tool. There is no Makefile added, on Linux and macOS just use 'make cesu8' to compile the C source.

## Using cesu8
cesu8 is a command line tool. Running it without any input files shows how to use it and what options are supported. The current help text is like this:

```
Usage: cesu8 [<options>] file ...
  Converts CESU-8 file(s) to UTF-8. Does inverse conversion if -i specified.
  The file named '-' means stdin.
  Converted output is written to stdout (but see -o)
Options:
  -i  --u2c    Convert to CESU-8; i.e. inverse conversion
      --c2u    Convert to UTF-8; (this is the default)
  -f  --fix    Fix unpaired surrogates: convert them to '?'
  -v           Verbose mode: report converted codes
  -s           Silent mode: don't report encoding warnings
  -S           Silent mode: don't report file I/O errors and encoding warnings
  -o <file>    Write output to <file>, not stdout
Note: An option affects processing of file(s) that follow it
Note: Conversion is done without checking the file's encoding!
If the file is already UTF-8 (or CESU-8 in case of -i), no codes are modified.
Unpaired surrogate fixing (-f) is possible at CESU-8 to UTF-8 conversion only.
(Running 'cesu8 -f' on a UTF-8 file fixes unpaired surrogates in that text,
 too, no other text modifications are done.)
```

## License
This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
