# multicopy
A tool for copying files to multiple destinations at once with a single read

```
Usage: multicopy [OPTION]... SOURCE DESTINATION...
Copy SOURCE to multiple DESTINATION(s)
If SOURCE is a directory - recursively copies a directory (symlinks are copied, not followed)

-h --help
        display this help and exit
-f --force
        force copy even if destination files exist (overwrites files)
-p --progress
        display persent copied for each file
-P --global-progress
        display total persent copied of all files in a directory
-s --stats
        show stats at the end (files opened/created, bytes read/written)
-v --verbose
        be verbose
-b --buffsize <size>
        buffer size in kilobytes, default=8
--allocate
        allocate space for files before copying
--fatal-errors
        treat every error as fatal and immediately exit
```
