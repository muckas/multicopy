#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <assert.h>

#define PROGRAM_NAME "multicopy"

void print_usage(char *program_name) {
	fprintf(stdout, "Usage: %s [OPTION]... SOURCE DESTINATION...\n", program_name);
}

void print_help(char *program_name) {
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

int main(int argc, char *argv[]) {
	char *program_name = argv[0];
	bool opt_force = false;
	bool opt_progress = false;
	bool opt_verbose = false;

	// Parse command line arguments
	int opt;
	while ((opt = getopt(argc, argv, ":hfpv")) != -1) {
		switch(opt) {
			case 'h':
				print_help(program_name);
				exit(EXIT_SUCCESS);
				break;
			case 'f':
				opt_force = true;
				break;
			case 'p':
				opt_progress = true;
				break;
			case 'v':
				opt_verbose = true;
				break;
			case '?':
				fprintf(stderr, "%s: invalid option -- '%c'\n", program_name, optopt);
				fprintf(stdout, "Try '%s -h' for more information'\n", program_name);
				exit(EXIT_FAILURE);
				break;
		}
	}
	// Count extra arguments
	int dest_num = argc - optind - 1;
	if (dest_num < 1) {
		fprintf(stderr, "%s: not enough arguments\n", program_name);
		print_usage(program_name);
		exit(EXIT_SUCCESS);
	}

	char *source_path = argv[optind];
	optind++; // optind now on first DESTINATION argument

	// Open source file
	int source_fd = open(source_path, O_RDONLY);
	if (source_fd < 0) {
		fprintf(stderr, "%s: cannot read '%s': %s\n", program_name, source_path, strerror(errno));
		exit(EXIT_FAILURE);
	}
	struct stat statbuff;
	if (fstat(source_fd, &statbuff) < 0) {
		fprintf(stderr, "%s: cannot stat '%s': %s\n", program_name, source_path, strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (!opt_force) {
		// Check if overwriting
		int overwriting = 0;
		for (int i = 0; i < dest_num; i++) {
			struct stat buff;
			if (stat(argv[i+optind], &buff) == 0) {
				fprintf(stderr, "%s: file already exists '%s'\n", program_name, argv[i+optind]);
				overwriting = 1;
			}
		}
		if (overwriting == 1) {
			fprintf(stdout, "%s: aborting copy, use '-f' to overwrite existing files\n", program_name);
			exit(EXIT_FAILURE);
		}
	}

	// Get file descriptors and allocate space for new files
	int dest_fds[dest_num];
	for (int i = 0; i < dest_num; i++) {
		dest_fds[i] = open(argv[i+optind], O_CREAT|O_WRONLY|O_TRUNC, statbuff.st_mode);
		if (dest_fds[i] < 0) {
			fprintf(stderr, "%s: cannot create regular file '%s': %s\n", program_name, argv[i+optind], strerror(errno));
			exit(EXIT_FAILURE);
		}
		int err = posix_fallocate(dest_fds[i], 0, statbuff.st_size);
		if ( err != 0) {
			fprintf(stderr, "%s: cannot allocate space for '%s': %s\n", program_name, argv[i+optind], strerror(err));
			exit(EXIT_FAILURE);
		}
	}

	if (opt_verbose) fprintf(stdout, "Copying %s to %i destinations...\n", source_path, dest_num);
	if (posix_fadvise(source_fd, 0, 0, POSIX_FADV_SEQUENTIAL) != 0) {
		fprintf(stderr, "%s: posix_fadvice on '%s': %s\n", program_name, source_path, strerror(errno));
		exit(EXIT_FAILURE);
	}

	// Copying files
	char buf[8192];
	ssize_t total_read = 0;
	if (opt_progress) fprintf(stdout, "Progress:  0%%");
	while (1) {
		ssize_t read_result = read(source_fd, &buf[0], sizeof(buf));
		if (read_result == -1) {
		fprintf(stderr, "Error reading %s: %s\n", source_path, strerror(errno));
		break;
		}
		if (!read_result) break; // Source file ended

		for (int i = 0; i < dest_num; i++) {
			ssize_t write_result = write(dest_fds[i], &buf[0], read_result);
			if (write_result == -1) {
			fprintf(stderr, "Error writing %s: %s\n", argv[i+optind], strerror(errno));
			break;
			}
			assert(write_result == read_result);
		}
		// Display progress
		if (opt_progress) {
			total_read += read_result;
			fprintf(stdout, "\b\b\b\b%3.0f%%", ((float)total_read / (float)statbuff.st_size) * 100);
		}
	}
	if (opt_progress) fprintf(stdout, "\n");

	if (opt_verbose) {
		fprintf(stdout, "Created %i files:\n", dest_num);
		for (int i = 0; i < dest_num; i++) {
			fprintf(stdout, "\t%s\n", argv[i+optind]);
		}
	}
	exit(EXIT_SUCCESS);
}
