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
