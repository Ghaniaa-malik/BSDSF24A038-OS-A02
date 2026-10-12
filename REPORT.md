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
