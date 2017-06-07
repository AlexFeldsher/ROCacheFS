#include <iostream>
#include <cstring>
#include <unistd.h>
#include "CacheFS.h"
#include "debug.h"

#define TMP_FILE "/home/alex/fuck"
#define LOG_FILE "/home/alex/log.txt"

int main() {
	int ret = CacheFS_init(2, FBR, 0.1, 0.2);
	DEBUG("initialized CacheFS " << ret);
	int fd = CacheFS_open(TMP_FILE);
	DEBUG("opened " << TMP_FILE << " " << fd << " " << std::strerror(errno));

	char* buffer = new char[4];
	char* buffer2 = (char*) aligned_alloc(4096, 4096*2);
	ret = pread(fd, buffer2, 4096*2, 0);
	DEBUG("ret = " << ret << " " << std::strerror(errno));
	for (int i=4093;i<4097; ++i)
		DEBUG(buffer2[i]);
	ret = CacheFS_pread(fd, buffer, 4, 4093);
	ret = CacheFS_pread(fd, buffer, 4, 4093);
	ret = CacheFS_pread(fd, buffer, 4, 4093);
	DEBUG("read file ");
	for (int i = 0; i < 4; ++i)
		DEBUG(buffer[i]);

	ret = CacheFS_print_cache(LOG_FILE);
	ret = CacheFS_print_stat(LOG_FILE);
	ret = CacheFS_close(fd);
	delete[] buffer;
	free(buffer2);
	CacheFS_destroy();

	return 0;
}