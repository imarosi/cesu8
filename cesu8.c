//
// This project is licensed under the terms of the MIT license.
//

/******************************* CESU-8 to UTF-8 converter ****************************************

Characters with code points above U+FFFF (so-called supplementary characters) are represented
in CESU-8 by separately encoding the two surrogate code units of their UTF-16 representation.
So such code points are encoded as a six-byte sequence:

   CESU-8 bytes:        Code unit bits:         Surrogate code:
u: 1110 1101  ed        1101 10vv vvww wwww     d800-dbff
v: 1010 vvvv  a0-af
w: 10ww wwww  80-bf
x: 1110 1101  ed        1101 11yy yyzz zzzz     dc00-dfff
y: 1011 yyyy  b0-bf
z: 10zz zzzz  80-bf

These six bytes represent the Unicode character with the value
   0x10000+((v&0x0f)<<16)+((w&0x3f)<<10)+(y&0x0f)<<6)+(z&0x3f)

  vvvv wwww wwyy yyzz zzzz
+    1 0000 0000 0000 0000
--------------------------
V VVVV wwww wwyy yyzz zzzz

Official UTF-8 encoding of the same 21-bit value is the four-byte sequence:

  1111 0VVV   10VV wwww   10ww yyyy   10zz zzzz

Quick CESU-8 to UTF-8 conversion (without converting to UTF-16 first) could be like this:

input:   1110 1101   1010 vvvv   10ww wwww   1110 1101   1011 yyyy   10zz zzzz
         u           v           w           x           y           z
                  VVVVV = vvvv+1
output:  1111 0VVV               10VV wwww               10ww yyyy   10zz zzzz
         p                       q                       r           s
**************************************************************************************************/

#define U_BYTE              0xed    // 1110 1101
#define V_BYTE_FIXMASK      0xf0
#define V_BYTE_FIXVAL       0xa0    // 1010 vvvv
#define W_BYTE_FIXMASK      0xc0
#define W_BYTE_FIXVAL       0x80    // 10ww wwww
#define X_BYTE              0xed    // 1110 1101
#define Y_BYTE_FIXMASK      0xf0
#define Y_BYTE_FIXVAL       0xb0    // 1011 yyyy
#define Z_BYTE_FIXMASK      0xc0
#define Z_BYTE_FIXVAL       0x80    // 10zz zzzz

#define P_BYTE_FIXMASK      0xf8
#define P_BYTE_FIXVAL       0xf0    // 1111 0VVV
#define QRS_BYTE_FIXMASK    0xc0
#define QRS_BYTE_FIXVAL     0x80    // 10VV wwww, 10ww yyyy, 10zz zzzz

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <locale.h>

#define BSIZE 4096

// Global variables used by multiple functions:

const char *inputfile = NULL;       // file to convert
const char *outputfile = NULL;      // -o file to write
bool verbose = false;               // -v
bool silent = false;                // -s
bool silentio = false;              // -S
bool fixcode = false;               // -f
bool inverse = false;               // -i    false: CESU-8 to UTF-8 conevrsion; true: UTF-8 to CESU-8 conversion.

FILE *fpi;                          // input FILE pointer
FILE *fpo;                          // output FILE pointer

// in place conversion is done in buff:
unsigned char buff[BSIZE];
int blen;                           // total bytes loaded to buff
int rlen;                           // input bytes already processed in buff
int wlen;                           // output bytes converted in buff

unsigned long long bufpos;          // position of first byte of buff in input file

// inverse conversion requires a separate output buffer. 4 byte UTF-8 sequences
// are converted to 6-byte CESU-8 ones, a larger output buffer is needed:
unsigned char obuff[BSIZE + BSIZE / 2];
// wlen pertains to this buffer in case of inverse conversion...

///////////////////////////////////////////
void openFile()
{
    if (strcmp(inputfile, "-") == 0)
        fpi = stdin;
    else
        fpi = fopen(inputfile, "rb");
    if (!fpi) {
        if (!silentio)
            fprintf(stderr, "cesu8: Error: couldn't open %s\n", inputfile);
        exit(1);
    }
    blen = 0;
    rlen = 0;
    wlen = 0;

    bufpos = 0;
}

void closeFile()
{
    if (fpi != stdin)
        fclose(fpi);
}

void openOutput(const char *file)
{
    if (fpo != stdout) {
        // close previous output file
        int cl = fclose(fpo);
        if (cl != 0) {
            if (!silentio)
                fprintf(stderr, "cesu8: Error: couldn't successfully close %s\n", outputfile);
            exit(5);
        }
    }
    outputfile = file;

    if (strcmp(outputfile, "-") == 0)
        fpo = stdout;
    else
        fpo = fopen(outputfile, "wb");
    if (!fpo) {
        if (!silentio)
            fprintf(stderr, "cesu8: Error: couldn't open %s\n", outputfile);
        exit(4);
    }
}

void writeBuff(size_t len)
{
    if (len) {
        size_t wrn = fwrite(inverse ? obuff : buff, 1, len, fpo);
        if (wrn < len) {
            if (!silentio)
                fprintf(stderr, "cesu8: Error: couldn't write %s while processing %s\n", (fpo == stdout) ? "all text" : outputfile, inputfile);
            exit(2);
        }
    }
}

bool readFile()                                     // read next chunk from file to buff
{
    bufpos += rlen;     // previous buff will be replaced by a new one, starting here

    // emit already converted bytes:
    if (wlen)
        writeBuff(wlen);
    wlen = 0;

    // unprocessed bytes are to be moved to the start of buff:
    if (blen > rlen)
        memmove(buff, buff + rlen, blen - rlen);        // (areas could overlap!)
    blen -= rlen;
    rlen = 0;

    size_t bts = fread(buff + blen, 1, BSIZE - blen, fpi);
    blen += (int)bts;

    if (ferror(fpi)) {
        if (!silentio)
            fprintf(stderr, "cesu8: Error: couldn't read from %s\n", inputfile);
        exit(3);
    }

    return (blen > 0);  // false if no more bytes to process
}

////////////////////////////////////////////
// Searching for a CESU-8 sequence:

int find_U(int i)                                   // find the first byte of the 6-byte CESU-8 sequence
{
    for (; i < blen; i++) {
        if (buff[i] == U_BYTE) {
            if (verbose)
                fprintf(stderr, "CESU-8 Lead byte found at %#06llx; ", bufpos + i);
            return i;
        }
    }
    return blen;    // return blen if not found
}

bool is_found_1st_three(int i)                      // is it a high surrogate?
{
//  if (buff[i + 0] != U_BYTE) return false;                            // buff[i] is definitely U_BYTE (find_U was called), check all others only
    if ((buff[i + 1] & V_BYTE_FIXMASK) != V_BYTE_FIXVAL) return false;
    if ((buff[i + 2] & W_BYTE_FIXMASK) != W_BYTE_FIXVAL) return false;
    return true;
}
bool is_found_2nd_three(int i)                      // is it a low surrogate?
{
    if (buff[i + 0] != X_BYTE) return false;
    if ((buff[i + 1] & Y_BYTE_FIXMASK) != Y_BYTE_FIXVAL) return false;
    if ((buff[i + 2] & Z_BYTE_FIXMASK) != Z_BYTE_FIXVAL) return false;
    return true;
}

bool is_found_six(int i)
{
    return is_found_1st_three(i) && is_found_2nd_three(i + 3);
}

////////////////////////////////////////////
// Convert CESU-8 to UTF-8: (in place)

#define COMB(a, b, bcount) ((a) << bcount) | (b)   // combine bits

void convert_six()                                  // convert 6-byte CESU-8 at rlen to 4-byte UTF-8 at wlen
{
/*
 * input:   1110 1101   1010 vvvv   10ww wwww   1110 1101   1011 yyyy   10zz zzzz
 *          u           v           w           x           y           z
 *                   VVVVV = vvvv+1
 * output:  1111 0VVV               10VV wwww               10ww yyyy   10zz zzzz
 *          p                       q                       r           s
 */
    int vvvv = buff[rlen + 1] & (0xff - V_BYTE_FIXMASK);
    int wwwwww = buff[rlen + 2] & (0xff - W_BYTE_FIXMASK);
    int yyyy = buff[rlen + 4] & (0xff - Y_BYTE_FIXMASK);
    int zzzzzz = buff[rlen + 5] & (0xff - Z_BYTE_FIXMASK);      // no need to convert the last byte ... UTF-8 value is the same

    int VVVVV = vvvv + 1;

    // Unicode value: V VVVV wwww wwyy yyzz zzzz

    if (verbose) {
        int uni = COMB(COMB(COMB(VVVVV, wwwwww, 6), yyyy, 4), zzzzzz, 6);
        fprintf(stderr, "Unicode U+%04x (%lc)\n", uni, uni);
    }

    buff[wlen + 0] = P_BYTE_FIXVAL | (VVVVV >> 2);                          // p
    buff[wlen + 1] = QRS_BYTE_FIXVAL | ((VVVVV & 3) << 4) | (wwwwww >> 2);  // q
    buff[wlen + 2] = QRS_BYTE_FIXVAL | ((wwwwww & 3) << 4) | yyyy;          // r
    buff[wlen + 3] = buff[rlen + 5];                                        // s

    rlen += 6;
    wlen += 4;
}

////////////////////////////////////////////
// Searching for a UTF-8 sequence:

int find_P(int i)                                   // find the first byte of the 4-byte UTF-8 sequence
{
    for (; i < blen; i++) {
        if ((buff[i] & P_BYTE_FIXMASK) == P_BYTE_FIXVAL) {
            if (verbose)
                fprintf(stderr, "UTF-8 Lead byte found at %#06llx; ", bufpos + i);
            return i;
        }
    }
    return blen;    // return blen if not found
}

bool is_found_four(int i)                           // is it indeed a 4-byte UTF-8 sequence?
{
//  if ((buff[i + 0] & P_BYTE_FIXMASK) == P_BYTE_FIXVAL) return false;           // buff[i] is definitely P_BYTE_FIXVAL (find_P was called), check all others only
    if ((buff[i + 1] & QRS_BYTE_FIXMASK) != QRS_BYTE_FIXVAL) return false;
    if ((buff[i + 2] & QRS_BYTE_FIXMASK) != QRS_BYTE_FIXVAL) return false;
    if ((buff[i + 3] & QRS_BYTE_FIXMASK) != QRS_BYTE_FIXVAL) return false;
    return true;
}

////////////////////////////////////////////
// Convert UTF-8 to CESU-8:

void convert_four()                                  // convert 4-byte UTF-8 at rlen to 6-byte CESU-8 at wlen in obuff
{
/*
 * input:   1111 0VVV               10VV wwww               10ww yyyy   10zz zzzz
 *          p                       q                       r           s
 *                   vvvv = VVVVV-1
 * output:  1110 1101   1010 vvvv   10ww wwww   1110 1101   1011 yyyy   10zz zzzz
 *          u           v           w           x           y           z
 */
    int VVV = buff[rlen + 0] & (0xff - P_BYTE_FIXMASK);
    int VVwwww = buff[rlen + 1] & (0xff - QRS_BYTE_FIXMASK);
    int wwyyyy = buff[rlen + 2] & (0xff - QRS_BYTE_FIXMASK);
    int zzzzzz = buff[rlen + 3] & (0xff - QRS_BYTE_FIXMASK);      // no need to convert the last byte ... CESU-8 value is the same

    int VVVVV = COMB(VVV, VVwwww >> 4, 2);
    int wwwwww = COMB(VVwwww & 0x0f, wwyyyy >> 4, 2);
    int yyyy = wwyyyy & 0x0f;

    int vvvv = VVVVV - 1;

    // Unicode value: V VVVV wwww wwyy yyzz zzzz

    if (vvvv < 0 || vvvv > 0x0f) {
        // overlong UTF-8 (<0) or too large Unicode (>0xf)
        if (!silent) {
            int uni = COMB(COMB(COMB(VVVVV, wwwwww, 6), yyyy, 4), zzzzzz, 6);
            fprintf(stderr, "cesu8: Warning: Invalid 4-byte U+%06x found at %#06llx! %s\n"
                                                          , uni
                                                                          , bufpos + rlen
                                                                                   , fixcode ? "Converted to '?'" : "Left unchanged (see -f)"
            );
        }
        if (fixcode) {
            obuff[wlen] = '?';
            rlen += 4;
            wlen += 1;
        } else {
            // not to change: It's enough to copy the first byte now
            obuff[wlen++] = buff[rlen++];
        }
        return;
    }

    if (verbose) {
        int uni = COMB(COMB(COMB(VVVVV, wwwwww, 6), yyyy, 4), zzzzzz, 6);
        fprintf(stderr, "Unicode U+%04x (%lc)\n", uni, uni);
    }

    obuff[wlen + 0] = U_BYTE;                                               // u
    obuff[wlen + 1] = V_BYTE_FIXVAL | vvvv;                                 // v
    obuff[wlen + 2] = V_BYTE_FIXVAL | wwwwww;                               // w
    obuff[wlen + 3] = U_BYTE;                                               // x
    obuff[wlen + 4] = Y_BYTE_FIXVAL | yyyy;                                 // y
    obuff[wlen + 5] = buff[rlen + 3];                                       // z

    rlen += 4;
    wlen += 6;
}

////////////////////////////////////////////
// Other functions:

void step_to(int upos)                              // Save the string between rlen and upos (write it to wlen)
{
    if (upos > rlen) {
        int addlen = upos - rlen;
        if (inverse)
            memcpy(obuff + wlen, buff + rlen, addlen);
        else if (wlen != rlen)
            memmove(buff + wlen, buff + rlen, addlen);      // (areas could overlap!)
        rlen = upos;
        wlen += addlen;
    }
}

// Used for reporting unpaired surrogates in CESU-8 files:
int utf8_three()                                    // return the Unicode value of the 3-byte UTF-8 sequence ar rlen
{
    /* p: 1110 pppp     Code unit: pppp qqqq qqrr rrrr
     * q: 10qq qqqq
     * r: 10rr rrrr
     */
    int pppp = buff[rlen + 0] & 0x0f;
    int qqqqqq = buff[rlen + 1] & 0x3f;
    int rrrrrr = buff[rlen + 2] & 0x3f;

    return COMB(COMB(pppp, qqqqqq, 6), rrrrrr, 6);
}

////////////////////////////////////////////
// Buffer conversion:

void convertCesuBuff()                          // CESU-8 to UTF-8
{
    // we know that rlen == wlen == 0 (because readFile zeroes them)
    if (blen < 6) {
        // Short file, or this is the last (short) chunk of the file after a CESU-8 sequence close to the end of file
        step_to(blen);
        return;
    }
    while (rlen < blen) {
        int upos = find_U(rlen);
        // upos is the position of the first byte of a potential 6-byte CESU-8 sequence (u), or == blen if not found
        step_to(upos);      // move rlen to upos and write the unmodified rlen..upos range to wlen
        // rlen == upos now (and the string is written up to wlen)
        // if the leader byte found, check if this is indeed a CESU-8 sequence:
        if (rlen != blen) {
            if (rlen + 6 > blen)
                return;     // there are not enough bytes there, load next chunk
            if (is_found_six(rlen)) {
                // convert this CESU-8 code point to UTF-8
                convert_six();  //  (from buff+rlen to buff+wlen)
                // rlen and wlen updated
            } else {
                bool high = is_found_1st_three(rlen);
                bool low = is_found_2nd_three(rlen);
                if (high || low) {
                    // Oops, invalid code!
                    if (!silent)
                        fprintf(stderr, "cesu8: Warning: Unpaired %s surrogate U+%04x found at %#06llx! %s\n"
                                                        , high ? "High" : " Low"
                                                                    , utf8_three()
                                                                                    , bufpos + rlen
                                                                                            , fixcode ? "Converted to '?'" : "Left unchanged (see -f)"
                        );
                    if (fixcode) {
                        // step_to(upos) was already called (rpos == upos) and the string up to current position is copied
                        rlen += 3;
                        buff[wlen++] = '?';
                    } else {
                        // Just skip it
                        step_to(rlen + 3);
                    }
                } else {
                    // This is a normal non-surrogate code in the d000..d7ff range (or an invalid byte)
                    if (verbose)
                        fprintf(stderr, "Not a surrogate; Left unchanged\n");
                    step_to(rlen + 1);
                }
            }
        }
    }
}

void convertUtfBuff()                           // UTF-8 to CESU-8
{
    // we know that rlen == wlen == 0 (because readFile zeroes them)
    if (blen < 4) {
        // Short file, or this is the last (short) chunk of the file after a UTF-8 sequence close to the end of file
        step_to(blen);
        return;
    }
    while (rlen < blen) {
        int upos = find_P(rlen);
        // upos is the position of the first byte of a 4-byte UTF-8 sequence (p), or == blen if not found
        step_to(upos);      // move rlen to upos and write the unmodified rlen..upos range to wlen
        // rlen == upos now (and the string is written up to wlen)
        // if the leader byte found, check if this is indeed a CESU-8 sequence:
        if (rlen != blen) {
            if (rlen + 4 > blen)
                return;     // there are not enough bytes there, load next chunk
            if (is_found_four(rlen)) {
                // convert this UTF-8 code point to CESU-8
                convert_four();  //  (from buff+rlen to obuff+wlen)
                // rlen and wlen updated
                // (In case of wrong 4-byte code '?' is converted)
            } else {
                // It should not happen... happens only if the UTF-8 encoding is buggy
                if (!silent)
                    fprintf(stderr, "cesu8: Warning: Invalid UTF-8 sequence found at %#04llx! Left unchanged\n", bufpos + rlen);
                step_to(rlen + 1);
            }
        }
    }
}

////////////////////////////////////////////

int main(int argc, char **argv)
{
    int i;

    setlocale(LC_ALL, "");  // for printf'ing Unicode characters: %lc
    fpo = stdout;

    for (i=1; i<argc; i++) {
        if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--u2c") == 0) {
            inverse = true;
        } else if (strcmp(argv[i], "--c2u") == 0) {
            inverse = false;
        } else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--fix") == 0) {
            fixcode = true;
        } else if (strcmp(argv[i], "-v") == 0) {
            verbose = true;
        } else if (strcmp(argv[i], "-s") == 0) {
            silent = true;
        } else if (strcmp(argv[i], "-S") == 0) {
            silent = true;
            silentio = true;
        } else if (strcmp(argv[i], "-o") == 0) {
            if (++i < argc)
                openOutput(argv[i]);
        } else {
            // this is the file to convert:
            inputfile = argv[i];
            openFile();
            while (readFile()) {
                if (inverse)
                    convertUtfBuff();       // UTF-8 to CESU-8
                else
                    convertCesuBuff();      // CESU-8 to UTF-8
            }
            closeFile();
        }
    }
    openOutput("-");    // close previous output...

    if (!inputfile) {
        fprintf(stderr,
                "Usage: cesu8 [<options>] file ...\n"
                "  Converts CESU-8 file(s) to UTF-8. Does inverse conversion if -i specified.\n"
                "  The file named '-' means stdin.\n"
                "  Converted output is written to stdout (but see -o)\n"
                "Options:\n"
                "  -i  --u2c    Convert UTF-8 to CESU-8; i.e. inverse conversion\n"
                "      --c2u    Convert CESU-8 to UTF-8; (this is the default)\n"
                "  -f  --fix    Fix unpaired surrogates and invalid 4-byte codes:\n"
                "               Covert them to '?'\n"
                "  -v           Verbose mode: report converted codes\n"
                "  -s           Silent mode: don't report encoding warnings\n"
                "  -S           Silent mode: don't report file I/O errors and encoding warnings\n"
                "  -o <file>    Write output to <file>, not stdout\n"
                "Note: An option affects processing of file(s) that follow it\n"
                "Note: Conversion is done without checking the file's encoding!\n"
                "If the file is already UTF-8 (or CESU-8 in case of -i), no codes are modified.\n"
                "Unpaired surrogate fixing (-f) is possible at CESU-8 to UTF-8 conversion only.\n"
                "(Running 'cesu8 -f' on a UTF-8 file fixes unpaired surrogates in that text,\n"
                " too, no other text modifications are done.)\n"
                "Invalid 4-byte code fixing is possible at UTF-8 to CESU-8 conversion (-i) only.\n"
               );
    }

    return 0;
}

// vim: tabstop=4 shiftwidth=4 softtabstop=4 expandtab:
