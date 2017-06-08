#include <sys/stat.h>
#include <fcntl.h>
#include <set>
#include <map>
#include <algorithm>
#include <deque>
#include <iostream>
#include <string.h>
#include <stdlib.h>
#include "CacheFS.h"
#include "Block.h"

//--------------------------- definitions ----------------------------------------
/**
 * Path to the temporary folder
 */
// TODO: update before submit
#define TMP_PATH "/tmp"
/**
 * Generates a caches misses log string
 */
#define MISSES_LOG(x) std::string("Misses number: " + std::to_string(x) + "\n")
/**
 * Generates a caches hits log string
 */
#define HITS_LOG(x) std::string("Hits number: " + std::to_string(x) + "\n")
/**
 * Log file permissions
 */
#define LOG_PERMISSIONS 0666

//---------------------------- global variables -----------------------------------
/**
 * Holds the file system block size
 */
blksize_t BLOCK_SIZE;
/**
 * Max number of blocks
 */
int MAX_BLOCKS;
/**
 * Counter of the blocks currently saved in the cache
 */
int g_blocks_counter = 0;
/**
 * Data structure used to map a file descriptor to its blocks
 * fd->set<Block>
 */
std::map<int, std::set<Block*>*> file_block_map;
/**
 * Queue of pairs representing blocks, <file_descriptor, block_id>
 */
std::deque<int> block_queue;
/**
 * Array of block object pointers, each block is allocated to a free cell where block ID number == cell index
 */
Block** pBlockArray;
/**
 * Maps file descriptors to path strings
 */
std::map<int, std::string> fd_path_map;
/**
 * maps a cache fs file descriptor to its original file descriptor
 */
std::map<int, int> cachefd_origfd_map;
/**
 * Maps file descriptor to file size
 */
std::map<int, off_t> fd_size_map;
/**
 * The cache fs algorithm
 */
cache_algo_t CACHE_ALGO;
/**
 * The percentage of blocks in the old partition (rounding down) relevant in FBR algorithm only
 */
double PART_OLD;
/**
 * The percentage of blocks in the new partition (rounding down) relevant in FBR algorithm only
 */
double PART_NEW;
/**
 * Counter for the cache hits
 */
size_t g_hit_counter = 0;
/**
 * Counter for the cache misses
 */
size_t g_miss_counter = 0;

//--------------------------------- function definitions -----------------------------------------------

static blksize_t get_block_size();
static Block* create_block(int fd, int block_num);
static void LRU_update_queue(Block& block_p);
static void LFU_update_queue(Block& block_p);
static void FBR_update_queue(Block* block_p);
static void make_room();
static void LRU_make_room();
static void LFU_make_room();
static void FBR_make_room();
static bool FBR_block_is_new(Block& block);
static int get_free_id();
static Block* get_block(int fd, int block_num);
static void update_queue(Block* block_p);
static void remove_block(int block_id);
static off_t get_file_size(const char* path);
static int get_unique_cache_fd();

//------------------------------- CacheFS functions implementation ----------------------------------

/**
 * Initialize CacheFS
 * @param blocks_num max number of blocks
 * @param cache_algo cache fs algorithm
 * @param f_old old partition size in %
 * @param f_new new partition size in %
 * @return 0 if sucessful, otherwise -1
 */
int CacheFS_init(int blocks_num, cache_algo_t cache_algo, double f_old , double f_new)
{
	BLOCK_SIZE = get_block_size();
	if (BLOCK_SIZE == -1)
		return -1;

	// verify f_old and f_new
	if (cache_algo == FBR)
	{
		if (f_new + f_new > 1)
			return -1;
		if (f_new < 0 || f_old < 0)
			return -1;
	}

	// initialize global variables
	MAX_BLOCKS = blocks_num;
	CACHE_ALGO = cache_algo;
	PART_OLD = f_old;
	PART_NEW = f_new;

	// Initialize the block array
	pBlockArray = (Block**) malloc(sizeof(Block*)*blocks_num);
	if (pBlockArray == nullptr)
		return -1;
	for (int i = 0; i < blocks_num; ++i)
		pBlockArray[i] = nullptr;

	return 0;
}

/**
 * Destroys the CacheFS.
 * This function releases all the allocated resources by the library.
 * @return 0 in case of success, negative value in case of failure.
 * This function always succeeds
 */
int CacheFS_destroy()
{
	// free allocated memory
	for (int i = 0; i < MAX_BLOCKS; ++i)
		if (pBlockArray[i] != nullptr)
			delete pBlockArray[i];
	free(pBlockArray);

	// clear data structures
	block_queue.clear();
	file_block_map.clear();
	fd_path_map.clear();
	cachefd_origfd_map.clear();
	fd_size_map.clear();

	// reset global counters
	g_blocks_counter = 0;
	g_hit_counter = 0;
	g_miss_counter = 0;

	return 0;
}

/**
 * File open operation.
 * Receives a path for a file, opens it, and returns an id
 * @param pathname path to a file
 * @return if successful file id, otherwise -1.
 */
int CacheFS_open(const char *pathname)
{
	// verify path name
	size_t found = std::string(pathname).find(TMP_PATH);
	if (found == std::string::npos || found > 0)
		return -1;

	// get a unique cache file descriptor
	int cache_fd = get_unique_cache_fd();

	// if file is open, return its file descriptor
	for (auto fd : fd_path_map)
		if (strcmp(pathname, fd.second.c_str()) == 0)
		{
			cachefd_origfd_map[cache_fd] = fd.first;
			return cache_fd;
		}

	// open file
	int fd = open(pathname, O_RDONLY | O_DIRECT | O_SYNC);
	if (fd == -1)
		return -1;

	// initialize file map key
	try {
		file_block_map[fd] = new std::set<Block *>;
	} catch (std::bad_alloc e)
	{
		return -1;
	}

	// save file descriptor path
	fd_path_map[fd] = pathname;
	cachefd_origfd_map[cache_fd] = fd;
	fd_size_map[fd] = get_file_size(pathname);
	if (fd_size_map[fd] == -1)
		return -1;

	return cache_fd;
}

/**
 * File close operation.
 * Receives id of a file, and closes it.
 * @param cache_fd cache file descriptor
 * @return 0 if successful, otherwise -1.
 */
int CacheFS_close(int cache_fd)
{
	// find file in the open files data structure
	auto file_iter = cachefd_origfd_map.find(cache_fd);
	if (file_iter == cachefd_origfd_map.end())
		return -1;

	int orig_fd = file_iter->second;
	cachefd_origfd_map.erase(file_iter);

	// if multiple instances of the same file exist, return
	for (auto fd : cachefd_origfd_map)
		if (fd.second == orig_fd)
			return 0;

	// close file
	int ret = close(orig_fd);
	if (ret == -1)
		return -1;

	// remove file from data structures
	auto file_i = file_block_map.find(orig_fd);
	delete file_i->second;	// delete the file block set
	file_block_map.erase(file_i);	// remove file descriptor from open files data structure

	return 0;
}

int CacheFS_pread(int file_id, void *buf, size_t count, off_t offset)
{
	if (offset < 0)
		return -1;

	// check that the file was opened
	auto elem = cachefd_origfd_map.find(file_id);
	if (elem == cachefd_origfd_map.end())
		return -1;

	if (count == 0)
		return 0;

	// calculate first and last block ID numbers
	int first_block_num = (offset/BLOCK_SIZE);
	int last_block_num = ((offset + count)/BLOCK_SIZE);

	size_t pos, out_index = 0;
	int block_num, buffer_index, orig_fd = cachefd_origfd_map[file_id];
	char* output_buffer = (char*) buf;
	Block* block_p;

	// iterate over blocks and read data
	for (block_num = first_block_num; block_num <= last_block_num && out_index < count; ++block_num)
	{
		// out of file bounds check
		if (block_num*BLOCK_SIZE > fd_size_map[orig_fd])
			break;

		// get block pointer
		block_p = get_block(orig_fd, block_num);
		if (block_p == nullptr || block_p->data_size == -1)
			return -1;

		// read data (min(block_size, buffer_data_size) bytes) from block buffer
		for (buffer_index = 0; buffer_index < BLOCK_SIZE && buffer_index < block_p->data_size; ++buffer_index)
		{
			pos = (block_num*BLOCK_SIZE) + buffer_index;
			if (pos >= (size_t)offset && pos < offset + count)
			{
				output_buffer[out_index++] = ((char*)block_p->buffer)[buffer_index];
			}
		}

		// update block queue
		update_queue(block_p);
	}

	return out_index;
}

/**
 * Print the cache status to a log file
 * @param log_path path to the log file
 * @return 0 if successful, otherwise -1.
 */
int CacheFS_print_cache (const char *log_path)
{
	// open log file
	int log_fd = open(log_path, O_CREAT | O_WRONLY | O_APPEND, LOG_PERMISSIONS);
	if (log_fd == -1)
		return -1;

	std::string log_line = "";
	Block* block_p;

	// iterate over the block queue from the end
	for (int i = block_queue.size()-1; i >= 0; --i)
	{
		block_p = pBlockArray[block_queue[i]];
		// create string
		log_line += fd_path_map[block_p->file_id] + " " + std::to_string(block_p->block_num) + "\n";
		ssize_t ret = write(log_fd, log_line.c_str(), log_line.length());
		if (ret == -1)
			return -1;

		log_line = "";	// reset string
	}
	int close_ret = close(log_fd);
	return close_ret;
}

/**
 * Print number of cache hits and missed to a given log file
 * @param log_path path to a log file
 * @return 0 if successful, otherwise negative integer
 */
int CacheFS_print_stat (const char *log_path)
{
	// open log file
	int log_fd = open(log_path, O_CREAT | O_WRONLY | O_APPEND, LOG_PERMISSIONS);
	if (log_fd == -1)
		return -1;

	// generate log string
	std::string log_str = HITS_LOG(g_hit_counter);
	log_str += MISSES_LOG(g_miss_counter);

	ssize_t ret = write(log_fd, log_str.c_str(), log_str.length());
	if (ret == -1)
		return -1;

	int close_ret = close(log_fd);
	return close_ret;
}

//--------------------------- static functions implementation ------------------------------
/**
 * Returns the file system block size
 * @return if successful the block size, otherwise -1.
 */
static blksize_t get_block_size()
{
	struct stat fi;
	int ret = stat(TMP_PATH, &fi);

	if (ret != 0)
		return ret;

	return fi.st_blksize;
}

/**
 * Creates a new block
 * @param fd file descriptor
 * @param block_num the number of the block
 * @return pointer to a new block, nullptr when failed
 */
static Block* create_block(int fd, int block_num)
{
	Block * new_block;

	make_room();
	int id = get_free_id();

	// create new block
	try
	{
		new_block = new Block(fd, block_num, BLOCK_SIZE, id);
	} catch (std::bad_alloc e)
	{
		return nullptr;
	}

	// add new block to data structures
	pBlockArray[id] = new_block;
	file_block_map[fd]->insert(new_block);

	// increase global block counter
	g_blocks_counter++;
	return new_block;
}

/**
 * Update the position of the given block in the LRU queue
 * @param block_p pointer to a block
 */
static void LRU_update_queue(Block& block)
{
	// remove block from queue
	auto block_iter = std::find(block_queue.begin(), block_queue.end(), block.id);
	if (block_iter != block_queue.end())
		block_queue.erase(block_iter);

	// add it to the back of the queue
	block_queue.push_back(block.id);
}

/**
 * Adds the given block to the block_queue if it's not there
 * @param block the block to add to the block_queue
 */
static void LFU_update_queue(Block& block)
{
	block.reference_num++;

	// remove block from queue
	auto block_iter = std::find(block_queue.begin(), block_queue.end(), block.id);
	if (block_iter != block_queue.end())
		block_queue.erase(block_iter);

	// find first block with reference count higher than this block
	block_iter = std::find_if(block_queue.begin(), block_queue.end(), [&block](int& id)->bool{
		return pBlockArray[id]->reference_num > block.reference_num;
	});

	// if this block has the highest reference count, insert it to the end of the queue
	// otherwise insert it before the a block with a higher reference count
	if (block_iter == block_queue.end())
		block_queue.push_back(block.id);
	else
		block_queue.insert(block_iter, block.id);
}

/**
 * Updates the blocks queue and the block reference counter according to the FBR algorithm
 * @param block_p block object pointer
 */
static void FBR_update_queue(Block* block_p)
{
	// update references number only in blocks not in the new partition
	if (!FBR_block_is_new(*block_p))
		block_p->reference_num++;

	// update the queue according to the LRU algorithm
	LRU_update_queue(*block_p);
}

/**
 * Returns true if the given block is in the new partition
 * @param block a block object
 * @return true if the block is in the new partition, otherwise false.
 */
static bool FBR_block_is_new(Block& block)
{
	size_t i;
	// find block index in block queue
	for (i = 0; i < block_queue.size() && block.id != block_queue[i]; ++i)
		;

	// block isn't in the block queue => not in new partition
	if (i == block_queue.size())
		return false;

	// The new blocks are at the end of the queue
	// This calculates the block position in percentage from the end of the queue
	// last element = 0.0, first element = 1.0
	double pos = ((double)(block_queue.size() - (i + 1)))/block_queue.size();

	return (pos <= PART_NEW);
}

/**
 * Makes room in the cache according to the cache algorithm in the cache data structure if needed.
 */
static void make_room()
{
	if (g_blocks_counter < MAX_BLOCKS)
		return;

	if (CACHE_ALGO == LRU)
		LRU_make_room();
	else if (CACHE_ALGO == LFU)
		LFU_make_room();
	else if (CACHE_ALGO == FBR)
		FBR_make_room();
}

/**
 * Removes the least recently used block
 */
static void LRU_make_room()
{
	// get block id to remove
	int block_id = block_queue.front();
	remove_block(block_id);
}

/**
 * Remove the least frequently used block
 */
static void LFU_make_room()
{
	// get block to remove
	int block_id = block_queue.front();
	remove_block(block_id);
}

/**
 * Removes the block with the lowest reference count in the old partition
 */
static void FBR_make_room()
{
	size_t min_ref, new_ref, i;
	// set the initial min reference number
	int min_block_id = block_queue[0];
	min_ref = pBlockArray[min_block_id]->reference_num;
	// iterate over the old partition and find the block with the min reference number
	// the old partition is block_queue[0] up to block_queue[block_queue.size()*PART_OLD]
	for (i = 1 ; ((double)(i + 1))/block_queue.size() <= PART_OLD && i < block_queue.size(); ++i)
	{
		new_ref = pBlockArray[block_queue[i]]->reference_num;
		if (new_ref < min_ref)
		{
			min_ref = new_ref;
			min_block_id = block_queue[i];
		}
	}

	remove_block(min_block_id);
}

/**
 * Returns an unused block id
 * id = free cell in the block array
 * @return unique block id, -1 if failed
 */
static int get_free_id()
{
	int i;
	for (i = 0; i < MAX_BLOCKS; ++i)
		if (pBlockArray[i] == nullptr)
			break;
	return i;
}

/**
 * Returns a pointer to the requested block
 * Creates it if needed
 * @param fd file descriptor
 * @param block_num the number of the block
 * @return pointer to the requested block, nullptr when failed
 */
static Block* get_block(int fd, int block_num)
{
	Block* block_p;
	std::set<Block*>& block_set = *file_block_map[fd];

	// find block if exists
	auto block_iter = std::find_if(block_set.begin(), block_set.end(), [&block_num](Block* const block) {
		return block->block_num == block_num;
	});

	if (block_iter == block_set.end())
	{
		// block doesn't exist, create it
		g_miss_counter++;
		block_p = create_block(fd, block_num);	// block doesn't exist, create a new block
	}
	else
	{
		// block exists, return it
		g_hit_counter++;
		block_p = *block_iter;
	}

	return block_p;
}

/**
 * Update the block queue according to the cache algorithm
 * @param block_p block pointer
 */
static void update_queue(Block* block_p)
{
	// update blocks queue
	if (CACHE_ALGO == LRU)
		LRU_update_queue(*block_p);
	else if (CACHE_ALGO == LFU)
		LFU_update_queue(*block_p);
	else if (CACHE_ALGO == FBR)
		FBR_update_queue(block_p);
}

/**
 * Removes the requested block from all the data structures
 * and frees allocated memory
 * @param block_id the block to remove
 */
static void remove_block(int block_id)
{
	// remove block from queue
	auto block_iter = std::find(block_queue.begin(), block_queue.end(), block_id);
	block_queue.erase(block_iter);

	Block* block_p = pBlockArray[block_id];

	// remove block from blocks array
	pBlockArray[block_id] = nullptr;

	// remove block from file map
	auto blocks_set = file_block_map[block_p->file_id];
	blocks_set->erase(block_p);

	// free allocated memory
	delete block_p;

	g_blocks_counter--;
}

/**
 * Returns the file size for a given file
 * @param path path to a file
 * @return if successful the file size, otherwise -1.
 */
static off_t get_file_size(const char* path)
{
	struct stat fi;
	int ret = stat(path, &fi);

	if (ret != 0)
		return ret;

	return fi.st_size;
}

/**
 * Returns a unique cache fs file descriptor
 * @return unique cache fs file descriptor
 */
static int get_unique_cache_fd()
{
	static int cache_fd = 0;
	// while cache_fd is already used
	while (cachefd_origfd_map.find(cache_fd) != cachefd_origfd_map.end())
	{
		cache_fd++;
		if (cache_fd < 0)
			cache_fd = 0;
	}
	return cache_fd;
}
