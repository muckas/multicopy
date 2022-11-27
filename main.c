#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#define PROGRAM_NAME "multicopy"

int main(int argc, char *argv[]) {
	if (argc < 3) {
		fprintf(stderr, "Usage: %s <source_file> <dest_file_1> ... <dest_file_n>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	int dest_num = argc - 2;
	char *source_path = argv[1];

	int source_fd = open(source_path, O_RDONLY);
	if (source_fd < 0) {
		fprintf(stderr, "%s: cannot read '%s': %s\n", PROGRAM_NAME, source_path, strerror(errno));
		exit(EXIT_FAILURE);
	}
	struct stat sb;
	int stat_result = fstat(source_fd, &sb);
	if (stat_result < 0) {
		fprintf(stderr, "%s: cannot stat '%s': %s\n", source_path, strerror(errno));
		exit(EXIT_FAILURE);
	}

	int dest_fds[dest_num];
	for (int i = 0; i < dest_num; i++) {
		dest_fds[i] = open(argv[i+2], O_CREAT|O_WRONLY|O_TRUNC, sb.st_mode);
		if (dest_fds[i] < 0) {
			fprintf(stderr, "%s: cannot create regular file '%s': %s\n", PROGRAM_NAME, argv[i+2], strerror(errno));
			exit(EXIT_FAILURE);
		}
	}

	char buf[8192];
	fprintf(stdout, "Copying %s to %i destinations...\n", source_path, dest_num);

	while (1) {
		ssize_t read_result = read(source_fd, &buf[0], sizeof(buf));
		if (errno != 0) {
		fprintf(stderr, "Error reading %s: %s\n", source_path, strerror(errno));
		break;
		}
		if (!read_result) break;

		for (int i = 0; i < dest_num; i++) {
			ssize_t write_result = write(dest_fds[i], &buf[0], read_result);
			if (errno != 0) {
			fprintf(stderr, "Error writing %s: %s\n", argv[i+2], strerror(errno));
			break;
			}
		}
	}

	fprintf(stdout, "Created %i files:\n", dest_num);
	for (int i = 0; i < dest_num; i++) {
		fprintf(stdout, "%s\n", argv[i+2]);
	}
	exit(EXIT_SUCCESS);
}
