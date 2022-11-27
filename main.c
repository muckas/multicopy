#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

int main(int argc, char *argv[]) {
	char *source_path = argv[1];
	char *dest_path = argv[2];

	int source_fd = open(source_path, O_RDONLY);
	if (source_fd < 0) {
		fprintf(stderr, "Error reading %s: %s\n", source_path, strerror(errno));
		exit(EXIT_FAILURE);
	}
	struct stat sb;
	int stat_result = fstat(source_fd, &sb);
	if (stat_result < 0) {
		fprintf(stderr, "Error reading %s: %s\n", source_path, strerror(errno));
		exit(EXIT_FAILURE);
	}

	int dest_fd = open(dest_path, O_CREAT|O_WRONLY|O_TRUNC, sb.st_mode);
	if (dest_fd < 0) {
		fprintf(stderr, "Error writing %s: %s\n", dest_path, strerror(errno));
		exit(EXIT_FAILURE);
	}

	char buf[8192];

	while (1) {
		ssize_t read_result = read(source_fd, &buf[0], sizeof(buf));
		if (errno != 0) {
		fprintf(stderr, "Error reading %s: %s\n", source_path, strerror(errno));
		break;
		}
		if (!read_result) break;

		ssize_t write_result = write(dest_fd, &buf[0], read_result);
		if (errno != 0) {
		fprintf(stderr, "Error writing %s: %s\n", dest_path, strerror(errno));
		break;
		}
	}
	exit(EXIT_SUCCESS);
}
