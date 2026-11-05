// SPDX-License-Identifier: GPL-2.0-or-later
// The DOS drive whose files the host keeps (tools/dosbox_pure_neo98.h,
// Neo98DbpFiles). tools/build_dosbox_pure.py copies this file into the clone
// of the core as neo98_files.inc; the patched dosbox_pure_libretro.cpp
// includes it.
// The write layer of a game machine is one thing to Windows and DOS programs
// alike: a file a Windows program deleted is gone
// for a DOS program at once, and the other way round, because both ask the
// same provider of the host. This drive therefore keeps no directory cache
// and no file contents. A search takes the names of its directory when it
// starts, so "DEL *.*" sees every file once.
#include "src/dos/drives.h"
#include <string>
#include <vector>

namespace
{
// The codes the host answers with, as DOS names them.
Bit16u Neo98DosError(unsigned code)
{
	switch (code)
	{
		case 2: return DOSERR_FILE_NOT_FOUND;
		case 3: return DOSERR_PATH_NOT_FOUND;
		case 80: case 183: return DOSERR_FILE_ALREADY_EXISTS;
		default: return DOSERR_ACCESS_DENIED;
	}
}

struct Neo98File : public DOS_File
{
	Neo98File(const Neo98DbpFiles& _files, const char* _path, const Neo98DbpEntry& entry, Bit32u _flags, const char* dos_name) : files(_files), path(_path), pos(0)
	{
		date = (Bit16u)entry.date;
		time = (Bit16u)entry.time;
		attr = (Bit16u)entry.attributes;
		flags = _flags;
		SetName(dos_name);
		open = true;
		files.retain(files.owner, path.c_str());
	}
	~Neo98File() { if (open) { files.release(files.owner, path.c_str()); open = false; } }

	virtual bool Read(Bit8u* data, Bit16u* size)
	{
		if (!OPEN_IS_READING(flags)) return FALSE_SET_DOSERR(ACCESS_DENIED);
		unsigned done = 0;
		if (unsigned e = files.read(files.owner, path.c_str(), pos, data, *size, done)) { dos.errorcode = Neo98DosError(e); return false; }
		*size = (Bit16u)done;
		pos += done;
		return true;
	}
	virtual bool Write(Bit8u* data, Bit16u* size)
	{
		if (!OPEN_IS_WRITING(flags)) return FALSE_SET_DOSERR(ACCESS_DENIED);
		unsigned done = 0, e;
		// A write of no bytes cuts or extends the file at the position.
		if (!*size) e = files.resize(files.owner, path.c_str(), pos);
		else e = files.write(files.owner, path.c_str(), pos, data, *size, done);
		if (e && !done) { dos.errorcode = Neo98DosError(e); return false; }
		*size = (Bit16u)done;
		pos += done;
		return true;
	}
	virtual bool Seek(Bit32u* at, Bit32u type)
	{
		Bit32s to = 0;
		switch (type)
		{
			case DOS_SEEK_SET: to = (Bit32s)*at; break;
			case DOS_SEEK_CUR: to = (Bit32s)*at + (Bit32s)pos; break;
			case DOS_SEEK_END:
			{
				Neo98DbpEntry entry;
				if (!files.find(files.owner, path.c_str(), entry)) return FALSE_SET_DOSERR(ACCESS_DENIED);
				to = (Bit32s)entry.size + (Bit32s)*at;
				break;
			}
			default: return FALSE_SET_DOSERR(FUNCTION_NUMBER_INVALID);
		}
		if (to < 0) to = 0;
		*at = pos = (Bit32u)to;
		return true;
	}
	virtual bool Close()
	{
		if (refCtr == 1 && open)
		{
			if (newtime) { files.set_time(files.owner, path.c_str(), date, time); newtime = false; }
			files.release(files.owner, path.c_str());
			open = false;
		}
		return true;
	}
	virtual Bit16u GetInformation(void) { return 0; }

	const Neo98DbpFiles& files;
	std::string path;
	Bit32u pos;
};

struct neo98Drive : public DOS_Drive
{
	neo98Drive(const Neo98DbpFiles& _files, char letter) : files(_files), next_search(0)
	{
		label.SetLabel(files.label[0] ? files.label : "NEO98", false, true);
		snprintf(info, sizeof(info), "NEO98 %c:", letter);
	}
	virtual ~neo98Drive() { ForceCloseAll(); }

	virtual bool FileOpen(DOS_File** file, char* name, Bit32u flags)
	{
		if (!OPEN_CHECK_ACCESS_CODE(flags)) return FALSE_SET_DOSERR(ACCESS_CODE_INVALID);
		DOSPATH_REMOVE_ENDINGDOTS_KEEP(name);
		Neo98DbpEntry entry;
		if (!files.find(files.owner, name, entry) || (entry.attributes & DOS_ATTR_DIRECTORY)) return FALSE_SET_DOSERR(FILE_NOT_FOUND);
		if (OPEN_IS_WRITING(flags) && (entry.attributes & DOS_ATTR_READ_ONLY)) return FALSE_SET_DOSERR(ACCESS_DENIED);
		*file = new Neo98File(files, name, entry, flags, name_org);
		return true;
	}
	virtual bool FileCreate(DOS_File** file, char* path, Bit16u attributes)
	{
		DOSPATH_REMOVE_ENDINGDOTS_KEEP(path);
		if ((attributes & DOS_ATTR_DIRECTORY) || !*path) return FALSE_SET_DOSERR(ACCESS_DENIED);
		Neo98DbpEntry entry;
		unsigned e;
		if (files.find(files.owner, path, entry))
		{
			if (entry.attributes & (DOS_ATTR_DIRECTORY | DOS_ATTR_READ_ONLY)) return FALSE_SET_DOSERR(ACCESS_DENIED);
			e = files.resize(files.owner, path, 0);
		}
		else e = files.create(files.owner, path, (attributes & 0x27) | DOS_ATTR_ARCHIVE, false);
		if (e) { dos.errorcode = Neo98DosError(e); return false; }
		if (!files.find(files.owner, path, entry)) return FALSE_SET_DOSERR(ACCESS_DENIED);
		*file = new Neo98File(files, path, entry, OPEN_READWRITE, path_org);
		return true;
	}
	virtual bool FileUnlink(char* path)
	{
		DOSPATH_REMOVE_ENDINGDOTS(path);
		Neo98DbpEntry entry;
		if (!files.find(files.owner, path, entry) || (entry.attributes & DOS_ATTR_DIRECTORY)) return FALSE_SET_DOSERR(FILE_NOT_FOUND);
		if (entry.attributes & DOS_ATTR_READ_ONLY) return FALSE_SET_DOSERR(ACCESS_DENIED);
		if (unsigned e = files.remove(files.owner, path, false)) { dos.errorcode = Neo98DosError(e); return false; }
		return true;
	}
	virtual bool RemoveDir(char* path)
	{
		DOSPATH_REMOVE_ENDINGDOTS(path);
		Neo98DbpEntry entry;
		if (!*path || !files.find(files.owner, path, entry) || !(entry.attributes & DOS_ATTR_DIRECTORY)) return FALSE_SET_DOSERR(PATH_NOT_FOUND);
		if (unsigned e = files.remove(files.owner, path, true)) { dos.errorcode = Neo98DosError(e); return false; }
		return true;
	}
	virtual bool MakeDir(char* path)
	{
		DOSPATH_REMOVE_ENDINGDOTS(path);
		if (!*path) return FALSE_SET_DOSERR(ACCESS_DENIED);
		if (unsigned e = files.create(files.owner, path, DOS_ATTR_DIRECTORY, true)) { dos.errorcode = (e == 183 ? DOSERR_ACCESS_DENIED : Neo98DosError(e)); return false; }
		return true;
	}
	virtual bool TestDir(char* path)
	{
		DOSPATH_REMOVE_ENDINGDOTS(path);
		Neo98DbpEntry entry;
		return !*path || (files.find(files.owner, path, entry) && (entry.attributes & DOS_ATTR_DIRECTORY));
	}
	virtual bool FindFirst(char* path, DOS_DTA& dta, bool fcb_findfirst)
	{
		DOSPATH_REMOVE_ENDINGDOTS(path);
		if (!TestDir(path)) return FALSE_SET_DOSERR(PATH_NOT_FOUND);
		// A search that is never read to its end keeps its slot until the
		// slots have gone round once.
		const Bit16u id = next_search;
		next_search = (Bit16u)((next_search + 1) % SearchSlots);
		Search& s = searches[id];
		s.entries.clear();
		s.index = 0;
		s.root = !*path;
		s.active = true;
		Neo98DbpEntry entry;
		for (unsigned i = 0; files.child(files.owner, path, i, entry); i++)
			if (entry.name[0]) s.entries.push_back(entry);
		if (!s.root)
		{
			Neo98DbpEntry self;
			if (files.find(files.owner, path, self)) { s.date = self.date; s.time = self.time; }
		}
		dta.SetDirID(id);
		if (DriveFindDriveVolume(this, path, dta, fcb_findfirst)) return true;
		return FindNext(dta);
	}
	virtual bool FindNext(DOS_DTA& dta)
	{
		if (dta.GetDirID() >= SearchSlots) return FALSE_SET_DOSERR(ACCESS_DENIED);
		Search& s = searches[dta.GetDirID()];
		if (!s.active) return FALSE_SET_DOSERR(NO_MORE_FILES);
		Bit8u attr; char pattern[DOS_NAMELENGTH_ASCII];
		dta.GetSearchParams(attr, pattern);
		while (s.index < 2)
		{
			const char* dotted = (s.index++ ? ".." : ".");
			if (s.root || !DTA_PATTERN_MATCH(dotted, pattern) || !(attr & DOS_ATTR_DIRECTORY)) continue;
			dta.SetResult(dotted, 0, (Bit16u)s.date, (Bit16u)s.time, DOS_ATTR_DIRECTORY);
			return true;
		}
		while (s.index - 2 < s.entries.size())
		{
			const Neo98DbpEntry& e = s.entries[s.index++ - 2];
			if (!DTA_PATTERN_MATCH(e.name, pattern)) continue;
			if (~attr & (Bit8u)e.attributes & (DOS_ATTR_DIRECTORY | DOS_ATTR_HIDDEN | DOS_ATTR_SYSTEM)) continue;
			dta.SetResult(e.name, (e.attributes & DOS_ATTR_DIRECTORY) ? 0 : e.size, (Bit16u)e.date, (Bit16u)e.time, (Bit8u)e.attributes);
			return true;
		}
		s.active = false;
		s.entries.clear();
		return FALSE_SET_DOSERR(NO_MORE_FILES);
	}
	virtual bool GetFileAttr(char* name, Bit16u* attr)
	{
		DOSPATH_REMOVE_ENDINGDOTS(name);
		if (!*name) { *attr = DOS_ATTR_DIRECTORY; return true; }
		Neo98DbpEntry entry;
		if (!files.find(files.owner, name, entry)) return FALSE_SET_DOSERR(FILE_NOT_FOUND);
		*attr = (Bit16u)entry.attributes;
		return true;
	}
	virtual bool Rename(char* oldpath, char* newpath)
	{
		DOSPATH_REMOVE_ENDINGDOTS(oldpath);
		DOSPATH_REMOVE_ENDINGDOTS(newpath);
		Neo98DbpEntry entry;
		if (!files.find(files.owner, oldpath, entry)) return FALSE_SET_DOSERR(FILE_NOT_FOUND);
		if (unsigned e = files.rename(files.owner, oldpath, newpath)) { dos.errorcode = Neo98DosError(e); return false; }
		return true;
	}
	virtual bool AllocationInfo(Bit16u* _bytes_sector, Bit8u* _sectors_cluster, Bit16u* _total_clusters, Bit16u* _free_clusters)
	{
		// What a DOS program can count: at most 2 GiB in clusters of 32 KiB.
		unsigned long long total = 0, available = 0;
		files.space(files.owner, total, available);
		const unsigned long long cap = 0x7fff0000ull, cluster = 512 * 64;
		if (total > cap) total = cap;
		if (available > total) available = total;
		*_bytes_sector = 512;
		*_sectors_cluster = 64;
		*_total_clusters = (Bit16u)(total / cluster);
		*_free_clusters = (Bit16u)(available / cluster);
		return true;
	}
	virtual bool FileExists(const char* name)
	{
		DOSPATH_REMOVE_ENDINGDOTS(name);
		Neo98DbpEntry entry;
		return files.find(files.owner, name, entry) && !(entry.attributes & DOS_ATTR_DIRECTORY);
	}
	virtual bool FileStat(const char* name, FileStat_Block* const stat_block)
	{
		DOSPATH_REMOVE_ENDINGDOTS(name);
		Neo98DbpEntry entry;
		if (!files.find(files.owner, name, entry)) return false;
		stat_block->attr = (Bit16u)entry.attributes;
		stat_block->size = (entry.attributes & DOS_ATTR_DIRECTORY) ? 0 : entry.size;
		stat_block->date = (Bit16u)entry.date;
		stat_block->time = (Bit16u)entry.time;
		return true;
	}
	virtual Bit8u GetMediaByte(void) { return 0xF8; }
	virtual bool isRemote(void) { return false; }
	virtual bool isRemovable(void) { return false; }
	virtual Bits UnMount(void) { delete this; return 0; }

	enum { SearchSlots = 512 };
	struct Search
	{
		std::vector<Neo98DbpEntry> entries;
		size_t index;
		unsigned date, time;
		bool root, active;
		Search() : index(0), date(0), time(0), root(false), active(false) {}
	};
	const Neo98DbpFiles& files;
	Search searches[SearchSlots];
	Bit16u next_search;
};
}

// NEO98: what a saved state holds of the drives the host keeps. The files are the host's, and the state has a copy of
// them of its own; open files come back through DBPSerialize_Files, which
// opens them again by name and puts them at their place. What is left are the
// searches: a program between FindFirst and FindNext holds the number of its
// search in its DTA, and the names that search took live here.
#include <dbp_serialize.h>
void DBPSerialize_Neo98(DBPArchive& ar)
{
	if (ar.mode == DBPArchive::MODE_ZERO) return;
	if (ar.mode == DBPArchive::MODE_MAXSIZE) { ar.SerializeBytes(NULL, 1 << 20); return; }
	for (Bit8u letter = 0; letter != DOS_DRIVES; letter++)
	{
		neo98Drive* drive = (Drives[letter] ? dynamic_cast<neo98Drive*>(Drives[letter]) : NULL);
		Bit16u count = 0, next = (drive ? drive->next_search : (Bit16u)0);
		if (drive && ar.mode != DBPArchive::MODE_LOAD)
			for (unsigned i = 0; i != neo98Drive::SearchSlots; i++)
				if (drive->searches[i].active) count++;
		ar << count << next;
		if (drive && ar.mode == DBPArchive::MODE_LOAD)
		{
			for (unsigned i = 0; i != neo98Drive::SearchSlots; i++) { drive->searches[i].active = false; drive->searches[i].entries.clear(); }
			drive->next_search = (Bit16u)(next % neo98Drive::SearchSlots);
		}
		for (Bit16u id = (Bit16u)-1; count--;)
		{
			Bit32u index = 0, date = 0, time = 0, entries = 0; Bit8u root = 0;
			if (ar.mode != DBPArchive::MODE_LOAD)
			{
				while (!drive->searches[++id].active) { }
				const neo98Drive::Search& s = drive->searches[id];
				index = (Bit32u)s.index; date = s.date; time = s.time; root = (s.root ? 1 : 0); entries = (Bit32u)s.entries.size();
			}
			ar << id << index << date << time << root << entries;
			if (ar.mode == DBPArchive::MODE_LOAD)
			{
				if (entries > 0x100000) { ar.had_error = DBPArchive::ERR_LAYOUT; return; }
				if (!drive || id >= neo98Drive::SearchSlots)
				{
					ar.Discard(entries * sizeof(Neo98DbpEntry));
					ar.warnings |= DBPArchive::WARN_WRONGDRIVES;
					continue;
				}
				neo98Drive::Search& s = drive->searches[id];
				s.index = index; s.date = date; s.time = time; s.root = (root != 0); s.active = true;
				s.entries.resize(entries);
				if (entries) ar.SerializeBytes(&s.entries[0], entries * sizeof(Neo98DbpEntry));
			}
			else if (entries) ar.SerializeBytes(&drive->searches[id].entries[0], entries * sizeof(Neo98DbpEntry));
			if (ar.had_error) return;
		}
	}
}
