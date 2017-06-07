#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <set>
#include <vector>
#include <map>
#include <algorithm>
#include <deque>
#include <sstream>
#include <iostream>
#include <cstring>
#include "CacheFS.h"
#include "Block.h"
#include "debug.h"

/**
 * Path to the temporary folder
 */
// TODO: update before submit
#define TMP_PATH "/tmp"

/**
 * Generates a caches misses log string
 */
#define MISSES_LOG(x) std::string("Misses number: " + std::to_string(x) + ".\n")

/**
 * Generates a caches hits log string
 */
#define HITS_LOG(x) std::string("Hits number: " + std::to_string(x) + ".\n")

/**
 * Holds the file system block size
 */
int BLOCK_SIZE;

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

Block** pBlockArray;

/**
 * Maps file descriptors to path strings
 */
std::map<int, std::string> fd_path_map;

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
static int get_block_size();
static Block* create_block(int file_id, int block_num);
static void LRU_update_queue(Block& block_p);
static void LFU_update_queue(Block& block_p);
static void FBR_update_queue(Block& block_p);
static void make_room();
static void LRU_make_room();
static void LFU_make_room();
static void FBR_make_room();
static bool FBR_block_is_new(Block& block);
static int get_free_id();
static Block* get_block(int file_id, int block_num);
static void update_queue(Block* block_p);
static void remove_block(int block_id);

/**
 * Initialize CacheFS
 * @param blocks_num
 * @param cache_algo
 * @param f_old
 * @param f_new
 * @return
 */
int CacheFS_init(int blocks_num, cache_algo_t cache_algo, double f_old , double f_new)
{
	BLOCK_SIZE = get_block_size();
	DEBUG("block size = " << BLOCK_SIZE);
	if (BLOCK_SIZE == -1)
		return -1;

	MAX_BLOCKS = blocks_num;
	CACHE_ALGO = cache_algo;
	PART_OLD = f_old;
	PART_NEW = f_new;

	pBlockArray = (Block**) malloc(sizeof(Block*)*blocks_num);
	for (int i = 0; i < blocks_num; ++i)
		pBlockArray[i] = nullptr;

	return 0;
}

/**
 * Destroys the CacheFS.
 * This function releases all the allocated resources by the library.
 * @return 0 in case of success, negative value in case of failure.
 * The function will fail if a system call or a library function fails.
 */
int CacheFS_destroy()
{
	// free allocated memory
	for (int i = 0; i < MAX_BLOCKS; ++i)
		if (pBlockArray[i] != nullptr)
			delete pBlockArray[i];
	delete pBlockArray;

	// clear data structures
	block_queue.clear();
	file_block_map.clear();
	fd_path_map.clear();

	g_blocks_counter = 0;
	g_hit_counter = 0;
	g_miss_counter = 0;
}


/**
 * File open operation.
 * Receives a path for a file, opens it, and returns an id
 * @param pathname path to a file
 * @return if successful file id, otherwise -1.
 */
int CacheFS_open(const char *pathname)
{
	// TODO: add /tmp path validation
	// if file was opened already, return its file descriptor
	for (auto fd : fd_path_map)
		if (strcmp(pathname, fd.second.c_str()) == 0)
			return fd.first;

	// open file
	int fd = open(pathname, O_RDONLY | O_DIRECT | O_SYNC);
	if (fd == -1)
		return -1;

	file_block_map[fd] = new std::set<Block *>;
	fd_path_map[fd] = pathname;

	return fd;
}

/**
 * File close operation.
 * Receives id of a file, and closes it.
 * @param file_id file to close
 * @return 0 if successful, otherwise -1.
 */
// TODO: blocks should not be removed
int CacheFS_close(int file_id)
{
	// find file in the open files set
	auto file_iter = file_block_map.find(file_id);
	if (file_iter == file_block_map.end())
		return -1;

	// close file
	int ret = close(file_id);

	if (ret != -1)
	{
		delete file_iter->second;	// delete the file block set
		file_block_map.erase(file_iter);    // remove file from open files set
	}

	return ret;
}


int CacheFS_pread(int file_id, void *buf, size_t count, off_t offset)
{

	// check that the file was opened
	auto elem = file_block_map.find(file_id);
	DEBUG("found file " << (elem != file_block_map.end()));
	if (elem == file_block_map.end())
		return -1;

	// calculate first and last block ID numbers
	int first_block_num = (offset/BLOCK_SIZE);
	int last_block_num = ((offset + count)/BLOCK_SIZE);

	int block_num, buffer_index, out_index = 0, pos;
	char* out_buffer = (char*) buf;
	Block* block_p;
	std::set<Block*>& block_set = *file_block_map[file_id];

	// iterate over blocks and read data
	for (block_num = first_block_num; block_num <= last_block_num && out_index < count; ++block_num)
	{
		// get block pointer
		block_p = get_block(file_id, block_num);
		if (block_p->data_size == -1)
			return -1;

		// read data
		for (buffer_index = 0; buffer_index < BLOCK_SIZE && buffer_index < block_p->data_size; ++buffer_index)
		{
			pos = (block_num*BLOCK_SIZE) + buffer_index;
			if (pos >= offset && pos < offset + count)
			{
				out_buffer[out_index++] = ((char*)block_p->buffer)[buffer_index];
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
	// TODO: append or truncate?

	int log_fd = open(log_path, O_CREAT | O_WRONLY | O_APPEND, 0666);
	if (log_fd == -1)
		return -1;

	std::string log_line = "";
	Block* block_p;

	for (int i = block_queue.size()-1; i >= 0; --i)
	{
		// create string
		block_p = pBlockArray[block_queue[i]];
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
 *
 * @param log_path
 * @return
 */
int CacheFS_print_stat (const char *log_path)
{
	int log_fd = open(log_path, O_CREAT | O_WRONLY | O_APPEND, 0666);
	if (log_fd == -1)
		return -1;

	std::string log_str = HITS_LOG(g_hit_counter);
	log_str += MISSES_LOG(g_miss_counter);

	ssize_t ret = write(log_fd, log_str.c_str(), log_str.length());
	if (ret == -1)
		return -1;

	int close_ret = close(log_fd);
	return close_ret;
}

/**
 * Returns the file system block size
 * @return if successful the block size, otherwise -1.
 */
static int get_block_size()
{
	struct stat fi;
	int ret = stat(TMP_PATH, &fi);

	if (ret != 0)
		return ret;

	return fi.st_blksize;
}

/**
 * Creates a new block
 * @param file_id file descriptor
 * @param block_num the number of the block
 * @return iterator pointing to the block
 */
static Block* create_block(int file_id, int block_num)
{
	make_room();
	DEBUG("Creating block; file " << file_id << " block_num " << block_num);
	int id = get_free_id();
	Block* new_block = new Block(file_id, block_num, BLOCK_SIZE, id);

	pBlockArray[id] = new_block;

	auto blocks_set = file_block_map[file_id];
	blocks_set->insert(new_block);
	g_blocks_counter++;
	return new_block;
}

/**
 * Update the position of the given block in the LRU queue
 * @param block_p pointer to a block
 */
static void LRU_update_queue(Block& block)
{
	DEBUG("Updating LRU; file " << block.file_id << " block " << block.block_num);
	auto block_iter = std::find(block_queue.begin(), block_queue.end(), block.id);
	if (block_iter != block_queue.end())
	{
		DEBUG("found block in LRU, removing...");
		block_queue.erase(block_iter);
	}
	block_queue.push_back(block.id);
}

/**
 * Adds the given block to the block_queue if it's not there
 * @param block the block to add to the block_queue
 */
static void LFU_update_queue(Block& block)
{
	block.reference_num++;
	// if block not in queue add it
	auto block_iter = std::find(block_queue.begin(), block_queue.end(), block.id);
	if (block_iter == block_queue.end())
		block_queue.push_back(block.id);

	DEBUG("Sorting block queue");
	// sort the block queue according to the block reference counter
	std::sort(block_queue.begin(), block_queue.end(), [](int &a_id, int &b_id) {
		// get pointers to a and b blocks
		Block& a_block_p = *pBlockArray[a_id];
		Block& b_block_p = *pBlockArray[b_id];

		return a_block_p.reference_num < b_block_p.reference_num;
	});
}

/**
 * Updates the blocks queue and the block reference counter according to the FBR algorithm
 * @param block block object
 */
static void FBR_update_queue(Block& block)
{
	// update references number only in blocks not in the new partition
	if (!FBR_block_is_new(block))
		block.reference_num++;

	// update the queue according to the LRU algorithm
	LRU_update_queue(block);
}

/**
 * Returns true if the given block is in the new partition
 * @param block a block object
 * @return true if the block is in the new partition, otherwise false.
 */
static bool FBR_block_is_new(Block& block)
{
	int i;
	// find block index in block queue
	for (i = 0; i < block_queue.size(); ++i)
		if (block.id == block_queue[i])
			break;

	if (i == block_queue.size())
		return false;

	// The new blocks are at the end of the queue
	// This calculates the block position in percentage from the end of the queue
	// last element = 0.0, first element = 1.0
	double pos = (block_queue.size() - (i + 1))/block_queue.size();

	return (pos < PART_NEW);
}

/**
 * Makes room according to the cache algorithm in the cache data structure if needed.
 */
static void make_room()
{
	if (g_blocks_counter < MAX_BLOCKS)
		return;
	DEBUG("making room");
	if (CACHE_ALGO == LRU)
		LRU_make_room();
	else if (CACHE_ALGO == LFU)
		LFU_make_room();
	else if (CACHE_ALGO == FBR)
		FBR_make_room();
}

/**
 * Removes the least recent used block
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
	// find block to remove
	int block_id = block_queue.front();

	remove_block(block_id);
}

/**
 * Removes the block with the lowest reference count in the old partition
 */
static void FBR_make_room()
{
	int min_ref, new_ref, i;
	// set the initial min reference number
	int min_block_id = block_queue[0];
	min_ref = pBlockArray[min_block_id]->reference_num;
	// iterate over the old partition and find the block with the min reference number
	for (i = 1 ; (i + 1)/block_queue.size() < PART_OLD && i < block_queue.size(); ++i)
	{
		new_ref = pBlockArray[i]->reference_num;
		if (new_ref < min_ref)
		{
			min_ref = new_ref;
			min_block_id = i;
		}
	}

	DEBUG("found block to remove; id=" << min_block_id << ", ref=" << min_ref);

	remove_block(min_block_id);
}

/**
 * Returns an unused block id
 * id = free cell in the block array
 * @return unique block id
 */
static int get_free_id()
{
	for (int i = 0; i < MAX_BLOCKS; ++i)
		if (pBlockArray[i] == nullptr)
			return i;
}

/**
 * Returns a pointer to the requested block
 * Creates it if needed
 * @param file_id file descriptor
 * @param block_num the number of the block
 * @return pointer to the requested block
 */
static Block* get_block(int file_id, int block_num)
{
	Block* block_p;
	std::set<Block*>& block_set = *file_block_map[file_id];

	// find block if exists
	auto block_iter = std::find_if(block_set.begin(), block_set.end(), [&block_num](Block* const block) {
		return block->block_num == block_num;
	});

	if (block_iter == block_set.end())
	{
		g_miss_counter++;
		block_p = create_block(file_id, block_num);	// block doesn't exist, create a new block
	}
	else
	{
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
		FBR_update_queue(*block_p);
}

/**
 * Removes the requested block from all the data structures
 * and frees allocated memory
 * @param block_id the block to remove
 */
static void remove_block(int block_id)
{
	DEBUG("Removing id #" << block_id);
	// remove block from queue
	block_queue.pop_front();

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
