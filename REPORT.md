# REPORT - BSDSF24A038 - Ghania Malik

## Feature 2: Complete long listing (v1.1.0)

### 1. stat() versus lstat()

`stat(path, &st)` follows a symbolic link and reports the target's metadata.
`lstat(path, &st)` reports the link itself. The latter is appropriate for directory
entries in ls: it preserves the `l` file-type marker, link size and link permissions,
allows `readlink()` to show `name -> target`, and works for dangling links. During
recursion it also prevents a symlink to an ancestor from being followed forever.
The program uses lstat for explicit operands as well as directory entries.

### 2. Extracting type and permission bits

`st_mode` packs independent bit fields. `(mode & S_IFMT) == S_IFDIR` isolates and
tests the file type; `S_ISDIR(mode)` is the clearer macro form. Testing
`mode & S_IRUSR` checks the owner's read permission without disturbing other bits.
The same process with S_IWUSR and S_IXUSR, then group and other bits, produces the
nine rwx characters. File type is encoded, not an independent flag, so testing just
`mode & S_IFDIR` is not a correct general type test. S_ISUID, S_ISGID and S_ISVTX
replace the relevant execute character with s/s/t when execute is enabled, or
S/S/T when it is absent. `permissions()` implements all of these cases.

Long listing first gathers metadata and finds maximum field widths, then prints
aligned rows. getpwuid/getgrgid supply names; unresolved IDs fall back to decimal.
ctime supplies the modification date, with the year shown for old/future files.
Linux st_blocks counts 512-byte units, so total is converted to 1 KiB units.

## Feature 3: Down-then-across columns (v1.2.0)

### 3. Vertical indexing and why a simple loop is insufficient

First collect all names to know the count n and longest name. Use cell width
`w = longest + 2`, `columns = max(1, terminal_width / w)` (capped at n), and
`rows = ceil(n / columns)`. At output row r, indices are r, r + rows,
r + 2*rows, and so on, stopping at n. For seven names and three columns the
rows are `a d g`, `b e`, `c f`. A direct loop printing consecutive indices on
each row would instead produce horizontal order. Empty directories return before
any division; a filename wider than the terminal uses a single column.

### 4. ioctl and the fallback

`ioctl(STDOUT_FILENO, TIOCGWINSZ, &window)` asks the terminal driver for its current
size. ws_col provides the usable column count, so resizing changes the layout on
the next invocation. For redirected output or a failed/zero-width query, the code
uses 80. A fixed width alone could wrap badly in a narrow terminal or waste space
in a wide one. Tests attach stdout to pseudo-terminals of widths 1, 9, 18 and 80.
The assignment asks for fixed-width cells; GNU ls can pack variable-width columns
more tightly, so identical spacing is not promised for every directory.

## Feature 4: Horizontal columns (v1.3.0)

### 5. Vertical versus horizontal complexity

Both modes scan names to find the maximum cell width and use the terminal width
to compute the number of columns. Vertical layout additionally needs the final
file count and row count before its first output, because each next column is a
stride of rows away in the array. Horizontal layout walks consecutive indices,
printing a newline whenever `(index + 1) % columns == 0`, or at the final item.
Both printing algorithms are O(n); vertical order needs more layout pre-calculation.

### 6. Selecting a display mode

An enum, DisplayMode, holds VERTICAL, LONG or HORIZONTAL. VERTICAL is the default.
getopt processes each option in order: -l sets LONG and -x sets HORIZONTAL, so the
last display option wins, including combined flags (-xl means long; -lx means
horizontal). display() dispatches once to show_long(), show_horizontal() or
show_vertical(). A later independent recursive_flag can combine with any layout.

## Feature 5: Alphabetical sorting (v1.4.0)

### 7. Why collect entries before sorting?

readdir yields filesystem order, not lexical order. A name read later could precede
every name already printed, and stdout cannot retract output. We store copied
names in a dynamically grown char-pointer array, qsort it, then call the same
display functions. Names must be copied because readdir's storage may be reused.
This requires O(n + total filename bytes) memory, plus long-listing metadata when
requested. Huge directories can exhaust memory and delay the first output. External
sorting could trade disk I/O for lower memory use. Growth is checked for allocation
failure; all names and the array are freed after display and child traversal.

### 8. qsort comparator

The signature is `int compare_names(const void *a, const void *b)`. qsort is generic:
it knows the element count and size, but not the element type, so it passes untyped
pointers to two elements. Here an element is a char pointer; cast each argument to
`const char *const *`, dereference once, and return strcmp(*left, *right). Negative,
zero and positive results mean before, equal and after. Do not subtract pointers
or assume strcmp returns exactly -1 or +1. qsort is called before any display mode,
so all modes share the same order. strcmp gives case-sensitive byte order, matching
GNU ls under LC_ALL=C, rather than locale-aware dictionary collation.

## Feature 6: Colors (v1.5.0)

### 9. ANSI color sequences

A terminal interprets ESC followed by `[` and SGR parameters ending in `m` as a
style change rather than visible letters. For example,
`printf("\033[0;32mgreen text\033[0m");` selects green, writes the text, then resets
the style. Omitting reset would color later output too. The program uses blue
(0;34) for directories, green (0;32) for executable regular files, red (0;31) for
.tar/.gz/.zip archives, pink/magenta (0;35) for links, and reverse video (7) for
special files. Type takes priority over extension; executable archives are green.
Suffix comparisons avoid mistaking archive.zip.txt for a zip. Colors are emitted
in all modes, including redirected output, as requested by the assignment. Escape
sequences do not contribute to filename length or padding.

### 10. Execute permission bits

S_IXUSR, S_IXGRP and S_IXOTH represent execute permission for owner, group and
others. `(mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0` detects at least one execute
bit. The OR constructs a mask; AND retains only those bits. This describes the
file's mode, not whether the current user can execute it: ACLs, mount options and
credentials can change effective access. Check regular-file type first, because
execute bits on a directory represent search/traversal, and directories stay blue.

## Feature 7: Recursive listing (v1.6.0)

### 11. The base case

A base case is a condition that ends a recursive branch without another recursive
call. Here a directory containing no eligible child directories makes no further
calls, then frees its names and returns. Empty directories therefore terminate
immediately after display. Non-directory entries are never recursed into. Hidden
entries (including . and ..) are skipped, and the recursive loop also explicitly
excludes `.` and `..`. lstat plus S_ISDIR prevents following symbolic links, including
links to ancestors. An unreadable directory reports an error and returns, while
other readable directories continue. Memory and call-stack use grow with directory
depth; the implementation is intended for ordinary static directory trees.

### 12. Full paths and the working directory

Opening a directory does not change the process working directory. Inside
`do_ls("parent_dir")`, calling `do_ls("subdir")` would resolve subdir against the
original working directory, potentially listing an unrelated directory or failing.
make_path allocates enough space for the parent, optional slash, child and NUL.
Both lstat and the recursive call receive that same full path. No chdir is needed,
so sibling traversal and multiple initial operands remain correct.

The parent listing is completed before recursive descent, then children are
visited in sorted depth-first order with a header and blank separator. The PDF
mentions descending inside the display loop, but doing so interleaves child
headers with unfinished parent columns. A separate traversal of the same sorted
array satisfies the stated requirement to match GNU ls -R's output structure.

## Final validation and design boundaries

Every feature branch was built on Ubuntu 24.04 with GCC, using -Wall, -Wextra,
-Wpedantic and -Werror. The version-aware Python integration suite compares long
format to GNU ls, checks special permission bits and devices, uses real symlinks,
a FIFO and a Unix socket, and sets pseudo-terminal widths to verify column order.
It checks all display modes, sorting, color resets, recursion, empty directories,
long names, missing paths and permission errors. Tests for later features are
skipped on earlier versions. Release binaries come from passing Ubuntu CI runs,
not Windows executables. Each release links to its build/test run.

The final workflow also rebuilds with AddressSanitizer and UndefinedBehaviorSanitizer
and runs the full suite again. `make test` reproduces functional tests on Linux.
The compiled release asset is the normal optimized build, not the sanitizer build.

This implementation intentionally follows the assignment's subset: no -a option,
locale-aware sorting, ACL marker, filename quoting or Unicode display-width logic.
Lengths are byte counts. It does not aim to duplicate every GNU ls option or its
variable-width column packing. Memory allocation failure terminates with an error;
ordinary path errors allow subsequent operands to continue. Directory symlink
operands themselves are shown, not followed. A concurrently changing filesystem
may produce errors; bind-mount cycles are outside this educational scope.

## Viva preparation

Trace a seven-name directory through main, do_ls, compare_names, display and each
printing mode. Explain why names are copied, why lstat is needed, how enum option
state differs from the recursive flag, why rows determine vertical strides, how
ANSI codes differ from visible width, and when each allocation is freed. Then
trace parent/child path construction and an ancestor symlink. The student must
watch the required instructor video and understand the submitted code; neither
video viewing nor viva readiness is claimed by automated validation.
