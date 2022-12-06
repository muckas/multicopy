# multicopy
A tool for copying files to multiple destinations at once with a single read

```
Usage: multicopy [OPTION]... SOURCE DESTINATION...
Copy SOURCE to multiple DESTINATION(s)
If SOURCE is a directory - recursively copies a directory (symlinks are copied, not followed)

        -h      display this help and exit
        -f      force copy even if destination files exist (overwrites files)
        -p      show progress (persent copied), if copying directory, displays number of files
        -s      show stats at the end (files opened/created, bytes read/written)
        -v      be verbose
```
