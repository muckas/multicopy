#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <assert.h>

#define PROGRAM_NAME "multicopy"

int main(int argc, char *argv[]) {
	if (argc < 3) {
		fprintf(stderr, "Usage: %s <source_file> <dest_file_1> ... <dest_file_n>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	int dest_num = argc - 2;
	char *source_path = argv[1];

	// Open source file
	int source_fd = open(source_path, O_RDONLY);
	if (source_fd < 0) {
		fprintf(stderr, "%s: cannot read '%s': %s\n", PROGRAM_NAME, source_path, strerror(errno));
		exit(EXIT_FAILURE);
	}
	struct stat statbuff;
	if (fstat(source_fd, &statbuff) < 0) {
		fprintf(stderr, "%s: cannot stat '%s': %s\n", PROGRAM_NAME, source_path, strerror(errno));
		exit(EXIT_FAILURE);
	}

	// Check if overwriting
	int overwriting = 0;
	for (int i = 0; i < dest_num; i++) {
		struct stat buff;
		if (stat(argv[i+2], &buff) == 0) {
			fprintf(stderr, "%s: file already exists '%s'\n", PROGRAM_NAME, argv[i+2]);
			overwriting = 1;
		}
	}
	if (overwriting == 1) {
		fprintf(stdout, "%s: aborting copy\n", PROGRAM_NAME);
		exit(EXIT_FAILURE);
	}

	// Get file descriptors and allocate space for new files
	int dest_fds[dest_num];
	for (int i = 0; i < dest_num; i++) {
		dest_fds[i] = open(argv[i+2], O_CREAT|O_WRONLY|O_TRUNC, statbuff.st_mode);
		if (dest_fds[i] < 0) {
			fprintf(stderr, "%s: cannot create regular file '%s': %s\n", PROGRAM_NAME, argv[i+2], strerror(errno));
			exit(EXIT_FAILURE);
		}
		int err = posix_fallocate(dest_fds[i], 0, statbuff.st_size);
		if ( err != 0) {
			fprintf(stderr, "%s: cannot allocate space for '%s': %s\n", PROGRAM_NAME, argv[i+2], strerror(err));
			exit(EXIT_FAILURE);
		}
	}

	fprintf(stdout, "Copying %s to %i destinations...\n", source_path, dest_num);
	if (posix_fadvise(source_fd, 0, 0, POSIX_FADV_SEQUENTIAL) != 0) {
		fprintf(stderr, "%s: posix_fadvice on '%s': %s\n", PROGRAM_NAME, source_path, strerror(errno));
		exit(EXIT_FAILURE);
	}

	// Copying files
	char buf[8192];
	ssize_t total_read = 0;
	fprintf(stdout, "Progress:  0%%");
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
			fprintf(stderr, "Error writing %s: %s\n", argv[i+2], strerror(errno));
			break;
			}
			assert(write_result == read_result);
		}
		// Display progress
		total_read += read_result;
		fprintf(stdout, "\b\b\b\b%3.0f%%", ((float)total_read / (float)statbuff.st_size) * 100);
	}
	fprintf(stdout, "\n");

	fprintf(stdout, "Created %i files:\n", dest_num);
	for (int i = 0; i < dest_num; i++) {
		fprintf(stdout, "\t%s\n", argv[i+2]);
	}
	exit(EXIT_SUCCESS);
}
