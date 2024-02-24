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
#include <stdbool.h>

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
    "                            --help: Show this message\n";


#define PI 3.1415926535
#define ARRAY_SIZE(array) (sizeof(array) / sizeof(array[0]))

const unsigned char codes[] = {39,  38,  44,  43,  49,  48,  84,  83,  119, 118, 154, 148, 184, 178, 214,
                               208, 209, 203, 204, 198, 199, 163, 164, 128, 129, 93,  99,  63,  69,  33};
const unsigned char codes16[] = {31, 33, 32, 36, 34, 35, 95, 94, 96, 92, 93, 91};
unsigned int codesGradient[128];


/**
 * Этот объединенный тип данных rgb_c представляет цвет в формате RGB.
 * Он может быть представлен либо в виде трех отдельных компонентов красного (r), зеленого (g) и синего (b),
 * каждый из которых является восьмибитным беззнаковым целым числом (unsigned char),
 * либо в виде целого числа (unsigned int), содержащего все три компонента цвета в своих младших байтах,
 * в формате RGB24 (8 бит для каждого канала).
 */
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
    ERROR = -1,
};

// NONE: Исходное состояние, когда нет никаких управляющих последовательностей escape.
// ESC_BEGIN: Состояние, когда встречен символ начала управляющей последовательности escape.
// ESC_STRING: Состояние, когда обрабатывается строка управляющей последовательности.
// ESC_CSI: Состояние, когда обрабатывается управляющая последовательность управления курсором (CSI).
// ESC_STRING_TERM: Состояние, когда строка управляющей последовательности завершается.
// ESC_CSI_TERM: Состояние, когда управляющая последовательность управления курсором завершается.
// ESC_TERM: Состояние, когда завершается обработка управляющей последовательности.
enum escState { NONE = 0, ESC_BEGIN, ESC_STRING, ESC_CSI, ESC_STRING_TERM, ESC_CSI_TERM, ESC_TERM, EST_COUNT };


/**
 * Структура Flags используется для хранения флагов и параметров командной строки.
 * Каждый член структуры соответствует определенному флагу или параметру командной строки.
 *
 * f: Флаг для опции -f (--force-color), указывающий, следует ли принудительно выводить цвет даже при отсутствии терминала.
 * l: Флаг для опции -l (--no-force-locale), указывающий, следует ли использовать кодировку из системной локали вместо предположения UTF-8.
 * r: Флаг для опции -r (--random), указывающий, следует ли использовать случайные цвета.
 * s: Параметр для опции -s (--seed), задающий начальное значение для генерации случайных цветов.
 * g: Флаг для опции -g (--gradient), указывающий, следует ли использовать градиент цвета от начального до конечного.
 * b: Флаг для опции -b (--24bit), указывающий, следует ли выводить результат в 24-битном "истинном" RGB-режиме.
 * x: Флаг для опции -x (--16color), указывающий, следует ли выводить результат в 16-цветном режиме для основных терминалов.
 * i: Флаг для опции -i (--invert), указывающий, следует ли инвертировать передний план и задний план.
 * help: Флаг для опции --help, указывающий, следует ли выводить сообщение о помощи.
 */
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

/**
 * @brief Функция определяет текущее состояние обработки управляющих последовательностей escape (ESC) на основе входного символа и предыдущего состояния.
 *
 * @param c Входной символ для анализа.
 * @param state Предыдущее состояние обработки управляющих последовательностей escape.
 * @return Новое состояние обработки управляющих последовательностей escape.
 */

enum escState findEscapeSequences(char ch, enum escState state) {
    if (state == NONE || state == ESC_CSI_TERM) {
        // Если встречен символ начала управляющей последовательности escape
        if (ch == '\033') {
            return ESC_BEGIN;
        } else {
            return NONE;
        }
    } else if (state == ESC_BEGIN) {
        // Если встречен символ, следующий за ESC (код CSI)
        if (ch == '[') {
            return ESC_CSI;
        }
        // Если встречен символ, обозначающий начало строки управляющей последовательности
        else if (ch == 'P' || ch == ']' || ch == 'X' || ch == '^' || ch == '_') {
            return ESC_STRING; // Возвращаем новое состояние ESC_STRING
        } else {
            return ESC_CSI;
        }
    } else if (state == ESC_CSI) {
        // Если встречен символ завершения управляющей последовательности CSI
        if (0x40 <= ch && ch <= 0x7e) {
            return ESC_CSI_TERM;
        } else {
            return state;
        }
    } else if (state == ESC_STRING) {
        // Если встречен символ завершения строки управляющей последовательности
        if (ch == '\007') {
            return NONE;
        }
        // Если встречен символ ESC внутри строки управляющей последовательности
        else if (ch == '\033') {
            return ESC_STRING_TERM;
        } else {
            return state;
        }
    } else if (state == ESC_STRING_TERM) {
        // Если встречен символ завершения управляющей последовательности
        if (ch == '\\') {
            return NONE;
        } else {
            return ESC_STRING;
        }
    } else {
        return NONE; // Если состояние неизвестно, возвращаемся в состояние NONE
    }
}


/**
 * @brief Инициализирует структуру Flags и другие переменные в зависимости от символа, переданного в параметре symbol.
 * @param flags Указатель на структуру Flags, которую необходимо инициализировать.
 * @param symbol Символ опции командной строки.
 * @param seed Указатель на переменную, содержащую значение для инициализации генератора случайных чисел.
 * @param freq_h Указатель на переменную, содержащую горизонтальную частоту радужных цветов.
 * @param freq_v Указатель на переменную, содержащую вертикальную частоту радужных цветов.
 * @param startColor Указатель на переменную, содержащую начальный цвет для градиента.
 * @param rgb_start Указатель на структуру rgb_c, содержащую начальный цвет для градиента в формате RGB.
 * @param rgb_end Указатель на структуру rgb_c, содержащую конечный цвет для градиента в формате RGB.
 * @return Код ошибки (OK - успешное выполнение, ERROR - ошибка в процессе выполнения).
 */

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

/**
 * @brief Определяет индекс цвета в палитре Xterm256, который наиболее близок к заданному цвету в формате RGB.
 *
 * @param in Указатель на структуру rgb_c, представляющую заданный цвет в формате RGB.
 * @return Индекс цвета в палитре Xterm256, который наиболее близок к заданному цвету.
 */
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
    //Возвращаемое значение "16 + min_index" используется для
    // преобразования индекса цвета в палитре Xterm256. В Xterm256
    // палитра содержит 256 цветов, и она разделена на две части:
    // первые 16 цветов являются стандартными ANSI цветами,
    // а остальные 240 цветов - это цвета, которые
    // можно настроить пользователем. Таким образом,
    // чтобы указать на один из цветов в диапазоне
    // от 16 до 255, мы добавляем 16 к индексу, чтобы
    // получить соответствующий индекс в палитре Xterm256.
}

/**
 * @brief Выполняет интерполяцию между двумя заданными цветами в формате RGB.
 *
 * @param start Указатель на структуру rgb_c, представляющую начальный цвет.
 * @param end Указатель на структуру rgb_c, представляющую конечный цвет.
 * @param out Указатель на структуру rgb_c, куда будет записан результирующий цвет.
 * @param factor Параметр интерполяции, указывающий на то, насколько близко к конечному цвету
 *               должен быть результирующий цвет. Значение factor равное 0 соответствует начальному цвету,
 *               а значение равное 1 соответствует конечному цвету. Промежуточные значения factor лежат между 0 и 1.
 */
void rgbInterpolate(union rgb_c *start, union rgb_c *end, union rgb_c *out, double factor) {
    out->b = start->b + (end->b - start->b) * factor;
    out->r = start->r + (end->r - start->r) * factor;
    out->g = start->g + (end->g - start->g) * factor;
}

int wcwidth(wchar_t wc);

int main(int argc, char **argv) {
    char *defaultArgv[] = {"-"}; // Массив для хранения аргументов командной строки по умолчанию
    double freq_h = 0.23; // Горизонтальная частота радуги по умолчанию
    double freq_v = 0.01; // Вертикальная частота радуги по умолчанию
    int startColor = 0; // Начальный цвет радуги
    union rgb_c rgb_start; // Начальный цвет радуги в формате RGB
    union rgb_c rgb_end; // Конечный цвет радуги в формате RGB

    int stringCount = 0;
    int colorIndex = -1;

    int hasColor = isatty(STDOUT_FILENO); // Флаг, указывающий на наличие цветного вывода

    struct timeval timeVal; // Структура для хранения времени
    gettimeofday(&timeVal, NULL); // Получение текущего времени
    double offX = (timeVal.tv_sec % 300) / 300.0; // Отклонение по горизонтали


    int seed = time(NULL); // сид для генерации случайных чисел
    int errCode = OK;
    Flags flags = {false, true, false, false, false, false, false, false}; // Иницилизация структуры флагов
    char *flagsString = ":h:v:s:g:flrobxi?"; // Строка с опциями командной строки
    int flagSymbol;

    struct option longFlags[] = {{"horizontal-frequency", 0, NULL, 'h'}, // Длинные опции командной строки
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

    // Обработка опций командной строки
    while (((flagSymbol = getopt_long(argc, argv, flagsString, longFlags, NULL)) != -1)) {
        errCode = initStruct(&flags, flagSymbol, &seed, &freq_h, &freq_v, &startColor, &rgb_start, &rgb_end);
    }

    if (errCode != ERROR) {
        // Проверка на одновременное указание опций --24bit и --16color
        if (flags.b && flags.x) {
            wprintf(L"Only one of --24bit and --16color can be given at a time\n");
            exit(ERROR);
        }
    }

    // обработка флага --help
    if (flags.help) {
        printf("%s", helpStr); // Вывод справки
        return 0;
    }

    // Проверка флага --gradient
    if (flags.g) {
        // Проверка конфликтующего флага --16color
        if (flags.x) {
            wprintf(L"--gradient and --16color are mutually exclusive\n");
            exit(2);
        }

        // Если не указан флаг --24bit
        if (!flags.b) {
            size_t codesGradientSize = ARRAY_SIZE(codesGradient); // Размер массива цветов радуги
            double correctionFactor = 2 * codesGradientSize / (double)ARRAY_SIZE(codes); // Корректировочный коэффициент для частот
            freq_h *= correctionFactor; // Коррекция горизонтальной частоты
            freq_v *= correctionFactor; // Коррекция вертикальной частоты

            // Генерация цветов радуги
            for (size_t i = 0; i < codesGradientSize; ++i) {
                double factor = i / (double)(codesGradientSize - 1); // Фактор интерполяции
                union rgb_c rgb_intermediate; // Промежуточный цвет в формате RGB
                rgbInterpolate(&rgb_start, &rgb_end, &rgb_intermediate, factor); // Интерполяция цветов
                codesGradient[i] = xterm256LookLike(&rgb_intermediate); // Определение ближайшего цвета из палитры xterm256
            }
        }
    }

    // Обработка флага --invert
    if (flags.i) {
        if (flags.x) {
            wprintf(L"\033[30m\n"); // Установка цвета фона
        } else {
            wprintf(L"\033[38;5;16m\n"); // Установка цвета текста
        }
    }

    int randomOffset = 0; // Смещение для генерации случайных чисел

    // Генерация случайного смещения, если указан флаг --random
    if (flags.r) {
        srand(seed);
        randomOffset = rand();
    }

    char **inputsBegin = argv + optind; // Указатель на начало аргументов командной строки
    char **inputsEnd = argv + argc; // Указатель на конец аргументов командной строки

    // Если нет переданных файлов в аргументах командной строки, используем стандартный ввод
    if (inputsBegin == inputsEnd) {
        inputsBegin = defaultArgv;
        inputsEnd = inputsBegin + 1;
    }

    char *envLang = getenv("LANG");  // return en_GB.UTF-8

    // Установка локали
    if (flags.l && envLang && !strstr(envLang, "UTF-8")) {
        if (!setlocale(LC_ALL, "C.UTF-8")) {
            setlocale(LC_ALL, ""); // Использование текущей локали
        }
    } else {
        setlocale(LC_ALL, ""); // Использование текущей локали
    }

    int charCountInStr = 0; // Счетчик символов в строке

    // Чтение и обработка файлов
    for (char **fileName = inputsBegin; fileName < inputsEnd; fileName++) {
        FILE *filePtr;
        int escapeState = NONE; // Состояние управляющей последовательности
        char c; // Текущий символ

        if (!strcmp(*fileName, "-")) {
            filePtr = stdin; // Использование стандартного ввода
        } else {
            // Открытие файла для чтения
            if ((filePtr = fopen(*fileName, "r")) == NULL) {
                // Вывод сообщения об ошибке, если файл не удалось открыть
                fwprintf(stderr, L"Cannot open input file \"%s\": %s\n", *fileName, strerror(errno));
                return ERROR;
            }
        }

        // Построчное чтение файла
        while (fread(&c, 1, 1, filePtr) > 0) {
            // Если включен цветной вывод
            if (hasColor) {
                // Обработка управляющих последовательностей
                escapeState = findEscapeSequences(c, escapeState);

                // Если необходимо вывести символ
                if (escapeState == ESC_CSI_TERM) {
                    putwchar(c);
                }

                // Если управляющая последовательность завершена
                if (escapeState == NONE || escapeState == ESC_CSI_TERM) {
                    if (c == '\n') {
                        stringCount++; // Увеличение счетчика строк
                        charCountInStr = 0; // Обнуление счетчика символов в строке

                        // Если включен флаг инверсии цвета
                        if (flags.i) {
                            wprintf(L"\033[49m"); // Установка цвета фона
                        }
                    } else {
                        // Если управляющая последовательность завершена
                        if (escapeState == NONE) {
                            charCountInStr += wcwidth(c); // Увеличение счетчика символов в строке
                        }

                        // Если включен флаг --24bit
                        if (flags.b) {
                            // Вычисление параметра угла
                            float theta = charCountInStr * freq_h / 5.0f + stringCount * freq_v +
                                          PI * (offX + 2.0f * (randomOffset + startColor) / (double)RAND_MAX);

                            union rgb_c color;

                            // Если включен флаг --gradient
                            if (flags.g) {
                                // Корректировка угла для градиента
                                theta = fmodf(theta / 2.0f / PI, 2.0f);

                                // Если угол больше 1, отражаем его
                                if (theta > 1.0f) {
                                    theta = 2.0f - theta;
                                }

                                // Интерполяция цвета для градиента
                                rgbInterpolate(&rgb_start, &rgb_end, &color, theta);
                            } else {
                                // Вычисление составляющих цвета для радуги
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

                            // Вывод управляющей последовательности для цвета
                            wprintf(L"\033[%d;2;%d;%d;%dm", (flags.i ? 48 : 38), color.r, color.g, color.b);
                        // Если включен флаг --16color
                        } else if (flags.x) {
                            int newColorIndex = offX * ARRAY_SIZE(codes16) + (int)(charCountInStr * freq_h + stringCount * freq_v);

                            if (colorIndex != newColorIndex || escapeState == ESC_CSI_TERM) {
                                wprintf(L"\033[%hhum", (flags.i ? 10 : 0) +
                                                           codes16[(randomOffset + startColor + (colorIndex = newColorIndex)) %
                                                                   ARRAY_SIZE(codes16)]);
                            }

                        } else {
                            // Если включен флаг --gradient
                            if (flags.g) {
                                int newColorIndex = offX * ARRAY_SIZE(codesGradient) + (int)(charCountInStr * freq_h + stringCount * freq_v);

                                if (colorIndex != newColorIndex || escapeState == ESC_CSI_TERM) {
                                    size_t lookup = (randomOffset + startColor + (colorIndex = newColorIndex)) %
                                                    (2 * ARRAY_SIZE(codesGradient));

                                    if (lookup >= ARRAY_SIZE(codesGradient)) {
                                        lookup = 2 * ARRAY_SIZE(codesGradient) - 1 - lookup;
                                    }

                                    // Вывод управляющей последовательности для цвета
                                    wprintf(L"\033[%d;5;%hhum", (flags.i ? 48 : 38), codesGradient[lookup]);
                                }
                            } else {
                                // Если не включен флаг --gradient
                                int newColorIndex = offX * ARRAY_SIZE(codes) + (int)(stringCount * freq_h + stringCount * freq_v);
                                if (colorIndex != newColorIndex || escapeState == ESC_CSI_TERM) {
                                    // Вывод управляющей последовательности для цвета
                                    wprintf(
                                        L"\033[%d;5;%hhum", (flags.i ? 48 : 38),
                                        codes[(randomOffset + startColor + (colorIndex = newColorIndex)) % ARRAY_SIZE(codes)]);
                                }
                            }
                        }
                    }
                }
            }

            // Если управляющая последовательность завершена
            if (escapeState != ESC_CSI_TERM) {
                putwchar(c); // Вывод символа
            }
        }

        // Восстановление стандартного цвета после окончания обработки файла
        if (hasColor) {
            wprintf(L"\033[0m"); // Сброс цвета
        }

        // Закрытие файла, если он был открыт
        if (filePtr) {
            // Если возникла ошибка при чтении файла
            if (ferror(filePtr)) {
                fwprintf(stderr, L"Error reading input file \"%s\": %s\n", *fileName, strerror(errno));
                fclose(filePtr);
                return ERROR;
            }

            // Если возникла ошибка при закрытии файла
            if (fclose(filePtr)) {
                fwprintf(stderr, L"Error closing input file \"%s\": %s\n", *fileName, strerror(errno));
                return ERROR;
            }
        }
    }

    return errCode;
}
