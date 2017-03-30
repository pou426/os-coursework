/*
 * TAR File-system Driver
 * SKELETON IMPLEMENTATION -- TO BE FILLED IN FOR TASK (3)
 */

/*
 * STUDENT NUMBER: s1330027
 */
#include "tarfs.h"
#include <infos/kernel/log.h>

using namespace infos::fs;
using namespace infos::drivers;
using namespace infos::drivers::block;
using namespace infos::kernel;
using namespace infos::util;
using namespace tarfs;

/**
 * TAR files contain header data encoded as octal values in ASCII.  This function
 * converts this terrible representation into a real unsigned integer.
 *
 * You DO NOT need to modify this function.
 *
 * @param data The (null-terminated) ASCII data containing an octal number.
 * @return Returns an unsigned integer number, corresponding to the input data.
 */
static inline unsigned int octal2ui(const char *data)
{
	// Current working value.
	unsigned int value = 0;

	// Length of the input data.
	int len = strlen(data);

	// Starting at i = 1, with a factor of one.
	int i = 1, factor = 1;
	while (i < len) {
		// Extract the current character we're working on (backwards from the end).
		char ch = data[len - i];

		// Add the value of the character, multipled by the factor, to
		// the working value.
		value += factor * (ch - '0');

		// Increment the factor by multiplying it by eight.
		factor *= 8;

		// Increment the current character position.
		i++;
	}

	// Return the current working value.
	return value;
}

// helper function - convert bytes to blocks and round up the integer
static int byte2block(int bytes) {
	int cap = 0;
	int block = 0;
	while (cap < bytes) {
		cap = cap + 512;
		block++;
	}
	return block;
}

// The structure that represents the header block present in
// TAR files.  A header block occurs before every file, this
// this structure must EXACTLY match the layout as described
// in the TAR file format description.
namespace tarfs {
	struct posix_header {
		char name [100]; // name of file entry
		char mode [8];
		char uid [8];
		char gid [8];
		char size [12]; // size of file data (excluding header block)
		char mtime [12];
		char typeflag; // == '5' for directory, '0' for regular file
		char linkname [100];
		char magic [6];
		char version [2];
		char uname [32];
		char gname [32];
		char devmajor [8];
		char devminor [8];
		char prefix [155];
	} __packed;
}

/**
 * Reads the contents of the file into the buffer, from the specified file offset.
 * @param buffer The buffer to read the data into.
 * @param size The size of the buffer, and hence the number of bytes to read.
 * @param off The offset within the file. (byte offset)
 * @return Returns the number of bytes read into the buffer.
 */
int TarFSFile::pread(void* buffer, size_t size, off_t off)
{
	if (off >= this->size()) return 0;

	if (off+size > this->size()) { // if trying to read more bytes than the number of bytes the file contains
		size = this->size() - off;
	}

	int block_size = _owner.block_device().block_size(); // 512 bytes
	int index = 0; // index to the current byte
	int ending_index = size+off; // = total no. of bytes to be looped through
	int curr_block = _file_start_block; // index to the current block
	int buffer_idx = 0; // index to increment the buffer pointer = no. of bytes read

	while (index < ending_index) { // read all required data bytes
		int next_index = index + 512;

		int start_index; // while copying content from temp_buffer into buffer,we have to consider
		int last_index; // which byte to start and which byte to end according to offset and size parameters

		// start_index:
		if (index <= off && off < next_index) { // if offset is within this block
			start_index = off-index;
		} else if (off < index) { // offset has been encountered
			start_index = 0;
		}

		// last_index:
		if (ending_index < next_index) { // finish reading within this block
			last_index = ending_index-index;
		} else {
			last_index = next_index-index; // should read the whole block
		}

		if (next_index <= off) { // if offset is in the future block
			// do nothing
		} else { // copy content from temp_buffer to buffer
			uint8_t *temp_buffer = new uint8_t[block_size];
			_owner.block_device().read_blocks(temp_buffer, curr_block, 1); // read the current block
			for (int i = start_index; i < last_index; i++) {
				*((uint8_t*)buffer+buffer_idx) = temp_buffer[i]; // write data into buffer
				buffer_idx++;
			}
			delete temp_buffer;
		}

		index = next_index;
		curr_block++;
	}

	return buffer_idx;
}

/**
 * Reads all the file headers in the TAR file, and builds an in-memory
 * representation.
 * @return Returns the root TarFSNode that corresponds to the TAR file structure.
 */
TarFSNode* TarFS::build_tree()
{
	// Create the root node.
	TarFSNode *root = new TarFSNode(NULL, "", *this);

	int pos = 0; // index number for header block in tar file

	while (true) {

		// check for the two 0-byte blocks, if exists
		char *last_two = new char[block_device().block_size() * 2];
		block_device().read_blocks(last_two, pos, 2); // read 2 blocks
		bool isLast = true;
		for (int i = 0; i < (block_device().block_size()*2); i++) {
			if (octal2ui(last_two+i) != 0) { // if the byte != 0, they are not the ending blocks
				isLast = false;
				break;
			}
		}
		delete last_two;

		if (isLast) { // if 2 0-bytes blocks have been encountered, return
			return root;
		}

		// header block
		posix_header *header = (struct posix_header *) new char[block_device().block_size()];
		block_device().read_blocks(header, pos, 1);

		// path name
		String header_name_str = String(header->name);
		List<String> header_name_split = header_name_str.split('/', true); // split by slash

		TarFSNode *parent = root;
		for (const String& name : header_name_split) {
			PFSNode *poss_child = parent->get_child(name); // find the child if exists
			if (poss_child != NULL) { // if child exists, point the current parent to that child
				TarFSNode *child_node = static_cast<TarFSNode *>(parent->get_child(name));
				parent = child_node;
			} else { // if child does not exist, create a new child and append to the current parent
				TarFSNode *child_node = new TarFSNode(parent, name, *this);
				child_node->set_block_offset(pos); // updates this node with the offset of the block that contains the header of the file that this node represents
				parent->add_child(name, child_node);
				parent = root; // reset parent back to root for further searching
			}
		}
		pos = pos + 1 + byte2block(octal2ui(header->size)); // set to the position of the header for the next file entry
	}

	// // ---------------- printing the TarFS tree out for checking -------------------
	// for (const auto& child : root->children()) {
	// 	syslog.messagef(LogLevel::DEBUG, "\t\t%s",child.value->name().c_str());
	// 	for (const auto& child_child : child.value->children()) {
	// 		syslog.messagef(LogLevel::DEBUG, "\t\t\t%s",child_child.value->name().c_str());
	// 		for (const auto& child_child_child : child_child.value->children()) {
	// 			syslog.messagef(LogLevel::DEBUG, "\t\t\t\t%s",child_child_child.value->name().c_str());
	// 			for (const auto& child_child_child_child : child_child_child.value->children()) {
	// 				syslog.messagef(LogLevel::DEBUG, "\t\t\t\t\t%s",child_child_child_child.value->name().c_str());
	// 			}
	// 		}
	// 	}
	// }
}

/**
 * Returns the size of this TarFS File
 */
unsigned int TarFSFile::size() const
{
	int file_size_int = octal2ui(_hdr->size);
	return file_size_int;
}

/* --- YOU DO NOT NEED TO CHANGE ANYTHING BELOW THIS LINE --- */

/**
 * Mounts a TARFS filesystem, by pre-building the file system tree in memory.
 * @return Returns the root node of the TARFS filesystem.
 */
PFSNode *TarFS::mount()
{
	// If the root node has not been generated, then build it.
	if (_root_node == NULL) {
		_root_node = build_tree();
	}

	// Return the root node.
	return _root_node;
}

/**
 * Constructs a TarFS File object, given the owning file system and the block
 */
TarFSFile::TarFSFile(TarFS& owner, unsigned int file_header_block)
: _hdr(NULL),
_owner(owner),
_file_start_block(file_header_block),
_cur_pos(0)
{
	// Allocate storage for the header.
	_hdr = (struct posix_header *) new char[_owner.block_device().block_size()];

	// Read the header block into the header structure.
	_owner.block_device().read_blocks(_hdr, _file_start_block, 1);

	// Increment the starting block for file data.
	_file_start_block++;
}

TarFSFile::~TarFSFile()
{
	// Delete the header structure that was allocated in the constructor.
	delete _hdr;
}

/**
 * Releases any resources associated with this file.
 */
void TarFSFile::close()
{
	// Nothing to release.
}

/**
 * Reads the contents of the file into the buffer, from the current file offset.
 * The current file offset is advanced by the number of bytes read.
 * @param buffer The buffer to read the data into.
 * @param size The size of the buffer, and hence the number of bytes to read.
 * @return Returns the number of bytes read into the buffer.
 */
int TarFSFile::read(void* buffer, size_t size)
{
	// Read can be seen as a special case of pread, that uses an internal
	// current position indicator, so just delegate actual processing to
	// pread, and update internal state accordingly.

	// Perform the read from the current file position.
	int rc = pread(buffer, size, _cur_pos);

	// Increment the current file position by the number of bytes that was read.
	// The number of bytes actually read may be less than 'size', so it's important
	// we only advance the current position by the actual number of bytes read.
	_cur_pos += rc;

	// Return the number of bytes read.
	return rc;
}

/**
 * Moves the current file pointer, based on the input arguments.
 * @param offset The offset to move the file pointer either 'to' or 'by', depending
 * on the value of type.
 * @param type The type of movement to make.  An absolute movement moves the
 * current file pointer directly to 'offset'.  A relative movement increments
 * the file pointer by 'offset' amount.
 */
void TarFSFile::seek(off_t offset, SeekType type)
{
	// If this is an absolute seek, then set the current file position
	// to the given offset (subject to the file size).  There should
	// probably be a way to return an error if the offset was out of bounds.
	if (type == File::SeekAbsolute) {
		_cur_pos = offset;
	} else if (type == File::SeekRelative) {
		_cur_pos += offset;
	}
	if (_cur_pos >= size()) {
		_cur_pos = size() - 1;
	}
}

TarFSNode::TarFSNode(TarFSNode *parent, const String& name, TarFS& owner) : PFSNode(parent, owner), _name(name), _size(0), _has_block_offset(false), _block_offset(0)
{
}

TarFSNode::~TarFSNode()
{
}

/**
 * Opens this node for file operations.
 * @return
 */
File* TarFSNode::open()
{
	// This is only a file if it has been associated with a block offset.
	if (!_has_block_offset) {
		return NULL;
	}

	// Create a new file object, with a header from this node's block offset.
	return new TarFSFile((TarFS&) owner(), _block_offset);
}

/**
 * Opens this node for directory operations.
 * @return
 */
Directory* TarFSNode::opendir()
{
	return new TarFSDirectory(*this);
}

/**
 * Attempts to retrieve a child node of the given name.
 * @param name
 * @return
 */
PFSNode* TarFSNode::get_child(const String& name)
{
	TarFSNode *child;

	// Try to find the given child node in the children map, and return
	// NULL if it wasn't found.
	if (!_children.try_get_value(name.get_hash(), child)) {
		return NULL;
	}

	return child;
}

/**
 * Creates a subdirectory in this node.  This is a read-only file-system,
 * and so this routine does not need to be implemented.
 * @param name
 * @return
 */
PFSNode* TarFSNode::mkdir(const String& name)
{
	// DO NOT IMPLEMENT
	return NULL;
}

/**
 * A helper routine that updates this node with the offset of the block
 * that contains the header of the file that this node represents.
 * @param offset The block offset that corresponds to this node.
 */
void TarFSNode::set_block_offset(unsigned int offset)
{
	_has_block_offset = true;
	_block_offset = offset;
}

/**
 * A helper routine that adds a child node to the internal children
 * map of this node.
 * @param name The name of the child node.
 * @param child The actual child node.
 */
void TarFSNode::add_child(const String& name, TarFSNode *child)
{
	_children.add(name.get_hash(), child);
}

TarFSDirectory::TarFSDirectory(TarFSNode& node) : _entries(NULL), _nr_entries(0), _cur_entry(0)
{
	_nr_entries = node.children().count();
	_entries = new DirectoryEntry[_nr_entries];

	int i = 0;
	for (const auto& child : node.children()) {
		_entries[i].name = child.value->name();
		_entries[i++].size = child.value->size();
	}
}

TarFSDirectory::~TarFSDirectory()
{
	delete _entries;
}

bool TarFSDirectory::read_entry(infos::fs::DirectoryEntry& entry)
{
	if (_cur_entry < _nr_entries) {
		entry = _entries[_cur_entry++];
		return true;
	} else {
		return false;
	}
}

void TarFSDirectory::close()
{

}

static Filesystem *tarfs_create(VirtualFilesystem& vfs, Device *dev)
{
	if (!dev->device_class().is(BlockDevice::BlockDeviceClass)) return NULL;
	return new TarFS((BlockDevice &) * dev);
}

RegisterFilesystem(tarfs, tarfs_create);
