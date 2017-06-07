#ifndef CACHEFS_BLOCK_H
#define CACHEFS_BLOCK_H

#include <unistd.h>

struct Block {
	/**
	 * The file the block belongs to
	 */
	int file_id;

	/**
	 * The block number
	 */
	int block_num;

	/**
	 * Unique block id
	 */
	int id;

	/**
	 * The number of times this block was referenced
	 */
	unsigned long long reference_num = 0;

	/**
	 * Holds the block data
	 */
	void* buffer = nullptr;

	/**
	 * The number of bytes of data the block actually contains
	 */
	ssize_t data_size;

	/**
	 * Default constructor
	 */
	Block();

	/**
	 * Block constructor
	 * @param file_id the file the block is associated with
	 * @param block_num the file block number
	 * @param block_size the size of the block
	 * @param id unique block id
	 */
	Block(int file_id, int block_num, int block_size, int id);

	/**
	 * Destructor
	 */
	~Block();

	/**
	 * Copy constructor
	 * @param rhs the block to copy
	 */
	Block(const Block& rhs);

	/**
	 * Move constructor
	 * @param rhs the block to move
	 */
	Block(Block&& rhs);

	/**
	 * Copy assignment operator
	 * @param rhs the block to copy
	 * @return this block
	 */
	Block& operator=(const Block& rhs);

	/**
	 * Move assignment operator
	 * @param rhs the block to move
	 * @return this block
	 */
	Block& operator=(Block&& rhs);

	/**
	 * Less than operator
	 * First uses the file descriptor, if the file descriptors equal then uses the block id
	 * @param lhs block object
	 * @param rhs block object
	 * @return true if lhs.fd<rhs.fd, or if lhs.fd==rhs.fd then true lhs.block_id<rhs.block_id otherwise false
	 */
	friend bool operator< (const Block& lhs, const Block& rhs);

	/**
	 * Equals operator overload
	 * @param lhs block object
	 * @param rhs block object
	 * @return true if block IDs equal, otherwise false.
	 */
	friend bool operator== (const Block& lhs, const Block& rhs);

private:
	/**
	 * The file system block size
	 */
	int _block_size;

	/**
	 * Copy the basic data members
	 * @param rhs the block to copy from
	 */
	void copy_base(const Block& rhs);
};

#endif //CACHEFS_BLOCK_H
