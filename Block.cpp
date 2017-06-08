#include "Block.h"
#include <cstring>
#include <new>
#include <stdlib.h>

/**
 * Default constructor
 */
Block::Block() : reference_num(0), file_id(-1), block_num(-1), data_size(0), _block_size(-1) {}

/**
 * Block constructor
 * @param file_id the file the block is associated with
 * @param block_num the file block number
 * @param block_size the size of the block
 * @param id unique block id
 */
Block::Block(int file_id, int block_num, blksize_t block_size, int id) : reference_num(0), file_id(file_id), block_num(block_num),
												    id(id), _block_size(block_size)
{
	buffer = aligned_alloc(_block_size, _block_size);
	if (buffer == nullptr)
		throw std::bad_alloc();
	data_size = pread(file_id, buffer, _block_size, (block_num*_block_size));
}

/**
 * Destructor
 */
Block::~Block()
{
	free(buffer);
}

/**
 * Copy constructor
 * @param rhs the block to copy
 */
Block::Block(const Block& rhs) : reference_num(rhs.reference_num), file_id(rhs.file_id),
								 block_num(rhs.block_num), id(rhs.id), _block_size(rhs._block_size)
{
	free(buffer);
	buffer = aligned_alloc(_block_size, _block_size);
	std::memcpy(buffer, rhs.buffer, _block_size);
	data_size = rhs.data_size;
}

/**
 * Move constructor
 * @param rhs the block to move
 */
Block::Block(Block&& rhs) : reference_num(rhs.reference_num), file_id(rhs.file_id), block_num(rhs.block_num),
							id(rhs.id), _block_size(rhs._block_size)
{
	free(buffer);
	buffer= rhs.buffer;
	rhs.buffer = nullptr;
	data_size = rhs.data_size;
}

/**
 * Copy assignment operator
 * @param rhs the block to copy
 * @return this block
 */
Block& Block::operator=(const Block& rhs)
{
	if (this == &rhs)
		return *this;

	copy_base(rhs);

	free(buffer);
	buffer = aligned_alloc(_block_size, _block_size);
	std::memcpy(buffer, rhs.buffer, _block_size);
	data_size = rhs.data_size;
	return *this;
}

/**
 * Move assignment operator
 * @param rhs the block to move
 * @return this block
 */
Block& Block::operator=(Block&& rhs)
{
	if (this == &rhs)
		return *this;

	copy_base(rhs);

	free(buffer);
	buffer = rhs.buffer;
	rhs.buffer = nullptr;
	data_size = rhs.data_size;
	return *this;
}

/**
 * Less than operator
 * First uses the file descriptor, if the file descriptors equal then uses the block id
 * @param lhs block object
 * @param rhs block object
 * @return true if lhs.id < rhs.id, otherwise false
 */
inline bool operator< (const Block& lhs, const Block& rhs)
{
	return lhs.id < rhs.id;
}

/**
 * Equals operator overload
 * @param lhs block object
 * @param rhs block object
 * @return true if block IDs equal, otherwise false.
 */
inline bool operator== (const Block& lhs, const Block& rhs)
{
	return lhs.id == rhs.id;
}

/**
 * Copy the basic data members
 * @param rhs the block to copy from
 */
void Block::copy_base(const Block& rhs)
{
	file_id = rhs.file_id;
	_block_size = rhs._block_size;
	block_num = rhs.block_num;
	reference_num = rhs.reference_num;
	id = rhs.id;
}
