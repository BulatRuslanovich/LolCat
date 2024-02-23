#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

#include "math.h"

static char helpStr[] =
    "\n"
    "Usage: lolcat [-h horizontal_speed] [-v vertical_speed] [--] [FILES...]\n"
    "\n"
    "Concatenate FILE(s), or standard input, to standard output.\n"
    "With no FILE, or when FILE is -, read standard input.\n"
    "\n"
    "--horizontal-frequency <d>, -h <d>: Horizontal rainbow frequency (default: 0.23)\n"
    "  --vertical-frequency <d>, -v <d>: Vertical rainbow frequency (default: 0.1)\n"
    "                 --force-color, -f: Force color even when stdout is not a tty\n"
    "             --no-force-locale, -l: Use encoding from system locale instead of\n"
    "                                    assuming UTF-8\n"
    "                      --random, -r: Random colors\n"
    "                --seed <d>, -s <d>: Random colors based on given seed,\n"
    "                                    implies --random\n"
    "        --color_offset <d>, -o <d>: Start with a different color\n"
    "            --gradient <g>, -g <g>: Use color gradient from given start to end color,\n"
    "                                    format: -g ff4444:00ffff\n"
    "                       --24bit, -b: Output in 24-bit \"true\" RGB mode (slower and\n"
    "                                    not supported by all terminals)\n"
    "                     --16color, -x: Output in 16-color mode for basic terminals\n"
    "                      --invert, -i: Invert foreground and background\n"
    "                         --version: Print version and exit\n"
    "                            --help: Show this message\n"
    "\n"
    "Examples:\n"
    "  lolcat f - g      Output f's contents, then stdin, then g's contents.\n"
    "  lolcat            Copy standard input to standard output.\n"
    "  fortune | lolcat  Display a rainbow cookie.\n"
    "\n"
    "Original idea: <https://github.com/busyloop/lolcat/>\n";

#define true 1
#define false 0
#define PI 3.1415926535
#define ARRAY_SIZE(foo) (sizeof(foo) / sizeof(foo[0]))

const unsigned char codes[] = {39,  38,  44,  43,  49,  48,  84,  83,  119, 118, 154, 148, 184, 178, 214,
                               208, 209, 203, 204, 198, 199, 163, 164, 128, 129, 93,  99,  63,  69,  33};
const unsigned char codes16[] = {31, 33, 32, 36, 34, 35, 95, 94, 96, 92, 93, 91};
unsigned int codesGradient[128];

union rgb_c {
    struct {
        unsigned char r;
        unsigned char g;
        unsigned char b;
    };

    unsigned int i;
};

#include "xterm256Palette.h"

enum errorCodes {
    OK = 0,
    lol = 1,
    ERROR = 2,
};

enum escSt { NONE = 0, ESC_BEGIN, ESC_STRING, ESC_CSI, ESC_STRING_TERM, ESC_CSI_TERM, ESC_TERM, EST_COUNT };

const char *escStNames[EST_COUNT] = {[NONE] = "NONE",
                                     [ESC_BEGIN] = "BEGIN",
                                     [ESC_STRING] = "STRING",
                                     [ESC_CSI] = "CSI",
                                     [ESC_STRING_TERM] = "STRING_TERM",
                                     [ESC_CSI_TERM] = "CSI_TERM",
                                     [ESC_TERM] = "TERM"};

typedef struct {
    int f;
    int l;
    int r;
    int g;
    int b;
    int x;
    int i;
    int help;
} Flags;

enum escSt findEscapeSequences(char c, enum escSt state) {
    if (state == NONE || state == ESC_CSI_TERM) {
        if (c == '\033') {
            return ESC_BEGIN;
        } else {
            return NONE;
        }
    } else if (state == ESC_BEGIN) {
        if (c == '[') {
            return ESC_CSI;
        } else if (c == 'P' || c == ']' || c == 'X' || c == '^' || c == '_') {
            return ESC_STRING;
        } else {
            return ESC_CSI;
        }
    } else if (state == ESC_CSI) {
        if (0x40 <= c && c <= 0x7e) {
            return ESC_CSI_TERM;
        } else {
            return state;
        }
    } else if (state == ESC_STRING) {
        if (c == '\007') {
            return NONE;
        } else if (c == '\033') {
            return ESC_STRING_TERM;
        } else {
            return state;
        }
    } else if (state == ESC_STRING_TERM) {
        if (c == '\\') {
            return NONE;
        } else {
            return ESC_STRING;
        }
    } else {
        return NONE;
    }
}

int initStruct(Flags *flags, int symbol, int *seed, double *freq_h, double *freq_v, int *startColor,
               union rgb_c *rgb_start, union rgb_c *rgb_end) {
    int errCode = OK;
    char *endPtr;

    switch (symbol) {
        case 'h':
            *freq_h = strtod(optarg, &endPtr);

            if (*endPtr) {
                exit(ERROR);
            }
            break;
        case 'v':
            *freq_v = strtod(optarg, &endPtr);

            if (*endPtr) {
                exit(ERROR);
            }
            break;
        case 'f':
            flags->f = true;
            break;
        case 'l':
            flags->l = false;
            break;
        case 'r':
            flags->r = true;
            break;
        case 's':
            *seed = strtoul(optarg, &endPtr, 10);

            if (*endPtr) {
                exit(ERROR);
            }
            break;
        case 'o':
            *startColor = strtoul(optarg, &endPtr, 10);

            if (*endPtr) {
                exit(ERROR);
            }
            break;
        case 'g':
            flags->g = true;
            if (strlen(optarg) != 6 + 1 + 6 || optarg[6] != ':') {
                wprintf(L"Invalid format for --gradient\n");
                exit(ERROR);
            }

            char *endPtr;

            optarg[6] = '\0';
            rgb_start->i = strtoul(optarg, &endPtr, 16);

            if (*endPtr) {
                wprintf(L"Invalid format for --gradient\n");
                exit(ERROR);
            }

            rgb_end->i = strtoul(optarg + 6 + 1, &endPtr, 16);

            if (*endPtr) {
                wprintf(L"Invalid format for --gradient\n");
                exit(ERROR);
            }
            break;
        case 'b':
            flags->b = true;
            break;
        case 'x':
            flags->x = true;
            break;
        case 'i':
            flags->i = true;
            break;
        case '1':
            flags->help = true;
            break;
        case '?':
            errCode = ERROR;
    }

    return errCode;
}

int xterm256LookLike(union rgb_c *in) {
    size_t min_index;
    int min_v = INT_MAX;

    for (size_t i = 0; i < ARRAY_SIZE(xterm256Palette); ++i) {
        int diffR = in->r - xterm256Palette[i].r;
        int diffG = in->g - xterm256Palette[i].g;
        int diffB = in->b - xterm256Palette[i].b;

        int diff = diffR * diffR + diffG * diffG + diffB * diffB;

        if (diff < min_v) {
            min_v = diff;
            min_index = i;
        }
    }

    return 16 + min_index;
}

void rgbInterpolate(union rgb_c *start, union rgb_c *end, union rgb_c *out, double factor) {
    out->b = start->b + (end->b - start->b) * factor;
    out->r = start->r + (end->r - start->r) * factor;
    out->g = start->g + (end->g - start->g) * factor;
}

int wcwidth(wchar_t wc);

int main(int argc, char **argv) {
    char *defaultArgv[] = {"-"};
    double freq_h = 0.23;
    double freq_v = 0.01;
    int startColor = 0;
    union rgb_c rgb_start;
    union rgb_c rgb_end;

    int l = 0;
    int cc = -1;

    int colors = isatty(STDOUT_FILENO);

    struct timeval timeVal;
    gettimeofday(&timeVal, NULL);
    double offX = (timeVal.tv_sec % 300) / 300.0;

    int seed = time(NULL);
    int errCode = lol;
    Flags flags = {false, true, false, false, false, false, false, false};
    char *flagsString = ":h:v:s:g:flrobxi?";
    int flagSymbol;

    struct option longFlags[] = {{"horizontal-frequency", 0, NULL, 'h'},
                                 {"vertical-frequency", 0, NULL, 'v'},
                                 {"force-color", 0, NULL, 'f'},
                                 {"no-force-locale", 0, NULL, 'l'},
                                 {"random", 0, NULL, 'r'},
                                 {"seed", 0, NULL, 's'},
                                 {"color_offset", 0, NULL, 'o'},
                                 {"24bit", 0, NULL, 'b'},
                                 {"16color", 0, NULL, 'x'},
                                 {"invert", 0, NULL, 'i'},
                                 {"gradient", 0, NULL, 'g'},
                                 {"help", 0, NULL, '1'},
                                 {NULL, 0, NULL, 0}};

    while (((flagSymbol = getopt_long(argc, argv, flagsString, longFlags, NULL)) != -1)) {
        errCode = initStruct(&flags, flagSymbol, &seed, &freq_h, &freq_v, &startColor, &rgb_start, &rgb_end);
    }

    if (errCode != ERROR) {
        if (flags.b && flags.x) {
            wprintf(L"Only one of --24bit and --16color can be given at a time\n");
            exit(ERROR);
        }
    }

    if (flags.help) {
        printf("%s", helpStr);
        return 0;
    }

    if (flags.g) {
        if (flags.x) {
            wprintf(L"--gradient and --16color are mutually exclusive\n");
            exit(2);
        }

        if (!flags.b) {
            size_t codesGradientSize = ARRAY_SIZE(codesGradient);
            double correctionFactor = 2 * codesGradientSize / (double)ARRAY_SIZE(codes);
            freq_h *= correctionFactor;
            freq_v *= correctionFactor;

            for (size_t i = 0; i < codesGradientSize; ++i) {
                double factor = i / (double)(codesGradientSize - 1);
                union rgb_c rgb_intermediate;
                rgbInterpolate(&rgb_start, &rgb_end, &rgb_intermediate, factor);
                codesGradient[i] = xterm256LookLike(&rgb_intermediate);
            }
        }
    }

    if (flags.i) {
        if (flags.x) {
            wprintf(L"\033[30m\n");
        } else {
            wprintf(L"\033[38;5;16m\n");
        }
    }

    int randomOffset = 0;
    if (flags.r) {
        srand(seed);
        randomOffset = rand();
    }

    (void)randomOffset;

    char **inputsBegin = argv + optind;
    char **inputsEnd = argv + argc;

    if (inputsBegin == inputsEnd) {
        inputsBegin = defaultArgv;
        inputsEnd = inputsBegin + 1;
    }

    char *envLang = getenv("LANG");  // return en_GB.UTF-8

    if (flags.l && envLang && !strstr(envLang, "UTF-8")) {
        if (!setlocale(LC_ALL, "C.UTF-8")) {
            setlocale(LC_ALL, "");
        }
    } else {
        setlocale(LC_ALL, "");
    }

    int i = 0;

    for (char **fileName = inputsBegin; fileName < inputsEnd; fileName++) {
        FILE *filePtr;
        int escapeState = NONE;
        char c;

        if (!strcmp(*fileName, "-")) {
            filePtr = stdin;
        } else {
            if ((filePtr = fopen(*fileName, "r")) == NULL) {
                fwprintf(stderr, L"Cannot open input file \"%s\": %s\n", *fileName, strerror(errno));
                exit(2);
            }
        }

        while (fread(&c, 1, 1, filePtr) > 0) {
            if (colors) {
                escapeState = findEscapeSequences(c, escapeState);

                if (escapeState == ESC_CSI_TERM) {
                    putwchar(c);
                }

                if (escapeState == NONE || escapeState == ESC_CSI_TERM) {
                    if (c == '\n') {
                        l++;
                        i = 0;

                        if (flags.i) {
                            wprintf(L"\033[49m");
                        }
                    } else {
                        if (escapeState == NONE) {
                            i += wcwidth(c);
                        }

                        if (flags.b) {
                            float theta = i * freq_h / 5.0f + l * freq_v +
                                          PI * (offX + 2.0f * (randomOffset + startColor) / (double)RAND_MAX);

                            union rgb_c color;

                            if (flags.g) {
                                theta = fmodf(theta / 2.0f / PI, 2.0f);

                                if (theta > 1.0f) {
                                    theta = 2.0f - theta;
                                }

                                rgbInterpolate(&rgb_start, &rgb_end, &color, theta);
                            } else {
                                float offset = 0.1;
                                color.r =
                                    lrintf((offset + (1.0f - offset) * (0.5f + 0.5f * sin(theta))) * 255.0f);
                                color.g = lrintf(
                                    (offset + (1.0f - offset) * (0.5f + 0.5f * sin(theta + 2 * PI / 3))) *
                                    255.0f);
                                color.b = lrintf(
                                    (offset + (1.0f - offset) * (0.5f + 0.5f * sin(theta + 4 * PI / 3))) *
                                    255.0f);
                            }

                            wprintf(L"\033[%d;2;%d;%d;%dm", (flags.i ? 48 : 38), color.r, color.g, color.b);
                        } else if (flags.x) {
                            int ncc = offX * ARRAY_SIZE(codes16) + (int)(i * freq_h + l * freq_v);

                            if (cc != ncc || escapeState == ESC_CSI_TERM) {
                                wprintf(L"\033[%hhum", (flags.i ? 10 : 0) +
                                                           codes16[(randomOffset + startColor + (cc = ncc)) %
                                                                   ARRAY_SIZE(codes16)]);
                            }

                        } else {
                            if (flags.g) {
                                int ncc = offX * ARRAY_SIZE(codesGradient) + (int)(i * freq_h + l * freq_v);

                                if (cc != ncc || escapeState == ESC_CSI_TERM) {
                                    size_t lookup = (randomOffset + startColor + (cc = ncc)) %
                                                    (2 * ARRAY_SIZE(codesGradient));

                                    if (lookup >= ARRAY_SIZE(codesGradient)) {
                                        lookup = 2 * ARRAY_SIZE(codesGradient) - 1 - lookup;
                                    }

                                    wprintf(L"\033[%d;5;%hhum", (flags.i ? 48 : 38), codesGradient[lookup]);
                                }
                            } else {
                                int ncc = offX * ARRAY_SIZE(codes) + (int)(i * freq_h + l * freq_v);
                                if (cc != ncc || escapeState == ESC_CSI_TERM) {
                                    wprintf(
                                        L"\033[%d;5;%hhum", (flags.i ? 48 : 38),
                                        codes[(randomOffset + startColor + (cc = ncc)) % ARRAY_SIZE(codes)]);
                                }
                            }
                        }
                    }
                }
            }

            if (escapeState != ESC_CSI_TERM) {
                putwchar(c);
            }
        }

        if (colors) {
            wprintf(L"\033[0m");
        }

        if (filePtr) {
            if (ferror(filePtr)) {
                fwprintf(stderr, L"Error reading input file \"%s\": %s\n", *fileName, strerror(errno));
                fclose(filePtr);
                return ERROR;
            }

            if (fclose(filePtr)) {
                fwprintf(stderr, L"Error closing input file \"%s\": %s\n", *fileName, strerror(errno));
                return ERROR;
            }
        }
    }

    return errCode;
}
