
#include "app_msc_file.hpp"

//non-trivial constructor
MSC_File::MSC_File(	std::span<uint8_t, std::dynamic_extent> _file_contents,
					std::span<uint8_t, std::dynamic_extent> _file_name,
					bool _readonly,
					Mutex* _file_mutex):
	file_contents(_file_contents), readonly(_readonly), file_mutex(_file_mutex)
{
	//clear filename to NULL characters (0)
	file_name.fill(0);

	//compute the file name size
	filename_size = min(_file_name.size(), file_name.size());

	//copy the filename into the filename field
	std::copy(	_file_name.begin(),
				_file_name.begin() + filename_size,
				file_name.begin() );
}

//file validity checking --> check span to see if it was nontrivially initialized
bool MSC_File::is_valid() { return !(file_contents.empty() || (file_contents.data() == nullptr)); }

//comparison operator override
bool MSC_File::operator==(const MSC_File& other) const {
	if(this->file_contents.size() != other.file_contents.size()) return false;
	if(this->file_contents.data() != other.file_contents.data()) return false;
	return true;
}

//########## READ FUNCTION ##########
size_t MSC_File::read(size_t byte_offset, std::span<uint8_t, std::dynamic_extent> copy_dest) {
	//sanity check our starting address; return if outta range
	if(byte_offset >= file_contents.size()) return 0;

	//we'll be reading some bytes, try to acquire the mutex if it exists
	if(file_mutex)
		if(!file_mutex->AVAILABLE(true)) return 0;	//mutex acquisition failed, abort copy

	//perform our actual copy
	size_t req_size = copy_dest.size();
	size_t rem_size = file_contents.size() - byte_offset;
	size_t copy_size = min(req_size, rem_size);
	std::copy(file_contents.begin() + byte_offset, file_contents.begin() + byte_offset + copy_size, copy_dest.begin());

	//and if we had a mutex, unlock the mutex
	if(file_mutex) file_mutex->UNLOCK();

	//return number of bytes we copied
	return copy_size;
}

size_t MSC_File::write(size_t byte_offset, std::span<uint8_t, std::dynamic_extent> copy_src) {
	//sanity check our starting address; copy no bytes if outta range
	//and if the file is readonly, copy no bytes
	if(byte_offset >= file_contents.size()) return 0;
	if(readonly) return 0;

	//we'll be writing some bytes, try to acquire the mutex if it exists
	if(file_mutex)
		if(!file_mutex->AVAILABLE(true)) return 0;	//mutex acquisition failed, abort copy

	//perform our actual copy
	size_t req_size = copy_src.size();
	size_t rem_size = file_contents.size() - byte_offset;
	size_t copy_size = min(req_size, rem_size);
	std::copy(copy_src.begin(), copy_src.begin() + copy_size, file_contents.begin() + byte_offset);

	//and if we had a mutex, unlock the mutex
	if(file_mutex) file_mutex->UNLOCK();

	//return number of bytes we copied
	return copy_size;
}

//============== PRIVATE FUNCTION =============
void MSC_File::mk_8p3() {
	//super short lookup table to convert hex values to chars
	static constexpr char hex[] = "0123456789ABCDEF";

	//get the pointer to us as a 32-bit value
	uint32_t addr = reinterpret_cast<uint32_t>(this);

	//convert the name, then hard-code the extension
	for (int i = 0; i < 8; i++)	short_name.name[i] = hex[(addr >> (28 - 4 * i)) & 0xF];
	short_name.ext = {'F','I','L'};

	//finally compute the checksum
	//start by concatenating the names
	std::array<uint8_t, 11> name_cat = {0};
	std::copy(short_name.name.begin(), short_name.name.end(), name_cat.begin());
	std::copy(short_name.ext.begin(), short_name.ext.end(), name_cat.begin() + sizeof(short_name.name));

	//actually compute the checksum
	//basically sum = (rotate_right(sum) + char) % 256
	uint8_t sum = 0;
	for (int i = 0; i < 11; i++) sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + name_cat[i];

	short_name.checksum = sum;
}
