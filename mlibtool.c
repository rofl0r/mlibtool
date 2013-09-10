/* A miniscule version of libtool for sane systems. On insane systems, requires
 * that true libtool be installed. */

/*
 * Copyright (c) 2013 Gregor Richards
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define _XOPEN_SOURCE 500

/* headers used in written .l* files */
#define SANE_HEADER "# SYSTEM_IS_SANE\n"
#define PACKAGE "libtool (mlibtool) 0.1"
#define PACKAGE_HEADER "# Generated by " PACKAGE "\n"

#define SF(into, func, bad, args) do { \
    (into) = func args; \
    if ((into) == (bad)) { \
        perror(#func); \
        exit(1); \
    } \
} while (0)

/* make sure we're POSIX */
#if defined(unix) || defined(__unix) || defined(__unix__)
#include <unistd.h>
#endif

#ifndef _POSIX_VERSION
/* not even POSIX, this system can't possibly be sane */
int execv(const char *path, char *const argv[]);
int main(int argc, char **argv)
{
    execv(argv[1], argv + 1);
    return 1;
}
#else

#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#define SANE "__linux__ || " /* Linux */ \
             "__FreeBSD_kernel__ || __NetBSD__ || " \
             "__OpenBSD__ || __DragonFly__ || " /* BSD family */ \
             "__GNU__" /* GNU Hurd */

/* a simple buffer type for our persistent char ** commands */
struct Buffer {
    char **buf;
    size_t bufused, bufsz;
};

#define BUFFER_DEFAULT_SZ 8

#define INIT_BUFFER(ubuf) do { \
    struct Buffer *buf_ = &(ubuf); \
    SF(buf_->buf, malloc, NULL, (BUFFER_DEFAULT_SZ * sizeof(char *))); \
    buf_->bufused = 0; \
    buf_->bufsz = BUFFER_DEFAULT_SZ; \
} while (0)

#define WRITE_BUFFER(ubuf, val) do { \
    struct Buffer *buf_ = &(ubuf); \
    while (buf_->bufused >= buf_->bufsz) { \
        buf_->bufsz *= 2; \
        SF(buf_->buf, realloc, NULL, (buf_->buf, buf_->bufsz * sizeof(char *))); \
    } \
    buf_->buf[buf_->bufused++] = (val); \
} while (0)

#define FREE_BUFFER(ubuf) do { \
    free((ubuf).buf); \
} while (0)

/* Is this system sane? */
static int systemIsSane(char *cc)
{
    pid_t pid;
    int pipei[2], pipeo[2];
    int tmpi, sane, insane = 0;
    size_t i, bufused;
    ssize_t rd;
#define BUFSZ 32
    char buf[BUFSZ];

    /* We determine if it's sane by asking the preprocessor */
    static const char *sanityCheck =
        "#if " SANE "\n"
        "SYSTEM_IS_SANE\n"
        "#endif";

    SF(tmpi, pipe, -1, (pipei));
    SF(tmpi, pipe, -1, (pipeo));
    SF(pid, fork, -1, ());
    if (pid == 0) {
        /* child process, read in preproc commands */
        SF(tmpi, dup2, -1, (pipei[0], 0));
        close(pipei[0]); close(pipei[1]);
        SF(tmpi, dup2, -1, (pipeo[1], 1));
        close(pipeo[0]); close(pipeo[1]);

        /* and spawn the preprocessor */
        execlp(cc, cc, "-E", "-", NULL);
        perror(cc);
        exit(1);
    }
    close(pipei[0]);
    close(pipeo[1]);

    /* now send it the check */
    i = strlen(sanityCheck);
    if (write(pipei[1], sanityCheck, i) != i)
        insane = 1;
    close(pipei[1]);

    /* and read its input */
    sane = 0;
    bufused = 0;
    while ((rd = read(pipeo[0], buf + bufused, BUFSZ - bufused)) > 0 ||
           bufused) {
        if (rd > 0)
            bufused += rd;

        if (!strncmp(buf, "SYSTEM_IS_SANE", 14))
            sane = 1;

        for (i = 0; i < bufused && buf[i] != '\n'; i++);
        if (i < bufused) i++;
        memcpy(buf, buf + i, bufused - i);
        bufused -= i;
    }
    close(pipeo[0]);

    /* then wait for it */
    if (waitpid(pid, &tmpi, 0) != pid)
        insane = 1;
    if (tmpi != 0)
        insane = 1;

    /* finished */
    if (insane) sane = 0;
    return sane;
}

enum Mode {
    MODE_UNKNOWN = 0,
    MODE_COMPILE,
    MODE_LINK
};


struct Options {
    int dryRun, quiet, argc;
    char **argv, **cmd;
};

/* redirect to libtool */
static void execLibtool(struct Options *opt)
{
    execvp(opt->argv[1], opt->argv + 1);
    perror(opt->argv[1]);
    exit(1);
}

/* Generic function to spawn a child and wait for it. If the child fails and
 * retryIfFail is set, then execLibtool will be called. If the child fails and
 * retryIfFail is unset, then exit(1). */
static void spawn(struct Options *opt, char *const *cmd, int retryIfFail)
{
    size_t i;
    int fail = 0;

    /* output the command */
    if (!opt->quiet) {
        fprintf(stderr, "mlibtool:");
        for (i = 0; cmd[i]; i++)
            fprintf(stderr, " %s", cmd[i]);
        fprintf(stderr, "\n");
    }

    /* and run it */
    if (!opt->dryRun) {
        pid_t pid;
        int tmpi;
        SF(pid, fork, -1, ());
        if (pid == 0) {
            execvp(cmd[0], cmd);
            perror(cmd[0]);
            exit(1);
        }
        if (waitpid(pid, &tmpi, 0) != pid) {
            perror(cmd[0]);
            fail = 1;
        } else if (tmpi != 0)
            fail = 1;
    }

    if (fail) {
        if (retryIfFail) {
            execLibtool(opt);
        } else {
            exit(1);
        }
    }
}

/* Check for sanity by reading a .lo file. If cc is provided, fall back to that
 * if no .lo files are found. */
static int checkLoSanity(struct Options *opt, char *cc)
{
    int sane = 0, foundlo = 0;
    size_t i;

    /* look for a .lo file and check it for sanity */
    for (i = 1; opt->cmd[i]; i++) {
        char *arg = opt->cmd[i];
        if (arg[0] != '-') {
            char *ext = strrchr(arg, '.');
            if (ext && (!strcmp(ext, ".lo") || !strcmp(ext, ".la"))) {
                FILE *f;

                foundlo = 1;
                f = fopen(arg, "r");
                if (f) {
                    char buf[sizeof(SANE_HEADER)];
                    fgets(buf, sizeof(SANE_HEADER), f);
                    if (!strcmp(buf, SANE_HEADER)) sane = 1;
                    fclose(f);
                    break;
                }
            }
        }
    }

    if (!foundlo && cc)
        return systemIsSane(cc);

    return sane;
}


static void usage();
static void ltcompile(struct Options *);
static void ltlink(struct Options *);

int main(int argc, char **argv)
{
    int argi;

    /* options */
    struct Options opt;
    int insane = 0;
    char *modeS = NULL;
    enum Mode mode = MODE_UNKNOWN;
    int sane = 0;
    memset(&opt, 0, sizeof(opt));

    /* first argument must be target libtool */

    /* collect arguments up to --mode */
    for (argi = 2; argi < argc; argi++) {
        char *arg = argv[argi];

        if (!strcmp(arg, "-n") || !strcmp(arg, "--dry-run")) {
            opt.dryRun = 1;

        } else if (!strcmp(arg, "--quiet") ||
                   !strcmp(arg, "--silent")) {
            opt.quiet = 1;

        } else if (!strcmp(arg, "--no-quiet") ||
                   !strcmp(arg, "--no-silent")) {
            opt.quiet = 0;

        } else if (!strcmp(arg, "--version")) {
            printf("%s\n", PACKAGE);
            exit(0);

        } else if (!strcmp(arg, "-h") || !strcmp(arg, "--help")) {
            usage();
            exit(0);

        } else if (!strncmp(arg, "--mode=", 7) && argi < argc - 1) {
            modeS = arg + 7;
            argi++;
            break;

        } else if (!strncmp(arg, "--tag=", 6) ||
                   !strcmp(arg, "-v") ||
                   !strcmp(arg, "--verbose") ||
                   !strcmp(arg, "--no-verbose")) {
            /* ignored for compatibility */

        } else {
            insane = 1;

        }
    }
    opt.argc = argc;
    opt.argv = argv;
    opt.cmd = argv + argi;

    if (!modeS) {
        usage();
        exit(1);
    }

    /* check the mode */
    if (!strcmp(modeS, "compile")) {
        mode = MODE_COMPILE;
    } else if (!strcmp(modeS, "link")) {
        mode = MODE_LINK;
    }

    /* next argument is the compiler, use that to check for sanity */
    if (!insane) {
        if (mode == MODE_COMPILE) {
            sane = systemIsSane(opt.cmd[0]);
        } else if (mode == MODE_LINK) {
            sane = checkLoSanity(&opt, opt.cmd[0]);
        }
    }

    if (!sane) {
        /* just go to libtool */
        execLibtool(&opt);

    } else if (mode == MODE_COMPILE) {
        ltcompile(&opt);

    } else if (mode == MODE_LINK) {
        ltlink(&opt);

    } else {
        exit(1);

    }

    return 0;
}

static void usage()
{
    printf("Use: mlibtool <target-libtool> [options] --mode=<mode> <command>\n"
        "Options:\n"
        "\t-n|--dry-run: display commands without modifying any files\n"
        "\t--mode=<mode>: user operation mode <mode>\n"
        "\n"
        "<mode> must be one of the following:\n"
        "\tcompile: compile a source file into a libtool object\n"
        /*"\texecute: automatically set library path, then run a program\n"*/
        /*"\tinstall: install libraries or executables\n"*/
        "\tlink: create a library or an executable\n"
        "\n");
    printf("mlibtool is a mini version of libtool for sensible systems. If you're\n"
        "compiling for Linux or BSD with supported invocation commands,\n"
        "<target-libtool> will never be called.\n"
        "\n"
        "Unrecognized invocations will be redirected to <target-libtool>.\n");
}

static void ltcompile(struct Options *opt)
{
    struct Buffer outCmd;
    size_t i;
    char *ext;
    FILE *f;

    /* options */
    char *outName = NULL;
    char *inName = NULL;
    size_t outNamePos = 0;
    int preferPic = 0, preferNonPic = 0;
    int buildPic = 0, buildNonPic = 0;

    /* option derivatives */
    char *outDirC = NULL,
         *outDir = NULL,
         *libsDir = NULL,
         *outBaseC = NULL,
         *outBase = NULL,
         *picFile = NULL,
         *nonPicFile = NULL;

    /* allocate the output command */
    INIT_BUFFER(outCmd);

    /* and copy it in */
    WRITE_BUFFER(outCmd, opt->cmd[0]);
    for (i = 1; opt->cmd[i]; i++) {
        char *arg = opt->cmd[i];
        char *narg = opt->cmd[i+1];

        if (arg[0] == '-') {
            if (!strcmp(arg, "-o") && narg) {
                /* output name */
                WRITE_BUFFER(outCmd, arg);
                SF(outName, strdup, NULL, (narg));
                outNamePos = outCmd.bufused;
                WRITE_BUFFER(outCmd, narg);
                i++;

            } else if (!strcmp(arg, "-prefer-pic") ||
                    !strcmp(arg, "-shared")) {
                preferPic = 1;

            } else if (!strcmp(arg, "-prefer-non-pic") ||
                    !strcmp(arg, "-static")) {
                preferNonPic = 1;

            } else if (!strncmp(arg, "-Wc,", 4)) {
                WRITE_BUFFER(outCmd, arg + 4);

            } else if (!strcmp(arg, "-no-suppress")) {
                /* ignored for compatibility */

            } else {
                WRITE_BUFFER(outCmd, arg);

            }

        } else {
            inName = arg;
            WRITE_BUFFER(outCmd, arg);

        }
    }

    /* if we don't have an input name, fail */
    if (!inName) {
        fprintf(stderr, "error: --mode=compile with no input file\n");
        exit(1);
    }

    /* if both preferPic and preferNonPic were specified, neither were specified */
    if (preferPic && preferNonPic)
        preferPic = preferNonPic = 0;

    if (preferPic || !preferNonPic)
        buildPic = 1;
    if (preferNonPic || !preferPic)
        buildNonPic = 1;

    /* if we don't have an output name, guess */
    if (!outName) {
        /* + 4: .lo\0 */
        SF(outName, malloc, NULL, (strlen(inName) + 4));
        strcpy(outName, inName);

        if ((ext = strrchr(outName, '.'))) {
            strcpy(ext, ".lo");
        } else {
            strcat(outName, ".lo");
        }

        /* and add it to the command */
        WRITE_BUFFER(outCmd, "-o");
        outNamePos = outCmd.bufused;
        WRITE_BUFFER(outCmd, outName);

    } else {
        /* make sure the output name includes .lo */
        fprintf(stderr, "%s\n", outName);
        if ((ext = strrchr(outName, '.'))) {
            if (strcmp(ext, ".lo")) {
                fprintf(stderr, "error: --mode=compile used to compile something other than a .lo file\n");
                exit(1);
            }
        } else {
            fprintf(stderr, "error: --mode=compile used to compile an executable\n");
            exit(1);
        }

    }

    /* get the directory names */
    SF(outDirC, strdup, NULL, (outName));
    outDir = dirname(outDirC);
    SF(outBaseC, strdup, NULL, (outName));
    outBase = basename(outBaseC);
    ext = strrchr(outBase, '.');
    if (ext) *ext = '\0';

    /* make the .libs dir */
    SF(libsDir, malloc, NULL, (strlen(outDir) + 7));
    sprintf(libsDir, "%s/.libs", outDir);
    if (!opt->dryRun) mkdir(libsDir, 0777); /* ignore errors */

    /* and generate the pic/non-pic names */
    SF(picFile, malloc, NULL, (strlen(libsDir) + strlen(outBase) + 7));
    SF(nonPicFile, malloc, NULL, (strlen(libsDir) + strlen(outBase) + 7));
    sprintf(picFile, "%s/%s.sh.o", libsDir, outBase);
    sprintf(nonPicFile, "%s/%s.st.o", libsDir, outBase);

    /* now do the actual building */
    if (buildNonPic) {
        outCmd.buf[outNamePos] = nonPicFile;
        WRITE_BUFFER(outCmd, NULL);
        spawn(opt, outCmd.buf, 0);
        outCmd.bufused--;

        if (!buildPic && !opt->dryRun)
            link(nonPicFile, picFile);

    }

    if (buildPic) {
        WRITE_BUFFER(outCmd, "-fPIC");
        WRITE_BUFFER(outCmd, "-DPIC");
        outCmd.buf[outNamePos] = picFile;

        WRITE_BUFFER(outCmd, NULL);
        spawn(opt, outCmd.buf, 0);
        outCmd.bufused--;

        if (!buildNonPic && !opt->dryRun)
            link(picFile, nonPicFile);
    }

    /* and finally, write the .lo file */
    f = fopen(outName, "w");
    if (!f) {
        perror(outName);
        exit(1);
    }
    fprintf(f, SANE_HEADER
               PACKAGE_HEADER
               "pic_object='.libs/%s.sh.o'\n"
               "non_pic_object='.libs/%s.st.o'\n",
               outBase, outBase);
    fclose(f);

    free(nonPicFile);
    free(picFile);
    free(libsDir);
    free(outBaseC);
    free(outDirC);
    free(outName);

    FREE_BUFFER(outCmd);
}

static void ltlink(struct Options *opt)
{
    struct Buffer outCmd, outAr, tofree;
    size_t i;
    char *ext;
    int tmpi;

    /* options */
    int major = 0,
        minor = 0,
        revision = 0,
        insane = 0;
    char *outName = NULL,
         *rpath = NULL;
    size_t outNamePos = 0;

    /* option derivatives */
    int buildBinary = 0,
        buildSo = 0,
        buildA = 0,
        retryIfFail = 0;
    char *outDirC = NULL,
         *outDir = NULL,
         *libsDir = NULL,
         *outBaseC = NULL,
         *outBase = NULL,
         *soname = NULL,
         *longname = NULL,
         *linkname = NULL;


    /* before we can even start, we have to figure out what we're building to
     * know whether to build the command out of .st.o or .sh.o files */
    for (i = 1; opt->cmd[i]; i++) {
        if (!strcmp(opt->cmd[i], "-o") && opt->cmd[i+1]) {
            outName = opt->cmd[i+1];
            break;
        }
    }

    if (outName) {
        ext = strrchr(outName, '.');
        if (ext && !strcmp(ext, ".la")) {
            /* it's a libtool library */
            buildA = 1;

        } else {
            /* it's a binary */
            buildBinary = 1;

        }
    }

    /* allocate our buffers */
    INIT_BUFFER(outCmd);
    INIT_BUFFER(outAr);
    INIT_BUFFER(tofree);

    WRITE_BUFFER(outCmd, opt->cmd[0]);
    WRITE_BUFFER(outCmd, "-L.libs");
    WRITE_BUFFER(outAr, "ar");
    WRITE_BUFFER(outAr, "rc");
    WRITE_BUFFER(outAr, "a.a"); /* to be replaced */

    /* read in the command */
    for (i = 1; opt->cmd[i]; i++) {
        char *arg = opt->cmd[i];
        char *narg = opt->cmd[i+1];

        if (arg[0] == '-') {
            if (!strcmp(arg, "-all-static")) {
                WRITE_BUFFER(outCmd, "-static");

            } else if (!strcmp(arg, "-export-dynamic")) {
                WRITE_BUFFER(outCmd, "-rdynamic");

            } else if (!strncmp(arg, "-L", 2)) {
                char *llibs;

                /* need both the -L path specified and .../.libs */
                WRITE_BUFFER(outCmd, arg);
                SF(llibs, malloc, NULL, (strlen(arg) + 7));
                sprintf(llibs, "%s/.libs", arg);
                WRITE_BUFFER(outCmd, llibs);
                WRITE_BUFFER(tofree, llibs);

            } else if (!strcmp(arg, "-o") && narg) {
                WRITE_BUFFER(outCmd, arg);
                outNamePos = outCmd.bufused;
                WRITE_BUFFER(outCmd, narg);
                i++;

            } else if (!strcmp(arg, "-rpath") && narg) {
                rpath = narg;
                i++;

            } else if (!strcmp(arg, "-version-info") && narg) {
                /* current:revision:age instead of major.minor.revision */
                int current = 0;
                revision = 0;

                /* if the format fails in any way, just use 0 */
                sscanf(narg, "%d:%d:%d", &current, &revision, &minor);
                if (minor > current) minor = current;
                major = current - minor;

                i++;

            } else if (!strncmp(arg, "-Wc,", 4)) {
                WRITE_BUFFER(outCmd, arg + 4);

            } else if (narg &&
                       (!strcmp(arg, "-Xcompiler") ||
                        !strcmp(arg, "-XCClinker"))) {
                WRITE_BUFFER(outCmd, narg);
                i++;

            } else if (!strcmp(arg, "-dlopen") ||
                       !strcmp(arg, "-dlpreopen") ||
                       !strcmp(arg, "-module") ||
                       !strcmp(arg, "-objectlist") ||
                       !strcmp(arg, "-precious-files-regex") ||
                       !strcmp(arg, "-release") ||
                       !strcmp(arg, "-shared") ||
                       !strcmp(arg, "-shrext") ||
                       !strcmp(arg, "-static") ||
                       !strcmp(arg, "-static-libtool-libs") ||
                       !strcmp(arg, "-weak")) {
                /* unsupported */
                insane = 1;

            } else if (narg &&
                       (!strcmp(arg, "-bindir") ||
                        !strcmp(arg, "-export-symbols") ||
                        !strcmp(arg, "-export-symbols-regex"))) {
                /* ignored for compatibility */
                i++;

            } else if (!strcmp(arg, "-no-fast-install") ||
                       !strcmp(arg, "-no-install") ||
                       !strcmp(arg, "-no-undefined")) {
                /* ignored for compatibility */

            } else {
                WRITE_BUFFER(outCmd, arg);

            }

        } else {
            ext = strrchr(arg, '.');
            if (ext && !strcmp(ext, ".lo")) {
                /* make separate versions for the .a and the .so */
                char *loDirC, *loDir, *loBaseC, *loBase, *loFull;

                /* OK, it's a .lo file, figure out the .libs name */
                SF(loDirC, strdup, NULL, (arg));
                loDir = dirname(loDirC);
                SF(loBaseC, strdup, NULL, (arg));
                loBase = basename(loBaseC);

                ext = strrchr(loBase, '.');
                if (ext) *ext = '\0';

                /* make the .libs/.o version */
                SF(loFull, malloc, NULL, (strlen(loDir) + strlen(loBase) + 13));
                sprintf(loFull, "%s/.libs/%s.s%c.o", loDir, loBase, (buildBinary ? 't' : 'h'));
                WRITE_BUFFER(outAr, loFull);
                WRITE_BUFFER(outCmd, loFull);
                WRITE_BUFFER(tofree, loFull);

                free(loBaseC);
                free(loDirC);

            } else if (ext && !strcmp(ext, ".la")) {
                /* link to this library */
                char *laDirC, *laDir, *laBaseC, *laBase, *aarg;
                int wholeArchive = 0;

                /* OK, it's a .la file, figure out the .libs name */
                SF(laDirC, strdup, NULL, (arg));
                laDir = dirname(laDirC);
                SF(laBaseC, strdup, NULL, (arg));
                laBase = basename(laBaseC);

                /* get the -l name */
                ext = strrchr(laBase, '.');
                if (ext) *ext = '\0';

                /* add -L for the .libs path */
                SF(aarg, malloc, NULL, (strlen(laDir) + 9));
                sprintf(aarg, "-L%s/.libs", laDir);
                WRITE_BUFFER(outCmd, aarg);
                WRITE_BUFFER(tofree, aarg);

                /* if there is no .so file, we need -Wl,--whole-archive */
                SF(aarg, malloc, NULL, (strlen(laDir) + strlen(laBase) + 11));
                sprintf(aarg, "%s/.libs/%s.so", laDir, laBase);
                if (access(aarg, F_OK) != 0) {
                    /* .a only */
                    wholeArchive = retryIfFail = 1;
                    WRITE_BUFFER(outCmd, "-Wl,--whole-archive");
                }
                free(aarg);

                /* and add -l<lib name> */
                if (!strncmp(laBase, "lib", 3)) laBase += 3;
                SF(aarg, malloc, NULL, (strlen(laBase) + 3));
                sprintf(aarg, "-l%s", laBase);
                WRITE_BUFFER(outCmd, aarg);
                WRITE_BUFFER(tofree, aarg);

                if (wholeArchive) {
                    WRITE_BUFFER(outCmd, "-Wl,--no-whole-archive");
                }

                free(laBaseC);
                free(laDirC);

            } else {
                WRITE_BUFFER(outAr, arg);
                WRITE_BUFFER(outCmd, arg);

            }

        }
    }

    if (insane) {
        /* just go to libtool */
        execLibtool(opt);
    }

    /* make sure an output name was specified */
    if (!outName) {
        outName = "a.out";
        WRITE_BUFFER(outCmd, "-o");
        outNamePos = outCmd.bufused;
        WRITE_BUFFER(outCmd, outName);
    }

    /* should we build a .so? */
    if (buildA)
        if (rpath) buildSo = 1;

    /* get the directory names */
    SF(outDirC, strdup, NULL, (outName));
    outDir = dirname(outDirC);
    SF(outBaseC, strdup, NULL, (outName));
    outBase = basename(outBaseC);
    ext = strrchr(outBase, '.');
    if (ext) *ext = '\0';

    /* make the .libs dir */
    SF(libsDir, malloc, NULL, (strlen(outDir) + 7));
    sprintf(libsDir, "%s/.libs", outDir);
    if (!opt->dryRun) mkdir(libsDir, 0777); /* ignore errors */

    /* building a binary is super-simple */
    if (buildBinary) {
        WRITE_BUFFER(outCmd, NULL);
        spawn(opt, outCmd.buf, retryIfFail);
        outCmd.bufused--;
    }

    /* building a .a library is mostly simple */
    if (buildA) {
        char *afile;
        SF(afile, malloc, NULL, (strlen(outDir) + strlen(outBase) + 10));
        sprintf(afile, "%s/.libs/%s.a", outDir, outBase);
        outAr.buf[2] = afile;

        /* run ar */
        WRITE_BUFFER(outAr, NULL);
        spawn(opt, outAr.buf, retryIfFail);
        outAr.bufused--;

        /* and make sure to ranlib too! */
        outAr.buf[1] = "ranlib";
        outAr.buf[3] = NULL;
        spawn(opt, outAr.buf + 1, retryIfFail);

        free(afile);
    }

    /* and building a .so file is the most complicated */
    if (buildSo) {
        char *sopath, *longpath, *linkpath;

        /* we have three filenames:
         * (1) the soname, .so.major
         * (2) the long name, .so.major.minor.revision
         * (3) the linker name, .software
         *
         * We compile with the soname as output to avoid needing -Wl,-soname.
         */
        SF(soname, malloc, NULL, (strlen(outBase) + 4*sizeof(int) + 5));
        sprintf(soname, "%s.so.%d", outBase, major);
        SF(longname, malloc, NULL, (strlen(outBase) + 3*4*sizeof(int) + 7));
        sprintf(longname, "%s.so.%d.%d.%d", outBase, major, minor, revision);
        SF(linkname, malloc, NULL, (strlen(outBase) + 4));
        sprintf(linkname, "%s.so", outBase);

        /* and get full paths for them all */
#define FULLPATH(x) do { \
        SF(x ## path, malloc, NULL, (strlen(outDir) + strlen(x ## name) + 8)); \
        sprintf(x ## path, "%s/.libs/%s", outDir, x ## name); \
} while (0)
        FULLPATH(so);
        FULLPATH(long);
        FULLPATH(link);
#undef FULLPATH

        /* unlink anything that already exists */
        unlink(sopath);
        unlink(longpath);
        unlink(linkpath);

        /* set up the link command */
        WRITE_BUFFER(outCmd, "-shared");
        outCmd.buf[outNamePos] = sopath;

        /* link */
        WRITE_BUFFER(outCmd, NULL);
        spawn(opt, outCmd.buf, retryIfFail);
        outCmd.bufused--;

        /* move it to the proper name */
        if ((tmpi = rename(sopath, longpath)) < 0) {
            perror(longpath);
            exit(1);
        }

        /* link in the shorter names */
        if ((tmpi = symlink(longname, sopath)) < 0) {
            perror(sopath);
            exit(1);
        }
        if ((tmpi = symlink(longname, linkpath)) < 0) {
            perror(linkpath);
            exit(1);
        }

        free(sopath);
        free(longpath);
        free(linkpath);
    }

    /* finally, make the .la file */
    if (buildA) {
        FILE *f = fopen(outName, "w");
        if (!f) {
            perror(outName);
            exit(1);
        }

        fprintf(f, SANE_HEADER
                   PACKAGE_HEADER);

        if (soname) {
            /* we have a .so */
            fprintf(f, "dlname='%s'\n"
                       "library_names='%s %s %s'\n",
                       soname,
                       longname, soname, linkname);
        }

        /* FIXME */

        fclose(f);
    }

    free(soname);
    free(longname);
    free(linkname);
    free(libsDir);
    free(outBaseC);
    free(outDirC);

    for (i = 0; i < tofree.bufused; i++) free(tofree.buf[i]);

    FREE_BUFFER(tofree);
    FREE_BUFFER(outAr);
    FREE_BUFFER(outCmd);
}

#endif /* _POSIX_VERSION */
