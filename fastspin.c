/*
 * Spin to C/C++ translator
 * Copyright 2011-2016 Total Spectrum Software Inc.
 * 
 * +--------------------------------------------------------------------
 * ¦  TERMS OF USE: MIT License
 * +--------------------------------------------------------------------
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * +--------------------------------------------------------------------
 */

// this version of the front end uses the openspin command line flags
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>
#include "spinc.h"
#include "preprocess.h"
#include "version.h"

//#define DEBUG_YACC

extern int yyparse(void);

extern int yydebug;

const char *gl_progname;
const char *gl_cc = NULL;
const char *gl_intstring = "int32_t";

Module *current;
Module *allparse;
Module *globalModule;

int gl_p2;
int gl_errors;
int gl_output;
int gl_outputflags;
int gl_nospin;
int gl_gas_dat;
int gl_normalizeIdents;
int gl_debug;
int gl_expand_constants;
int gl_optimize_flags;
int gl_dat_offset;
AST *ast_type_word, *ast_type_long, *ast_type_byte;
AST *ast_type_float, *ast_type_string;
AST *ast_type_generic;
AST *ast_type_void;

const char *gl_outname = NULL;

static void
PrintInfo(void)
{
    fprintf(stderr, "Propeller Spin/PASM Compiler 'FastSpin' (c) 2011-2016 Total Spectrum Software Inc.\n");
    fprintf(stderr, "Version " VERSIONSTR " Compiled on: " __DATE__ "\n");
}

static void
Usage(void)
{
    fprintf(stderr, "usage: %s\n", gl_progname);
    fprintf(stderr, "  [ -h ]              display this help\n");
    fprintf(stderr, "  [ -L or -I <path> ] add a directory to the include path\n");
    fprintf(stderr, "  [ -o ]             output filename\n");
    fprintf(stderr, "  [ -b ]             output binary file format\n");
    fprintf(stderr, "  [ -e ]             output eeprom file format\n");
    fprintf(stderr, "  [ -c ]             output only DAT sections\n");
    fprintf(stderr, "  [ -f ]             output list of file names\n");
    fprintf(stderr, "  [ -q ]             quiet mode (suppress banner and non-error text)\n");
    fprintf(stderr, "  [ -p ]             disable the preprocessor\n");
    fprintf(stderr, "  [ -2 ]             compile for Prop2\n");
    fprintf(stderr, "  [ -D <define> ]    add a define\n");
    exit(2);
}

#define MAX_CMDLINE 4096
static char cmdline[MAX_CMDLINE];

#define needsquote(x) (needEscape && ((x) == ' '))

static void
appendWithoutSpace(const char *s, int needEscape)
{
    int len = 0;
    const char *src = s;
    char *dst = cmdline;
    int c;
    int addquote = 0;

    // move dst to the end of cmdline, and count up
    // the size
    while (*dst) {
      dst++;
      len++;
    }
    // check to see if "s" contains any spaces; if so,
    // we will have to escape those
    while (*s) {
      if (needsquote(*s))
	addquote = 1;
      s++;
      len++;
    }
    if (addquote) len += 2;
    if (len >= MAX_CMDLINE) {
        fprintf(stderr, "command line too long: aborting");
	exit(2);
    }
    // now actually copy it in
    if (addquote)
      *dst++ = '"';
    do {
      c = *src++;
      if (!c) break;
      *dst++ = c;
    } while (c);
    if (addquote)
      *dst++ = '"';
    *dst++ = 0;
}

static void
appendToCmd(const char *s)
{
    if (cmdline[0] != 0)
      appendWithoutSpace(" ", 0);
    appendWithoutSpace(s, 1);
}

static void
appendCompiler(const char *ccompiler)
{
    cmdline[0] = 0;
    if (!ccompiler) {
        ccompiler = getenv("CC");
    }
    if (!ccompiler) {
        ccompiler = "propeller-elf-gcc";
    }
    appendToCmd(ccompiler);
}

void
PrintFileSize(const char *fname)
{
    FILE *f = fopen(fname, "rb");
    unsigned len;

    if (f) {
        fseek(f, 0L, SEEK_END);
        len = ftell(f);
        fclose(f);
        printf("Program size is %u bytes\n", len);
    }
}

int
main(int argc, char **argv)
{
    int outputMain = 0;
    int outputDat = 0;
    int outputFiles = 0;
    int outputBin = 0;
    int outputAsm = 0;
    int compile = 0;
    int quiet = 0;
    Module *P;
    int retval = 0;
    struct flexbuf argbuf;
    time_t timep;
    int i;
    const char *outname = NULL;
    size_t eepromSize = 32768;
    int useEeprom = 0;
    
    /* Initialize the global preprocessor; we need to do this here
       so that the -D command line option can define preprocessor
       symbols. The rest of initialization happens after command line
       options have been parsed
    */
    InitPreprocessor();

    /* save our command line arguments and comments describing
       how we were run
    */
    flexbuf_init(&argbuf, 128);
    flexbuf_addstr(&argbuf, "//\n// automatically generated by spin2cpp v" VERSIONSTR " on ");
    time(&timep);
    flexbuf_addstr(&argbuf, asctime(localtime(&timep)));
    flexbuf_addstr(&argbuf, "// ");
    for (i = 0; i < argc; i++) {
        flexbuf_addstr(&argbuf, argv[i]);
        flexbuf_addchar(&argbuf, ' ');
    }
    flexbuf_addstr(&argbuf, "\n//\n\n");
    flexbuf_addchar(&argbuf, 0);
    gl_header = flexbuf_get(&argbuf);
    gl_output = OUTPUT_ASM;
    gl_outputflags = OUTFLAGS_DEFAULT;
    
    allparse = NULL;
#ifdef DEBUG_YACC
    yydebug = 1;  /* turn on yacc debugging */
#endif
    /* parse arguments */
    if (argv[0] != NULL) {
        gl_progname = argv[0];
        argv++; --argc;
    }
    gl_normalizeIdents = 1;
    compile = 1;
    outputMain = 1;
    outputBin = 1;
    outputAsm = 1;
    gl_optimize_flags |= OPT_REMOVE_UNUSED_FUNCS;
    appendCompiler(gl_progname);
    appendToCmd("-q");
    appendToCmd("--dat");
    appendToCmd("--binary");
    
    // put everything in HUB by default
    gl_outputflags &= ~OUTFLAG_COG_DATA;
    gl_outputflags &= ~OUTFLAG_COG_CODE;
    
    while (argv[0] && argv[0][0] == '-') {
        if (!strcmp(argv[0], "-y")) {
            yydebug = 1;
            argv++; --argc;
        } else if (!strncmp(argv[0], "--data=", 7)) {
            if (!strcmp(argv[0]+7, "cog")) {
                gl_outputflags |= OUTFLAG_COG_DATA;
            } else if (!strcmp(argv[0]+7, "hub")) {
                gl_outputflags &= ~OUTFLAG_COG_DATA;
            } else {
                fprintf(stderr, "Unknown --data= choice: %s\n", argv[0]);
                Usage();
            }
            argv++; --argc;
        } else if (!strncmp(argv[0], "--code=", 7)) {
            if (!strcmp(argv[0]+7, "cog")) {
                gl_outputflags |= OUTFLAG_COG_CODE;
            } else if (!strcmp(argv[0]+7, "hub")) {
                gl_outputflags &= ~OUTFLAG_COG_CODE;
            } else {
                fprintf(stderr, "Unknown --code= choice: %s\n", argv[0]);
                Usage();
            }
            argv++; --argc;
        } else if (!strcmp(argv[0], "-c") || !strncmp(argv[0], "--dat", 5)) {
            compile = 0;
            outputMain = 0;
            gl_output = OUTPUT_DAT;
            outputDat = 1;
            argv++; --argc;
        } else if (!strcmp(argv[0], "-p2")) {
            gl_p2 = 1;
            argv++; --argc;
        } else if (!strcmp(argv[0], "-f")) {
            outputFiles = 1;
            quiet = 1;
            argv++; --argc;
        } else if (!strcmp(argv[0], "-q")) {
            quiet = 1;
            argv++; --argc;
        } else if (!strcmp(argv[0], "-2")) {
            gl_p2 = 1;
            argv++; --argc;
        } else if (!strcmp(argv[0], "-h")) {
            PrintInfo();
            Usage();
            exit(0);
        } else if (!strncmp(argv[0], "--bin", 5) || !strcmp(argv[0], "-b")
                   || !strcmp(argv[0], "-e"))
        {
            compile = 1;
            outputMain = 1;
	    outputBin = 1;
            if (!strcmp(argv[0], "-e")) {
                useEeprom = 1;
            }
            gl_optimize_flags |= OPT_REMOVE_UNUSED_FUNCS;
            if (gl_output == OUTPUT_ASM) {
                appendCompiler(gl_progname);
                appendToCmd("--dat");
                appendToCmd(argv[0]);
                appendToCmd("-q");
            } else {
                appendCompiler(NULL);
            }
            argv++; --argc;
        } else if (!strcmp(argv[0], "-p")) {
            gl_preprocess = 0;
            argv++; --argc;
	} else if (!strncmp(argv[0], "-o", 2)) {
	    char *opt;
	    opt = argv[0];
            argv++; --argc;
            if (opt[2] == 0) {
                if (argv[0] == NULL) {
                    fprintf(stderr, "Error: expected another argument after -o\n");
                    exit(2);
                }
                opt = argv[0];
		argv++; --argc;
            } else {
                opt += 2;
            }
	    gl_outname = outname = strdup(opt);
        } else if (!strncmp(argv[0], "-g", 2)) {
            if (compile) {
                appendToCmd(argv[0]);
            }
            argv++; --argc;
            gl_debug = 1;
        } else if (!strncmp(argv[0], "-D", 2) || !strncmp(argv[0], "-C", 2)) {
            char *opt = argv[0];
            char *name;
            char optchar[3];
            argv++; --argc;
            // save the -D or -C
            strncpy(optchar, opt, 2);
            optchar[2] = 0;
            if (opt[2] == 0) {
                if (argv[0] == NULL) {
                    fprintf(stderr, "Error: expected another argument after %s\n", optchar);
                    exit(2);
                }
                opt = argv[0];
                argv++; --argc;
            } else {
                opt += 2;
            }
            /* if we are compiling, pass this on to the compiler too */
            if (compile) {
                appendToCmd(optchar);
                appendToCmd(opt);
            }
            opt = strdup(opt);
            name = opt;
            while (*opt && *opt != '=')
                opt++;
            if (*opt) {
                *opt++ = 0;
            } else {
                opt = "1";
            }
            pp_define(&gl_pp, name, opt);
        } else if (!strncmp(argv[0], "-L", 2) || !strncmp(argv[0], "-I", 2)) {
            char *opt = argv[0];
            char *incpath;
            char optchar[3];
            argv++; --argc;
            // save the -L or -I
            strncpy(optchar, opt, 2);
            optchar[2] = 0;
            if (opt[2] == 0) {
                if (argv[0] == NULL) {
                    fprintf(stderr, "Error: expected another argument after %s\n", optchar);
                    exit(2);
                }
                opt = argv[0];
                argv++; --argc;
            } else {
                opt += 2;
            }
            /* if we are compiling, pass this on to the compiler too */
            if (compile) {
                appendToCmd(optchar);
                appendToCmd(opt);
            }
            opt = strdup(opt);
            incpath = opt;
            pp_add_to_path(&gl_pp, incpath);
        } else {
            fprintf(stderr, "Unrecognized option: %s\n", argv[0]);
            Usage();
        }
    }

    if (!quiet) {
        PrintInfo();
    }
    if (argv[0] == NULL || (argc != 1 && !compile)) {
        Usage();
    }

    /* add some predefined symbols */
    
    pp_define(&gl_pp, "__FASTSPIN__", str_(VERSION_MAJOR));
    pp_define(&gl_pp, "__SPINCVT__", str_(VERSION_MAJOR));
    if (gl_output == OUTPUT_ASM) {
        pp_define(&gl_pp, "__SPIN2PASM__", "1");
    }
    if (gl_output == OUTPUT_CPP || gl_output == OUTPUT_C) {
        pp_define(&gl_pp, "__SPIN2CPP__", "1");
        if (gl_output == OUTPUT_CPP) {
            pp_define(&gl_pp, "__cplusplus", "1");
        }
    }
    if (gl_p2) {
        pp_define(&gl_pp, "__P2__", "1");
    }
    /* set up the binary offset */
    gl_dat_offset = -1; // by default offset is unknown
    if ( (gl_output == OUTPUT_DAT||gl_output == OUTPUT_ASM) && outputBin) {
        // a 32 byte spin header is prepended to binary output of dat
        // (but not in p2 mode)
        gl_dat_offset = gl_p2 ? 0 : 32;
    } else if (gl_output == OUTPUT_DAT && gl_gas_dat) {
        // GAS output for dat uses symbols, so @@@ is OK there
        gl_dat_offset = 0;
    }

    /* initialize the parser; we do that after command line processing
       so that command line options can influence it */
    Init();

    /* now actually parse the file */
    if (!quiet) {
        gl_printprogress = 1;
    }
    P = ParseFile(argv[0]);
    if (compile && argc > 1) {
        if (gl_p2) {
            appendToCmd("-2");
        }
        /* append the remaining arguments to the command line */
        for (i = 1; i < argc; i++) {
            appendToCmd(argv[i]);
        }
    }

    if (outputFiles) {
        Module *Q;
        for (Q = allparse; Q; Q = Q->next) {
            printf("%s\n", Q->fullname);
        }
        return 0;
    }
    
    if (P) {
        /* do type checking and deduction */
        int changes;
        Module *Q;
        MarkUsed(P->functions);
        for (Q = allparse; Q; Q = Q->next) {
            ProcessFuncs(Q);
        }
        do {
            changes = 0;
            for (Q = allparse; Q; Q = Q->next) {
                changes += InferTypes(Q);
            }
        } while (changes != 0);
        if (gl_errors > 0) {
            exit(1);
        }
        for (Q = allparse; Q; Q = Q->next) {
            SpinTransform(Q);
        }
        AssignObjectOffsets(P);
        if (outputDat) {
            outname = gl_outname;
            if (gl_gas_dat) {
	        if (!outname) {
                    outname = ReplaceExtension(P->fullname, ".S");
                }
                OutputGasFile(outname, P);
            } else {
	        if (!outname) {
                    if (outputBin) {
                        if (useEeprom) {
                            outname = ReplaceExtension(P->fullname, ".eeprom");
                        } else {
                            outname = ReplaceExtension(P->fullname, ".binary");
                        }
                    } else {
                        outname = ReplaceExtension(P->fullname, ".dat");
                    }
                }
                OutputDatFile(outname, P, outputBin);
                if (outputBin) {
                    DoPropellerChecksum(outname, useEeprom ? eepromSize : 0);
                }
            }
        } else if (outputAsm) {
            const char *binname = NULL;
            const char *asmname = NULL;
            if (compile) {
                binname = gl_outname;
                if (binname) {
                    asmname = ReplaceExtension(binname, gl_p2 ? ".p2asm" : ".pasm");
                } else {
                    if (useEeprom) {
                        binname = ReplaceExtension(P->fullname, ".eeprom");
                    } else {
                        binname = ReplaceExtension(P->fullname, ".binary");
                    }
                }
            } else {
                asmname = gl_outname;
            }
            if (!asmname) {
                asmname = ReplaceExtension(P->fullname, gl_p2 ? ".p2asm" : ".pasm");
            }
            OutputAsmCode(asmname, P, outputMain);
            if (compile && !gl_p2)  {
                appendToCmd("-o");
                appendToCmd(binname);
                appendToCmd(asmname);
                retval = system(cmdline);
                if (retval < 0) {
                    fprintf(stderr, "Unable to run command: %s\n", cmdline);
                    exit(1);
                }
                if (!quiet) {
                    printf("Done.\n");
                    if (retval == 0) {
                        PrintFileSize(binname);
                    }
                }
            }
        } else {
            fprintf(stderr, "fastspin cannot convert to C\n");
        }
    } else {
        fprintf(stderr, "parse error\n");
        return 1;
    }

    if (gl_errors > 0) {
        exit(1);
    }
    return retval;
}
