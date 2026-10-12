/*
 * Programming Assignment 02: extensions to Arif Butt's lsv1.0.0.
 * Keep the original main -> do_ls -> opendir/readdir/closedir flow.
 * See the initial setup commit for the unmodified instructor source.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/sysmacros.h>
#include <sys/ioctl.h>

typedef enum { VERTICAL, LONG, HORIZONTAL } DisplayMode;
static DisplayMode display_mode = VERTICAL;
static int exit_status;
static int recursive_flag;
static int directory_header;
static int printed_directory;
void do_ls(const char *dir);

static void fail(const char *path)
{
    fprintf(stderr, "ls: %s: %s\n", path, strerror(errno));
    exit_status = 1;
}

static void *allocate(size_t bytes)
{
    void *p = malloc(bytes ? bytes : 1);
    if (!p) { perror("ls: malloc"); exit(EXIT_FAILURE); }
    return p;
}

static char *copy_string(const char *s)
{
    char *p = allocate(strlen(s) + 1);
    strcpy(p, s);
    return p;
}

/* NULL base denotes an explicit file operand, already a complete path. */
static char *make_path(const char *base, const char *name)
{
    if (!base) return copy_string(name);
    size_t a = strlen(base), b = strlen(name);
    if (a > SIZE_MAX - b - 2) { errno = ENOMEM; fail(base); exit(EXIT_FAILURE); }
    char *p = allocate(a + b + 2);
    snprintf(p, a + b + 2, "%s%s%s", base, a && base[a-1] == '/' ? "" : "/", name);
    return p;
}

static void permissions(mode_t mode, char out[11])
{
    strcpy(out, "----------");
    if (S_ISDIR(mode)) out[0] = 'd';
    else if (S_ISLNK(mode)) out[0] = 'l';
    else if (S_ISCHR(mode)) out[0] = 'c';
    else if (S_ISBLK(mode)) out[0] = 'b';
    else if (S_ISFIFO(mode)) out[0] = 'p';
    else if (S_ISSOCK(mode)) out[0] = 's';
    const mode_t bits[] = {S_IRUSR,S_IWUSR,S_IXUSR,S_IRGRP,S_IWGRP,S_IXGRP,S_IROTH,S_IWOTH,S_IXOTH};
    for (size_t i = 0; i < 9; ++i)
        if (mode & bits[i]) out[i+1] = "rwx"[i % 3];
    if (mode & S_ISUID) out[3] = (mode & S_IXUSR) ? 's' : 'S';
    if (mode & S_ISGID) out[6] = (mode & S_IXGRP) ? 's' : 'S';
    if (mode & S_ISVTX) out[9] = (mode & S_IXOTH) ? 't' : 'T';
}

static char *owner_name(uid_t uid)
{
    struct passwd *pw = getpwuid(uid);
    if (pw) return copy_string(pw->pw_name);
    char buf[32];
    snprintf(buf, sizeof buf, "%ju", (uintmax_t)uid);
    return copy_string(buf);
}

static char *group_name(gid_t gid)
{
    struct group *gr = getgrgid(gid);
    if (gr) return copy_string(gr->gr_name);
    char buf[32];
    snprintf(buf, sizeof buf, "%ju", (uintmax_t)gid);
    return copy_string(buf);
}

static void timestamp(time_t modified, char out[13])
{
    /* ctime supplies "Www Mmm dd hh:mm:ss yyyy\n" in local time. */
    const char *s = ctime(&modified);
    if (!s) { strcpy(out, "????????????"); return; }
    time_t now = time(NULL);
    double age = difftime(now, modified);
    if (age < 0 || age > 15552000.0)
        snprintf(out, 13, "%.7s %.4s", s + 4, s + 20);
    else
        snprintf(out, 13, "%.12s", s + 4);
}

#define BLUE "\033[0;34m"
#define GREEN "\033[0;32m"
#define RED "\033[0;31m"
#define PINK "\033[0;35m"
#define REVERSE "\033[7m"
#define RESET "\033[0m"

static int is_archive(const char *name)
{
    const char *extension = strrchr(name, '.');
    return extension && (!strcmp(extension, ".tar") || !strcmp(extension, ".gz") || !strcmp(extension, ".zip"));
}

static void print_name(const char *name, const char *path)
{
    struct stat st;
    const char *color = "";
    if (lstat(path, &st) < 0) fail(path);
    else if (S_ISDIR(st.st_mode)) color = BLUE;
    else if (S_ISLNK(st.st_mode)) color = PINK;
    else if (!S_ISREG(st.st_mode)) color = REVERSE;
    else if (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) color = GREEN;
    else if (is_archive(name)) color = RED;
    fputs(color, stdout);
    fputs(name, stdout);
    if (*color) fputs(RESET, stdout);
}

static void print_link(const char *path)
{
    size_t capacity = 128;
    for (;;) {
        char *target = allocate(capacity + 1);
        ssize_t n = readlink(path, target, capacity);
        if (n < 0) { fail(path); free(target); return; }
        if ((size_t)n < capacity) {
            target[n] = '\0';
            printf(" -> %s", target);
            free(target);
            return;
        }
        free(target);
        if (capacity > SIZE_MAX / 2 - 1) { errno = ENOMEM; fail(path); return; }
        capacity *= 2;
    }
}

typedef struct {
    struct stat st;
    char *path, *owner, *group;
    char links[32], size[64];
    int valid;
} LongRow;

static void show_long(char **names, size_t count, const char *base)
{
    LongRow *rows = allocate(count * sizeof *rows);
    size_t link_width = 1, owner_width = 1, group_width = 1, size_width = 1;
    intmax_t blocks = 0;
    for (size_t i = 0; i < count; ++i) {
        LongRow *r = &rows[i];
        memset(r, 0, sizeof *r);
        r->path = make_path(base, names[i]);
        if (lstat(r->path, &r->st) < 0) { fail(r->path); continue; }
        r->valid = 1;
        r->owner = owner_name(r->st.st_uid);
        r->group = group_name(r->st.st_gid);
        snprintf(r->links, sizeof r->links, "%ju", (uintmax_t)r->st.st_nlink);
        if (S_ISCHR(r->st.st_mode) || S_ISBLK(r->st.st_mode))
            snprintf(r->size, sizeof r->size, "%u, %u", major(r->st.st_rdev), minor(r->st.st_rdev));
        else snprintf(r->size, sizeof r->size, "%jd", (intmax_t)r->st.st_size);
        if (strlen(r->links) > link_width) link_width = strlen(r->links);
        if (strlen(r->owner) > owner_width) owner_width = strlen(r->owner);
        if (strlen(r->group) > group_width) group_width = strlen(r->group);
        if (strlen(r->size) > size_width) size_width = strlen(r->size);
        blocks += r->st.st_blocks;
    }
    /* Linux st_blocks uses 512-byte units; ls displays 1 KiB by default. */
    if (base) printf("total %jd\n", (blocks + 1) / 2);
    for (size_t i = 0; i < count; ++i) {
        LongRow *r = &rows[i];
        if (r->valid) {
            char mode[11], date[13];
            permissions(r->st.st_mode, mode);
            timestamp(r->st.st_mtime, date);
            printf("%s %*s %-*s %-*s %*s %s ", mode,
                   (int)link_width, r->links, (int)owner_width, r->owner,
                   (int)group_width, r->group, (int)size_width, r->size, date);
            print_name(names[i], r->path);
            if (S_ISLNK(r->st.st_mode)) print_link(r->path);
            putchar('\n');
        }
        free(r->path); free(r->owner); free(r->group);
    }
    free(rows);
}

/* ANSI sequences are printed separately and never count toward padding. */
static size_t column_width(char **names, size_t count)
{
    size_t longest = 0;
    for (size_t i = 0; i < count; ++i)
        if (strlen(names[i]) > longest) longest = strlen(names[i]);
    return longest + 2;
}

static size_t column_count(size_t width, size_t count)
{
    struct winsize window;
    size_t terminal_width = 80;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &window) == 0 && window.ws_col)
        terminal_width = window.ws_col;
    size_t columns = terminal_width / width;
    if (!columns) columns = 1;
    if (columns > count) columns = count;
    return columns;
}

static void print_cell(char **names, size_t index, const char *base, size_t width, int pad)
{
    char *path = make_path(base, names[index]);
    print_name(names[index], path);
    free(path);
    if (pad)
        for (size_t n = strlen(names[index]); n < width; ++n) putchar(' ');
}

static void show_vertical(char **names, size_t count, const char *base)
{
    if (!count) return;
    size_t width = column_width(names, count);
    size_t columns = column_count(width, count);
    size_t rows = count / columns + (count % columns != 0);
    for (size_t row = 0; row < rows; ++row) {
        for (size_t index = row; index < count; index += rows)
            print_cell(names, index, base, width, index + rows < count);
        putchar('\n');
    }
}

static void show_horizontal(char **names, size_t count, const char *base)
{
    if (!count) return;
    size_t width = column_width(names, count);
    size_t columns = column_count(width, count);
    for (size_t index = 0; index < count; ++index) {
        int last = (index + 1) % columns == 0 || index + 1 == count;
        print_cell(names, index, base, width, !last);
        if (last) putchar('\n');
    }
}

static void display(char **names, size_t count, const char *base)
{
    if (display_mode == LONG) show_long(names, count, base);
    else if (display_mode == HORIZONTAL) show_horizontal(names, count, base);
    else show_vertical(names, count, base);
}

int main(int argc, char *argv[])
{
    int option;
    while ((option = getopt(argc, argv, "lxR")) != -1) {
        if (option == 'l') display_mode = LONG;
        else if (option == 'x') display_mode = HORIZONTAL;
        else if (option == 'R') recursive_flag = 1;
        else { fprintf(stderr, "Usage: %s [-lxR] [FILE|DIRECTORY ...]\n", argv[0]); return 2; }
    }
    directory_header = argc - optind > 1;
    if (optind == argc) do_ls(".");
    else for (int i = optind; i < argc; ++i) {
        do_ls(argv[i]);
    }
    if (fflush(stdout) == EOF) { perror("ls: stdout"); return 1; }
    return exit_status;
}

/* qsort passes pointers to array elements, hence pointers to char pointers. */
static int compare_names(const void *a, const void *b)
{
    const char *const *left = a;
    const char *const *right = b;
    return strcmp(*left, *right);
}

void do_ls(const char *dir)
{
    struct stat st;
    if (lstat(dir, &st) < 0) { fail(dir); return; }
    if (!S_ISDIR(st.st_mode)) {
        char *name = (char *)dir;
        display(&name, 1, NULL);
        return;
    }
    DIR *dp = opendir(dir);
    if (!dp) { fail(dir); return; }
    if (recursive_flag || directory_header) {
        if (printed_directory) putchar('\n');
        printf("%s:\n", dir);
    }
    printed_directory = 1;
    char **names = NULL;
    size_t count = 0, capacity = 0;
    struct dirent *entry;
    /* Reset errno immediately before readdir, independently of other calls. */
    for (;;) {
        errno = 0;
        entry = readdir(dp);
        if (!entry) { if (errno) fail(dir); break; }
        if (entry->d_name[0] == '.') continue;
        if (count == capacity) {
            if (capacity > SIZE_MAX / 2 / sizeof *names) {
                errno = ENOMEM; perror("ls"); exit(EXIT_FAILURE);
            }
            capacity = capacity ? capacity * 2 : 32;
            char **next = realloc(names, capacity * sizeof *names);
            if (!next) { perror("ls: realloc"); exit(EXIT_FAILURE); }
            names = next;
        }
        names[count++] = copy_string(entry->d_name);
    }
    if (closedir(dp) < 0) fail(dir);
    if (count > 1) qsort(names, count, sizeof *names, compare_names);
    display(names, count, dir);
    /* Finish this directory's listing before descending, as GNU ls does.
       A separate traversal avoids interleaving child headers with parent rows. */
    if (recursive_flag) {
        for (size_t i = 0; i < count; ++i) {
            if (!strcmp(names[i], ".") || !strcmp(names[i], "..")) continue;
            char *path = make_path(dir, names[i]);
            struct stat child;
            if (lstat(path, &child) < 0) fail(path);
            else if (S_ISDIR(child.st_mode)) do_ls(path);
            free(path);
        }
    }
    for (size_t i = 0; i < count; ++i) free(names[i]);
    free(names);
}
