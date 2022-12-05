#define PROGRAM_NAME "multicopy"
#define VERSION "2.0"

#define _XOPEN_SOURCE 500
#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <ftw.h>

struct Options {
	char *name;
	bool force;
	bool progress;
	bool verbose;
	int dest_num;
	char *dest[];
} OPTS = {PROGRAM_NAME, false, false, false, 0, NULL};

void print_usage(char *program_name) {
	fprintf(stdout, "Usage: %s [OPTION]... SOURCE DESTINATION...\n", program_name);
}

void print_help(char *program_name) {
	fprintf(stdout, "%s %s\n", PROGRAM_NAME, VERSION);
	print_usage(program_name);
	fprintf(stdout, "\
Copy SOURCE to multiple DESTINATION(s)\n\
If SOURCE is a directory - recursively copies a directory\n\
\n\
	-h	display this help and exit\n\
	-f	force copy even if destination files exist (overwrites files)\n\
	-p	show progress (persent copied)\n\
	-v	be verbose\n\
");
}

int copy_file(const char *source_path, const struct stat *source_stat, char *dest[]) {

	// Open source file
	int source_fd = open(source_path, O_RDONLY);
	if (source_fd < 0) {
		fprintf(stderr, "%s: cannot read '%s': %s\n", OPTS.name, source_path, strerror(errno));
		exit(EXIT_FAILURE);
	}

	// Get file descriptors and allocate space for new files
	int dest_fds[OPTS.dest_num];
	for (int i = 0; i < OPTS.dest_num; i++) {
		dest_fds[i] = open(dest[i], O_CREAT|O_WRONLY|O_TRUNC, source_stat->st_mode);
		if (dest_fds[i] < 0) {
			fprintf(stderr, "%s: cannot create regular file '%s': %s\n", OPTS.name, dest[i], strerror(errno));
			return -1;
		}
		int err = posix_fallocate(dest_fds[i], 0, source_stat->st_size);
		if ( err != 0) {
			fprintf(stderr, "%s: cannot allocate space for '%s': %s\n", OPTS.name, dest[i], strerror(err));
			return -1;
		}
	}

	if (OPTS.verbose) fprintf(stdout, "Copying %s to %i destinations...\n", source_path, OPTS.dest_num);
	if (posix_fadvise(source_fd, 0, 0, POSIX_FADV_SEQUENTIAL) != 0) {
		fprintf(stderr, "%s: posix_fadvice on '%s': %s\n", OPTS.name, source_path, strerror(errno));
		return -1;
	}

	// Copying files
	char buf[8192];
	ssize_t total_read = 0;
	if (OPTS.progress) fprintf(stdout, "Progress:  0%%");
	while (1) {
		ssize_t bytes_read = read(source_fd, &buf[0], sizeof(buf));
		if (bytes_read == -1) {
			fprintf(stderr, "%s: error reading %s: %s\n", OPTS.name, source_path, strerror(errno));
			return -1;
		}
		if (!bytes_read) break; // Source file ended

		for (int i = 0; i < OPTS.dest_num; i++) {
			ssize_t bytes_written = write(dest_fds[i], &buf[0], bytes_read);
			if (bytes_written == -1) {
				fprintf(stderr, "%s: error writing %s: %s\n", OPTS.name, dest[i], strerror(errno));
				return -1;
			}
			if (bytes_written != bytes_read) {
				fprintf(stderr, "%s: error: bytes_written not equal to bytes_read: file %s\n", OPTS.name, dest[i]);
				return -1;
			}
		}
		// Display progress
		if (OPTS.progress) {
			total_read += bytes_read;
			fprintf(stdout, "\b\b\b\b%3.0f%%", ((float)total_read / (float)source_stat->st_size) * 100);
		}
	}
	if (OPTS.progress) fprintf(stdout, "\n");
	return 0;
}

const char *relative_path(const char *entry_path, int level) {
	size_t path_len = strlen(entry_path);
	size_t path_pos = path_len;
	int count = 0;
	while (path_pos >= 0 && count <= level) {
		if (entry_path[path_pos] == '/') count++;
		path_pos--;
	}
	return &entry_path[path_pos + 2];
}

int handle_dir_entry(const char *entry_path, const struct stat *entry_stat, int tflag, struct FTW *ftwbuf) {
	switch (tflag) {
		case(FTW_D): // Directory
			for (int i = 0; i < OPTS.dest_num; i++) {
				// Creating destination path
				const char *rel_path = relative_path(entry_path, ftwbuf->level - 1); // (level - 1) to change root directory name
				size_t path_len = snprintf(NULL, 0, "%s/%s", OPTS.dest[i], rel_path);
				char path[path_len + 1];
				if (snprintf(path, path_len + 1, "%s/%s", OPTS.dest[i], rel_path) != path_len) {
					fprintf(stderr, "%s: snprintf result not equal %i for '%s'\n", OPTS.name, (int)path_len, path);
					return -1;
				}
				if (path[path_len - 1] == '/') path[path_len - 1] = '\0'; // remove trailing slash
				// Creating directory
				if (mkdir(path, entry_stat->st_mode) == -1) {
					if (errno == EEXIST) { // path exists, checking if it's a directory
						struct stat sb;
						if (lstat(path, &sb) < 0) {
							fprintf(stderr, "%s: cannot stat '%s': %s\n", OPTS.name, path, strerror(errno));
							return -1;
						}
						if (!S_ISDIR(sb.st_mode)) { // it's not a directory
							fprintf(stderr, "%s: cannot mkdir, path exists, but it is not a directory '%s'\n",
											OPTS.name, path);
							return -1;
						}
					} else {
						fprintf(stderr, "%s: failed creating directory '%s': %s\n", OPTS.name, path, strerror(errno));
						return -1;
					}
				}
			}
			break;
		case(FTW_DNR): // Unreadable directory
			fprintf(stderr, "%s: cannot read directory '%s'\n", OPTS.name, entry_path);
			break;
		case(FTW_F): // File
			{ // scope to bypass error: switch jumps into scope of identifier with variably modified type
				// Creating destinations
				char *dest[OPTS.dest_num];
				for (int i = 0; i < OPTS.dest_num; i++) {
					const char *rel_path = relative_path(entry_path, ftwbuf->level - 1); // (level - 1) to change root directory name
					size_t path_len = snprintf(NULL, 0, "%s/%s", OPTS.dest[i], rel_path);
					dest[i] = malloc(path_len + 1);
					if (snprintf(dest[i], path_len + 1, "%s/%s", OPTS.dest[i], rel_path) != path_len) {
						fprintf(stderr, "%s: snprintf result not equal %i for '%s'\n", OPTS.name, (int)path_len, dest[i]);
						return -1;
					}
					if (dest[i][path_len - 1] == '/') dest[i][path_len - 1] = '\0'; // remove trailing slash
				}
				int copy_result = copy_file(entry_path, entry_stat, dest);
				for (int i = 0; i < OPTS.dest_num; i++) { 
					free(dest[i]); // free allocated memory
				}
				if ( copy_result != 0) {
					return -1;
				}
			}
			break;
		case(FTW_SL): // Symbolic link
			/* printf("symbolic link\n"); */
			break;
		case(FTW_NS): // Stat failed, lack of permission
			fprintf(stderr, "%s: cannot call stat on '%s'\n", OPTS.name, entry_path);
			break;
	}
	return 0;
}

int main(int argc, char *argv[]) {
	OPTS.name = argv[0];

	// Parse command line arguments
	int opt;
	while ((opt = getopt(argc, argv, ":hfpv")) != -1) {
		switch(opt) {
			case 'h':
				print_help(OPTS.name);
				exit(EXIT_SUCCESS);
				break;
			case 'f':
				OPTS.force = true;
				break;
			case 'p':
				OPTS.progress = true;
				break;
			case 'v':
				OPTS.verbose = true;
				break;
			case '?':
				fprintf(stderr, "%s: invalid option -- '%c'\n", OPTS.name, optopt);
				fprintf(stdout, "Try '%s -h' for more information'\n", OPTS.name);
				exit(EXIT_FAILURE);
				break;
		}
	}
	// Count extra arguments
	OPTS.dest_num = argc - optind - 1;
	if (OPTS.dest_num < 1) {
		fprintf(stderr, "%s: not enough arguments\n", OPTS.name);
		print_usage(OPTS.name);
		exit(EXIT_SUCCESS);
	}

	char *source_path = argv[optind];
	optind++; // optind now on first DESTINATION argument
	for (int i = 0; i < argc-optind; i++) {
		OPTS.dest[i] = argv[optind+i];
	}

	if (!OPTS.force) {
		// Check if overwriting
		int overwriting = 0;
		for (int i = 0; i < OPTS.dest_num; i++) {
			struct stat buff;
			if (stat(OPTS.dest[i], &buff) == 0) {
				fprintf(stderr, "%s: destination already exists '%s'\n", OPTS.name, OPTS.dest[i]);
				overwriting = 1;
			}
		}
		if (overwriting == 1) {
			fprintf(stdout, "%s: aborting copy, use '-f' to overwrite existing files\n", OPTS.name);
			exit(EXIT_FAILURE);
		}
	}

	// Stat SOURCE
	struct stat statbuff;
	if (lstat(source_path, &statbuff) < 0) {
		fprintf(stderr, "%s: cannot stat '%s': %s\n", OPTS.name, source_path, strerror(errno));
		exit(EXIT_FAILURE);
	}
	if (S_ISREG(statbuff.st_mode)) { // SOURCE is regular file
		char *dest[OPTS.dest_num]; // Copy variable sized global array to static size local array
		for (int i = 0; i < OPTS.dest_num; i++) {
			dest[i] = OPTS.dest[i];
		}
		int copy_result = copy_file(source_path, &statbuff, dest);
		if (copy_result != 0) exit(EXIT_FAILURE);

	} else if (S_ISDIR(statbuff.st_mode)) { // SOURCE is directory
		int nftw_result = nftw(source_path, handle_dir_entry, 10, FTW_PHYS); // FTW_PHYS (no symlincs)
		if (nftw_result == -1) {
			fprintf(stderr, "%s: nftw error: %s\n", OPTS.name, strerror(errno));
			exit(EXIT_FAILURE);
		}

	} else {
		fprintf(stderr, "%s: '%s' is not a regular file or directory\n", OPTS.name, source_path);
		exit(EXIT_FAILURE);
	}

	if (OPTS.verbose) {
		fprintf(stdout, "Created %i destinations:\n", OPTS.dest_num);
		for (int i = 0; i < OPTS.dest_num; i++) {
			fprintf(stdout, "\t%s\n", argv[i+optind]);
		}
	}
	exit(EXIT_SUCCESS);
}
