#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#define PROGRAM_NAME "multicopy"
#define VERSION "1.1+"

struct Options {
	char *name;
	bool force;
	bool progress;
	bool verbose;
	char *dest[];
} OPTS = {PROGRAM_NAME, false, false, false};

void print_usage(char *program_name) {
	fprintf(stdout, "Usage: %s [OPTION]... SOURCE DESTINATION...\n", program_name);
}

void print_help(char *program_name) {
	fprintf(stdout, "%s %s\n", PROGRAM_NAME, VERSION);
	print_usage(program_name);
	fprintf(stdout, "\
Copy SOURCE to multiple DESTINATION(s)\n\
\n\
	-h	display this help and exit\n\
	-f	force copy even if destination files exist (overwrites files)\n\
	-p	show progress (persent copied)\n\
	-v	be verbose\n\
");
}

int copy_file(char *source_path, struct stat source_stat, char *dest[], int dest_num) {

	// Open source file
	int source_fd = open(source_path, O_RDONLY);
	if (source_fd < 0) {
		fprintf(stderr, "%s: cannot read '%s': %s\n", OPTS.name, source_path, strerror(errno));
		exit(EXIT_FAILURE);
	}

	// Get file descriptors and allocate space for new files
	int dest_fds[dest_num];
	for (int i = 0; i < dest_num; i++) {
		dest_fds[i] = open(dest[i], O_CREAT|O_WRONLY|O_TRUNC, source_stat.st_mode);
		if (dest_fds[i] < 0) {
			fprintf(stderr, "%s: cannot create regular file '%s': %s\n", OPTS.name, dest[i], strerror(errno));
			return -1;
		}
		int err = posix_fallocate(dest_fds[i], 0, source_stat.st_size);
		if ( err != 0) {
			fprintf(stderr, "%s: cannot allocate space for '%s': %s\n", OPTS.name, dest[i], strerror(err));
			return -1;
		}
	}

	if (OPTS.verbose) fprintf(stdout, "Copying %s to %i destinations...\n", source_path, dest_num);
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

		for (int i = 0; i < dest_num; i++) {
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
			fprintf(stdout, "\b\b\b\b%3.0f%%", ((float)total_read / (float)source_stat.st_size) * 100);
		}
	}
	if (OPTS.progress) fprintf(stdout, "\n");
	return 1;
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
	int dest_num = argc - optind - 1;
	if (dest_num < 1) {
		fprintf(stderr, "%s: not enough arguments\n", OPTS.name);
		print_usage(OPTS.name);
		exit(EXIT_SUCCESS);
	}

	char *source_path = argv[optind];
	optind++; // optind now on first DESTINATION argument
	char *dest[argc-optind];
	for (int i = 0; i < argc-optind; i++) {
		dest[i] = argv[optind+i];
	}

	if (!OPTS.force) {
		// Check if overwriting
		int overwriting = 0;
		for (int i = 0; i < dest_num; i++) {
			struct stat buff;
			if (stat(dest[i], &buff) == 0) {
				fprintf(stderr, "%s: file already exists '%s'\n", OPTS.name, dest[i]);
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
	if (stat(source_path, &statbuff) < 0) {
		fprintf(stderr, "%s: cannot stat '%s': %s\n", OPTS.name, source_path, strerror(errno));
		exit(EXIT_FAILURE);
	}
	if (S_ISREG(statbuff.st_mode)) {
		int copy_result = copy_file(source_path, statbuff, dest, dest_num);
		if (copy_result != 1) exit(EXIT_FAILURE);
	} else {
		fprintf(stderr, "%s: '%s' is not a regular file\n", OPTS.name, source_path);
		exit(EXIT_FAILURE);
	}

	if (OPTS.verbose) {
		fprintf(stdout, "Created %i files:\n", dest_num);
		for (int i = 0; i < dest_num; i++) {
			fprintf(stdout, "\t%s\n", argv[i+optind]);
		}
	}
	exit(EXIT_SUCCESS);
}
