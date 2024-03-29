#define PROGRAM_NAME "multicopy"
#define VERSION "3.2"

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
#include <limits.h>
#include <getopt.h>

struct Stats {
	int copied_files;
	int total_files;
	int files_read;
	int files_created;
	int dirs_read;
	int dirs_created;
	int symlinks_read;
	int symlinks_created;
	int errors;
	size_t bytes_read;
	size_t bytes_written;
	size_t total_size;
	char * str_total_size;
} STATS; //Global struct

struct Options {
	char *name;
	bool force;
	bool progress;
	bool global_progress;
	bool stats;
	bool verbose;
	bool allocate;
	bool fatal_errors;
	int bufsize_kb;
	int dest_num;
	char *dest[];
} OPTS; // Global struct

char *human_readable(size_t bytes);
void print_usage(char *program_name);
void print_help(char *program_name);
void print_version();
void print_stats();

int copy_file(const char *source_path, const struct stat *source_stat, char *dest[]);
int count_dir_files(const char *entry_path, const struct stat *entry_stat, int tflag, struct FTW *ftwbuf);
const char *relative_path(const char *entry_path, int level);
int handle_dir_entry(const char *entry_path, const struct stat *entry_stat, int tflag, struct FTW *ftwbuf);


int main(int argc, char *argv[]) {
	OPTS.name = argv[0];
	OPTS.force = false;
	OPTS.progress = false;
	OPTS.global_progress = false;
	OPTS.stats = false;
	OPTS.verbose = false;
	OPTS.allocate = false;
	OPTS.fatal_errors = false;
	OPTS.bufsize_kb = 8;
	OPTS.dest_num = 0;

	STATS.copied_files = 0;
	STATS.total_files = 0;
	STATS.files_read = 0;
	STATS.files_created = 0;
	STATS.dirs_read = 0;
	STATS.dirs_created = 0;
	STATS.symlinks_read = 0;
	STATS.symlinks_created = 0;
	STATS.errors = 0;
	STATS.total_size = 0;
	STATS.bytes_read = 0;
	STATS.bytes_written = 0;
	STATS.str_total_size = NULL;

	enum longopt {
		allocate,
		fatal_errors,
		help,
		version,
	};
	static struct option long_options[] = {
		{"force", no_argument, 0, 'f'},
		{"progress", no_argument, 0, 'p'},
		{"global-progress", no_argument, 0, 'P'},
		{"stats", no_argument, 0, 's'},
		{"verbose", no_argument, 0, 'v'},
		{"buffsize", required_argument, 0, 'b'},
		{"allocate", no_argument, 0, allocate},
		{"fatal-errors", no_argument, 0, fatal_errors},
		{"help", no_argument, 0, help},
		{"version", no_argument, 0, version},
	};
	// Parse command line arguments
	int opt;
	int option_index = 0;
	while ((opt = getopt_long(argc, argv, ":fpPsvb:", long_options, NULL)) != -1) {
		switch(opt) {
			case 'f':
				OPTS.force = true;
				break;
			case 'p':
				OPTS.progress = true;
				break;
			case 'P':
				OPTS.global_progress = true;
				break;
			case 's':
				OPTS.stats = true;
				break;
			case 'v':
				OPTS.verbose = true;
				break;
			case 'b':
				OPTS.bufsize_kb = atoi(optarg);
				if (OPTS.bufsize_kb <= 0) {
					fprintf(stderr, "%s: invalid buffer size -- '%s'\n", OPTS.name, optarg);
					fprintf(stdout, "Try '%s --help' for more information'\n", OPTS.name);
					exit(EXIT_FAILURE);
				}
				break;
			case allocate:
				OPTS.allocate = true;
				break;
			case fatal_errors:
				OPTS.fatal_errors = true;
				break;
			case help:
				print_help(OPTS.name);
				exit(EXIT_SUCCESS);
				break;
			case version:
				print_version();
				exit(EXIT_SUCCESS);
				break;
			case ':':
				fprintf(stderr, "%s: option '%c' requires an argument\n", OPTS.name, optopt);
				fprintf(stdout, "Try '%s --help' for more information'\n", OPTS.name);
				exit(EXIT_FAILURE);
				break;
			case '?':
				fprintf(stderr, "%s: invalid option -- '%c'\n", OPTS.name, optopt);
				fprintf(stdout, "Try '%s --help' for more information'\n", OPTS.name);
				exit(EXIT_FAILURE);
				break;
		}
	}
	// Count extra arguments
	OPTS.dest_num = argc - optind - 1;
	if (OPTS.dest_num < 1) {
		fprintf(stderr, "%s: not enough arguments\n", OPTS.name);
		print_usage(OPTS.name);
		exit(EXIT_FAILURE);
	}

	size_t source_len = strlen(argv[optind]);
	char *source_path = argv[optind];
	if (source_path[source_len - 1] == '/') source_path[source_len - 1] = '\0'; // remove trailing slash

	optind++; // optind now on first DESTINATION argument
	for (int i = 0; i < OPTS.dest_num; i++) {
		size_t dest_len = strlen(argv[optind + i]);
		OPTS.dest[i] = argv[optind + i];
		if (OPTS.dest[i][dest_len - 1] == '/') OPTS.dest[i][dest_len - 1] = '\0'; // remove trailing slash
		if (strcmp(OPTS.dest[i], source_path) == 0) { // DEST is the same as SOURCE
			fprintf(stderr, "%s: source and destination cannot be the same: '%s'\n", OPTS.name, OPTS.dest[i]);
			exit(EXIT_FAILURE);
		}
	}

	char *allocated_memory[OPTS.dest_num]; // Keeps track of dynamically allocated memory
	for (int i = 0; i < OPTS.dest_num; i++) {
		allocated_memory[i] = NULL;
	}
	// Same name copy if DEST is a directory
	for (int i = 0; i < OPTS.dest_num; i++) {
		struct stat buff;
		if (stat(OPTS.dest[i], &buff) == 0) {
			if (S_ISDIR(buff.st_mode)) { // DEST is directory, appending SOURCE name
				const char *source_name = relative_path(source_path, 0);
				char * dest = OPTS.dest[i];
				size_t path_len = snprintf(NULL, 0, "%s/%s", dest, source_name);
				OPTS.dest[i] = malloc( (path_len + 1)* sizeof(char) );
				allocated_memory[i] = OPTS.dest[i]; // this memory is freed at the end
				if (snprintf(OPTS.dest[i], path_len + 1, "%s/%s", dest, source_name) != path_len) {
					fprintf(stderr, "%s: snprintf result not equal %lu for '%s'\n", OPTS.name, path_len, dest);
					return -1;
				}
			}
		}
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
	if (lstat(source_path, &statbuff) == -1) {
		fprintf(stderr, "%s: cannot stat '%s': %s\n", OPTS.name, source_path, strerror(errno));
		exit(EXIT_FAILURE);
	}
	if (S_ISREG(statbuff.st_mode)) { // SOURCE is regular file
		char *dest[OPTS.dest_num]; // Copy variable sized global array to static size local array
		for (int i = 0; i < OPTS.dest_num; i++) {
			dest[i] = OPTS.dest[i];
		}
		STATS.total_files = 1;
		STATS.copied_files = 1;
		STATS.str_total_size = human_readable(statbuff.st_size); // malloc
		int copy_result = copy_file(source_path, &statbuff, dest);
		if (copy_result != 0) exit(EXIT_FAILURE);

	} else if (S_ISDIR(statbuff.st_mode)) { // SOURCE is directory
		if (OPTS.global_progress) {
			// Counting files
			int nftw_result = nftw(source_path, count_dir_files, 10, FTW_PHYS); // FTW_PHYS (no symlincs)
			if (nftw_result == -1) {
				fprintf(stderr, "%s: nftw error on counting files: %s\n", OPTS.name, strerror(errno));
				exit(EXIT_FAILURE);
			}
			STATS.str_total_size = human_readable(STATS.total_size); // malloc
		}
		//Copying files
		int nftw_result = nftw(source_path, handle_dir_entry, 10, FTW_PHYS); // FTW_PHYS (no symlincs)
		if (nftw_result == -1) {
			fprintf(stderr, "%s: nftw error: %s\n", OPTS.name, strerror(errno));
			exit(EXIT_FAILURE);
		}

	} else {
		fprintf(stderr, "%s: '%s' is not a regular file or directory\n", OPTS.name, source_path);
		exit(EXIT_FAILURE);
	}

	if (OPTS.stats) print_stats();
	if (OPTS.verbose) {
		fprintf(stdout, "Copied to %i destinations:\n", OPTS.dest_num);
		for (int i = 0; i < OPTS.dest_num; i++) {
			fprintf(stdout, "\t%s\n", OPTS.dest[i]);
		}
	}

	// Free allocated destinations
	for (int i = 0; i < OPTS.dest_num; i++) {
		free(allocated_memory[i]); 
	}
	free(STATS.str_total_size);

	exit(EXIT_SUCCESS);
}


char *human_readable(size_t bytes) { // allocates memory
	char *tsize;
	double size;
	size = bytes; tsize = " bytes";
	if (size > 1024) { size /= 1024; tsize = "Kib"; }
	if (size > 1024) { size /= 1024; tsize = "Mib"; }
	if (size > 1024) { size /= 1024; tsize = "Gib"; }
	size_t str_size = snprintf(NULL, 0, "%f%s", size, tsize);
	char *str = malloc( (str_size + 1) * sizeof(char) );
	snprintf(str, str_size + 1, "%.2f%s", size, tsize);
	return str;
}

void print_usage(char *program_name) {
	fprintf(stdout, "Usage: %s [OPTION]... SOURCE DESTINATION...\n", program_name);
}

void print_help(char *program_name) {
	print_usage(program_name);
	fprintf(stdout, "\
Copy SOURCE to one or more DESTINATION(s) simultaneously\n\
If SOURCE is a directory - recursively copies a directory (symlinks are copied, not followed)\n\
If DESTINATION is a directory, SOURCE is copied into that directory\n\
Options:\n\
-f --force\n\
\tforce copy even if destination files exist (overwrites files)\n\
-p --progress\n\
\tdisplay persent copied for each file\n\
-P --global-progress\n\
\tdisplay total persent copied of all files in a directory\n\
-s --stats\n\
\tshow stats at the end (files opened/created, bytes read/written)\n\
-v --verbose\n\
\tbe verbose\n\
-b --buffsize <size>\n\
\tbuffer size in kilobytes, default=8\n\
--allocate\n\
\tallocate space for files before copying\n\
--fatal-errors\n\
\ttreat every error as fatal and immediately exit\n\
--help\n\
\tdisplay this help and exit\n\
--version\n\
\tdisplay version information and exit\n\
");
}

void print_version() {
	fprintf(stdout, "%s %s\n", PROGRAM_NAME, VERSION);
	fprintf(stdout, "License GPLv3+: GNU GPL version 3 or later <https://gnu.org/licenses/gpl.html>.\n\
This is free software: you are free to change and redistribute it.\n\
There is NO WARRANTY, to the extent permitted by law.\n"
	);
}

void print_stats() {
	fprintf(stdout, "Opened %i dirs, %i files, %i symlinks\n",
			STATS.dirs_read, STATS.files_read, STATS.symlinks_read);
	fprintf(stdout, "Created %i dirs, %i files, %i symlinks\n",
			STATS.dirs_created, STATS.files_created, STATS.symlinks_created);
	char *read = human_readable(STATS.bytes_read);
	char *written = human_readable(STATS.bytes_written);
	fprintf(stdout, "%s read, %s written, errors: %i\n", read, written, STATS.errors);
	free(read);
	free(written);
}

int copy_file(const char *source_path, const struct stat *source_stat, char *dest[]) {

	// Open source file
	int source_fd = open(source_path, O_RDONLY);
	if (source_fd < 0) {
		fprintf(stderr, "%s: cannot read '%s': %s\n", OPTS.name, source_path, strerror(errno));
		STATS.errors++;
		if (OPTS.fatal_errors) {return -1;} else {return 0;}
	}
	if (OPTS.stats) STATS.files_read++;

	// Get file descriptors and allocate space for new files
	int dest_fds[OPTS.dest_num];
	for (int i = 0; i < OPTS.dest_num; i++) {
		dest_fds[i] = open(dest[i], O_CREAT|O_WRONLY|O_TRUNC, source_stat->st_mode);
		if (dest_fds[i] < 0) {
			fprintf(stderr, "%s: cannot create regular file '%s': %s\n", OPTS.name, dest[i], strerror(errno));
			STATS.errors++;
			if (OPTS.fatal_errors) {return -1;} else {return 0;}
		}
		if (OPTS.stats) STATS.files_created++;
		if (OPTS.allocate) {
			if (OPTS.verbose) fprintf(stdout, "Allocating %lu bytes for '%s'\n", source_stat->st_size, dest[i]);
			int err = posix_fallocate(dest_fds[i], 0, source_stat->st_size);
			if ( err != 0) {
				fprintf(stderr, "%s: cannot allocate space for '%s': %s\n", OPTS.name, dest[i], strerror(err));
				STATS.errors++;
				if (OPTS.fatal_errors) {return -1;} else {return 0;}
			}
		}
	}

	if (OPTS.verbose) fprintf(stdout, "Copying %s to %i destinations...\n", source_path, OPTS.dest_num);
	if (posix_fadvise(source_fd, 0, 0, POSIX_FADV_SEQUENTIAL) != 0) {
		fprintf(stderr, "%s: posix_fadvice on '%s': %s\n", OPTS.name, source_path, strerror(errno));
		STATS.errors++;
		if (OPTS.fatal_errors) {return -1;} else {return 0;}
	}

	// Copying files
	char buf[OPTS.bufsize_kb * 1024];
	ssize_t total_read = 0;
	while (1) {
		ssize_t bytes_read = read(source_fd, &buf[0], sizeof(buf));
		if (bytes_read == -1) {
			fprintf(stderr, "%s: error reading %s: %s\n", OPTS.name, source_path, strerror(errno));
			STATS.errors++;
			if (OPTS.fatal_errors) {return -1;} else {return 0;}
		}
		if (!bytes_read) break; // Source file ended
		STATS.bytes_read += bytes_read;

		for (int i = 0; i < OPTS.dest_num; i++) {
			ssize_t bytes_written = write(dest_fds[i], &buf[0], bytes_read);
			if (bytes_written == -1) {
				fprintf(stderr, "%s: error writing %s: %s\n", OPTS.name, dest[i], strerror(errno));
				STATS.errors++;
				if (OPTS.fatal_errors) {return -1;} else {return 0;}
			}
			STATS.bytes_written += bytes_written;
			if (bytes_written != bytes_read) {
				fprintf(stderr, "%s: error: bytes_written not equal to bytes_read: '%s'\n", OPTS.name, dest[i]);
				STATS.errors++;
				if (OPTS.fatal_errors) {return -1;} else {return 0;}
			}
		}
		// Display progress
		if (OPTS.progress || OPTS.global_progress) {
			total_read += bytes_read;
			if (OPTS.global_progress) {
				char *str_read = human_readable(STATS.bytes_read);
				double total_percent_copied = ((float)STATS.bytes_read / (float)STATS.total_size) * 100;
				fprintf(stdout, "%3.0f%% (%s/%s), files (%i/%i)",
								total_percent_copied, str_read, STATS.str_total_size, STATS.copied_files, STATS.total_files);
				free(str_read);
			}
			if (OPTS.progress) {
				double persent_copied = ((float)total_read / (float)source_stat->st_size) * 100;
				fprintf(stdout, " File progress:%3.0f%%     \b\b\b\b\b", persent_copied);
			}
			fprintf(stdout, "\r");
			fflush(stdout);
		}
	}

	// Close file descriptors
	if (close(source_fd) == -1) {
		fprintf(stderr, "%s: error closing file descriptor %i '%s': %s\n", OPTS.name, source_fd, source_path, strerror(errno));
		STATS.errors++;
	}
	for (int i = 0; i < OPTS.dest_num; i++) {
		if (close(dest_fds[i]) == -1) {
			fprintf(stderr, "%s: error closing file descriptor %i '%s': %s\n", OPTS.name, source_fd, dest[i], strerror(errno));
			STATS.errors++;
		}
	}
	if (OPTS.progress || OPTS.global_progress) {
		fprintf(stdout, "\r");
		int i;
		while (i++ < 80) {
			fputc(' ', stdout);
		}
		fprintf(stdout, "\r");
	}
	return 0;
}

int count_dir_files(const char *entry_path, const struct stat *entry_stat, int tflag, struct FTW *ftwbuf) {
	if (tflag == FTW_F) {
		STATS.total_files++;
		STATS.total_size += entry_stat->st_size;
		char *str_size = human_readable(STATS.total_size);
		fprintf(stdout, "Counting files: %i, total size: %s\r", STATS.total_files, str_size);
		fflush(stdout);
		free(str_size);
	}
	return 0;
}

const char *relative_path(const char *entry_path, int level) {
	size_t path_len = strlen(entry_path);
	size_t path_pos = path_len;
	int count = 0;
	while (path_pos > 0 && count <= level) {
		path_pos--;
		if (entry_path[path_pos] == '/') count++;
	}
	if (entry_path[path_pos] == '/') path_pos++; // remove leading slash
	return &entry_path[path_pos];
}

int handle_dir_entry(const char *entry_path, const struct stat *entry_stat, int tflag, struct FTW *ftwbuf) {
	switch (tflag) {
		case(FTW_D): // Directory
		case(FTW_SL): // Symbolic link
			if (OPTS.stats) {
				switch(tflag) {
					case(FTW_D):
						STATS.dirs_read++;
						break;
					case(FTW_SL):
						STATS.symlinks_read++;
						break;
				}
			}
			for (int i = 0; i < OPTS.dest_num; i++) {
				// Creating destination path
				const char *rel_path = relative_path(entry_path, ftwbuf->level - 1); // (level - 1) to change root directory name
				size_t path_len = snprintf(NULL, 0, "%s/%s", OPTS.dest[i], rel_path);
				char path[path_len + 1];
				if (snprintf(path, path_len + 1, "%s/%s", OPTS.dest[i], rel_path) != path_len) {
					fprintf(stderr, "%s: snprintf result not equal %i for '%s'\n", OPTS.name, (int)path_len, path);
					STATS.errors++;
					if (OPTS.fatal_errors) {return -1;} else {return 0;}
				}
				if (path[path_len - 1] == '/') path[path_len - 1] = '\0'; // remove trailing slash

				if (tflag == FTW_D) { // entry_path is a directory
					// Creating directory
					if (mkdir(path, entry_stat->st_mode) == -1) {
						if (errno == EEXIST) { // path exists, checking if it's a directory
							struct stat sb;
							if (lstat(path, &sb) == -1) {
								fprintf(stderr, "%s: cannot stat '%s': %s\n", OPTS.name, path, strerror(errno));
								STATS.errors++;
								if (OPTS.fatal_errors) {return -1;} else {return 0;}
							}
							if (!S_ISDIR(sb.st_mode)) { // it's not a directory, removing it
								if (remove(path) == 0) { // file at 'path' removed
									if (mkdir(path, entry_stat->st_mode) == -1) {
										fprintf(stderr, "%s: cannot mkdir '%s':%s\n",
														OPTS.name, path, strerror(errno));
										STATS.errors++;
										if (OPTS.fatal_errors) {return -1;} else {return 0;}
									} else { // directory created
										if (OPTS.stats) STATS.dirs_created++;
									}
								} else { //failed to remove file at 'path'
									fprintf(stderr, "%s: cannot mkdir, failed overwriting '%s':%s\n",
													OPTS.name, path, strerror(errno));
									STATS.errors++;
									if (OPTS.fatal_errors) {return -1;} else {return 0;}
								}
							}
						} else {
							fprintf(stderr, "%s: failed creating directory '%s': %s\n", OPTS.name, path, strerror(errno));
							STATS.errors++;
							if (OPTS.fatal_errors) {return -1;} else {return 0;}
						}
					} else { // directory created
						if (OPTS.stats) STATS.dirs_created++;
					}

				} else { // entry_path is a symbolic link
					ssize_t bufsize;
					if (entry_stat->st_size == 0) {
						bufsize = PATH_MAX;
					} else {
						bufsize = entry_stat->st_size + 1;
					}
					char *target = malloc(bufsize);
					if (readlink(entry_path, target, bufsize) == -1) {
						fprintf(stderr, "%s: failed reading symbolic link '%s': %s\n", OPTS.name, entry_path, strerror(errno));
						free(target);
						STATS.errors++;
						if (OPTS.fatal_errors) {return -1;} else {return 0;}
					}
					target[bufsize - 1] = '\0'; // add nul terminator to the end of the string
					if (remove(path) == -1) {
						if (errno != ENOENT) { // ignore errors if path does not exist
							fprintf(stderr, "%s: failed removing symbolic link '%s': %s\n", OPTS.name, path, strerror(errno));
							free(target);
							STATS.errors++;
							if (OPTS.fatal_errors) {return -1;} else {return 0;}
						}
					}
					if (symlink(target, path) == -1) {
						fprintf(stderr, "%s: failed creating symbolic link '%s': %s\n", OPTS.name, path, strerror(errno));
						free(target);
						STATS.errors++;
						if (OPTS.fatal_errors) {return -1;} else {return 0;}
					} else { // symlink created
						if (OPTS.stats) STATS.symlinks_created++;
					}
					free(target);
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
					dest[i] = malloc( (path_len + 1) * sizeof(char) );
					if (snprintf(dest[i], path_len + 1, "%s/%s", OPTS.dest[i], rel_path) != path_len) {
						fprintf(stderr, "%s: snprintf result not equal %i for '%s'\n", OPTS.name, (int)path_len, dest[i]);
						STATS.errors++;
						if (OPTS.fatal_errors) {return -1;} else {return 0;}
					}
					if (dest[i][path_len - 1] == '/') dest[i][path_len - 1] = '\0'; // remove trailing slash
				}
				STATS.copied_files++;
				int copy_result = copy_file(entry_path, entry_stat, dest);
				for (int i = 0; i < OPTS.dest_num; i++) { 
					free(dest[i]); // free allocated memory
				}
				if ( copy_result != 0) {
					return -1;
				}
			}
			break;
		case(FTW_NS): // Stat failed, lack of permission
			fprintf(stderr, "%s: cannot call stat on '%s'\n", OPTS.name, entry_path);
			break;
	}
	return 0;
}
