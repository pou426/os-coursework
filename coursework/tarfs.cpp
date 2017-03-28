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
		char name [100]; // file name of the file, with directory names preceding the file name, separated by slashes
		char mode [8]; // 9 bits = file permissions; 3 bits = Set UID, Set GID, Save Text modes
		char uid [8]; // User ID of file owner
		char gid [8]; // Group ID of file owner
		char size [12]; // size of file in bytes
		char mtime [12]; // data modification time of the file at the time it was archived. it is the ASCII representation of the octal value of the last time the file's contents were modified, represented as an integer number of seconds since January 1, 1970, 00:00
		char chksum [8]; // ASCII representation of the octal value of the simple sum of all bytes in the header block
		char typeflag; // type of file archieved, 0 = reg file, 5 = directory
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
 * @param off The offset within the file.
 * @return Returns the number of bytes read into the buffer.
 */
int TarFSFile::pread(void* buffer, size_t size, off_t off)
{
	if (off >= this->size()) return 0;

	// TO BE FILLED IN

	// buffer is a pointer to the buffer that should receive the data.
	// size is the amount of data to read from the file.
	// off is the zero-based offset within the file to start reading from.

	// NOTE: this function is used for reading the data associated with a file. it
	// reads from a particular offset in the file, for a particular length into the supplied
	// buffer.
	return 0;
}

// --------------------------------------------------------------------------------------------
//
// 																	TarFS::build_tree()
//
// --------------------------------------------------------------------------------------------
/**
 * Reads all the file headers in the TAR file, and builds an in-memory
 * representation.
 * @return Returns the root TarFSNode that corresponds to the TAR file structure.
 */
TarFSNode* TarFS::build_tree()
{
	// Create the root node.
	TarFSNode *root = new TarFSNode(NULL, "", *this);

	// TO BE FILLED IN

	// You must read the TAR file, and build a tree of TarFSNodes that represents each file present in the archive.

	// size_t nr_blocks = block_device().block_count(); // nr_blocks = 400
	// syslog.messagef(LogLevel::DEBUG, "--------------Block Device nr_blocks=%lu", nr_blocks);
	// int block_size = block_device().block_size(); // block_size = 512
	// syslog.messagef(LogLevel::DEBUG, "--------------Block Device block_size=%lu", block_size);
	//
	// uint8_t *buffer = new uint8_t[block_size*nr_blocks];
	// block_device().read_blocks(buffer, 0, nr_blocks);
	// delete buffer;

	int pos = 0; // index number for block in tar file
	bool done = false; // still got file entries to read
	while (!done) {
		// check for the two 0-byte blocks, if exists
		char *last_two = new char[block_device().block_size() * 2];
		block_device().read_blocks(last_two, pos, 2);
		bool isLast = true;
		for (int i = 0; i < block_device().block_size() * 2; i++) {
			if (octal2ui(last_two+i) != 0) {
				isLast = false;
				break;
			}
		}
		if (isLast) {
			// syslog.message(LogLevel::DEBUG, "two 0-byte blocks encountered. done");
			done = true;
		} else {
			// read header block
			posix_header *header = (struct posix_header *) new char[block_device().block_size()];
			block_device().read_blocks(header, pos, 1);
			// syslog.messagef(LogLevel::DEBUG, "name=%s, size=%lu, pos=%lu", header->name, byte2block(octal2ui(header->size)), pos);

			// path name
			String header_name_str = String(header->name);
			List<String> header_name_split = header_name_str.split('/', true);

			TarFSNode *parent = root;
			for (const String& name : header_name_split) {
				TarFSNode *child;
				if (parent->children().try_get_value(name.get_hash(), child)) {
					// syslog.message(LogLevel::DEBUG, "child node has been found");
					parent = child;
				} else {
					// syslog.message(LogLevel::DEBUG, "child node not found");
					TarFSNode *child_node = new TarFSNode(parent, name, *this);
					child_node->set_block_offset(pos);
					parent->add_child(name, child_node);
					parent = child_node;
				}
			}
			pos = pos + 1 + byte2block(octal2ui(header->size));
		}

	}


	// // ------------------------- this prints out all directories and files ----------------------------------
	// int start = 0;
	// posix_header *header = (struct posix_header *) new char[block_device().block_size()];
	// block_device().read_blocks(header, start, 1);
	// int header_size_bytes = octal2ui(header->size);
	// int header_size_block = byte2block(header_size_bytes);
	// syslog.messagef(LogLevel::DEBUG, "name=%s, size=%lu blocks, pos=%lu", header->name, header_size_block, start);

	// // ---------------- typeflag ---------------
	// bool isDir = false;
	// char header_typeflag = header->typeflag;
	// syslog.messagef(LogLevel::DEBUG, "typeflag=%c", header_typeflag);
	// if (header_typeflag == '5') {
	// 	isDir = true;
	// 	syslog.message(LogLevel::DEBUG, "This is a directory");
	// }
	//
	// // ---------------- path name ---------------
	// char *header_name = header->name;
	// String header_name_str = String(header_name);
	// List<String> header_name_split = header_name_str.split('/',true);
	// int header_name_split_len = header_name_split.count();
	// for (const String &s : header_name_split) {
	// 	syslog.messagef(LogLevel::DEBUG, "s=%s", s.c_str());
	// }
	//
	// TarFSNode *parent = root;
	// for (const String& name : header_name_split) {
	// 	TarFSNode *child;
	// 	if (parent->children().try_get_value(name.get_hash(), child)) { // child node not found
	// 		syslog.message(LogLevel::DEBUG, "child node is found!!!!!!!!!!!!!");
	// 		parent = child; // set parent to child
	// 	} else {
	// 		syslog.message(LogLevel::DEBUG, "child node is not found!!!!!!!!!!!!!");
	// 		TarFSNode *child_node = new TarFSNode(parent, name, *this);
	// 		child_node->set_block_offset(start);
	// 		parent->add_child(name, child_node);
	// 		parent = child_node;
	// 	}
	// }
	//
	//
	// bool done = false;
	// while (!done) {
	// 	start = start + 1 + header_size_block;
	// 	char *last_two = new char[block_device().block_size() * 2];
	// 	block_device().read_blocks(last_two, start, 2);
	// 	bool isLast = true;
	// 	for (int i = 0; i < 1024; i++) {
	// 		if (octal2ui(last_two+i) != 0) {
	// 			isLast = false;
	// 		}
	// 	}
	// 	if (isLast) {
	// 		done = true;
	// 		break;
	// 	} else {
	// 		header = (struct posix_header *) new char[block_device().block_size()];
	// 		block_device().read_blocks(header, start, 1);
	// 		header_size_bytes = octal2ui(header->size);
	// 		header_size_block = byte2block(header_size_bytes);
	// 		syslog.messagef(LogLevel::DEBUG, "name=%s, size=%lu blocks, pos=%lu", header->name, header_size_block, start);
	// 		// ------------------ typeflag -----------------
	// 		isDir = false;
	// 		char header_typeflag = header->typeflag;
	// 		syslog.messagef(LogLevel::DEBUG, "typeflag=%c", header_typeflag);
	// 		if (header_typeflag == '5') {
	// 			isDir = true;
	// 			syslog.message(LogLevel::DEBUG, "This is a directory");
	// 		}
	// 		// ---------------- path name ---------------
	// 		header_name = header->name;
	// 		header_name_str = String(header_name);
	// 		List<String> header_name_split = header_name_str.split('/',true);
	// 		header_name_split_len = header_name_split.count();
	// 		for (const String &s : header_name_split) {
	// 			syslog.messagef(LogLevel::DEBUG, "s=%s", s.c_str());
	// 		}
	//
	// 		parent = root;
	// 		for (const String& name : header_name_split) {
	// 			TarFSNode *child;
	// 			if (parent->children().try_get_value(name.get_hash(), child)) { // child node not found
	// 				syslog.message(LogLevel::DEBUG, "child node is found");
	// 				parent = child; // set parent to child
	// 			} else {
	// 				syslog.message(LogLevel::DEBUG, "child node is not found!!!!!!!!!!!!!");
	// 				TarFSNode *child_node = new TarFSNode(parent, name, *this);
	// 				child_node->set_block_offset(start);
	// 				parent->add_child(name, child_node);
	// 				parent = child_node;
	// 			}
	// 		}
	// 	}
	// }




	// NOTE: this function is used during mount time to build the tree representation of the file
	// system. Normally, a file system wouldn't read its entire directory layout into memory, build_tree
	// for simplicity this is what should be done here
	//
	// called at file-system mount time to build the in-memory representation of the file-system tree.
	// Need to iterate over the TAR file, and build a tere of TarFSNodes

	return root;
}

/**
 * Returns the size of this TarFS File
 */
unsigned int TarFSFile::size() const
{
	// TO BE FILLED IN

	// NOTE: this is a ery imple method (a one liner!) that returns the size of the file
	// representated by the TarFSFile object. you can get the size by interrogating the header
	// block of the file
	return 0;
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
