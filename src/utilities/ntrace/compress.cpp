/*
*	PROGRAM:	SQL Trace plugin
*	MODULE:		compress.cpp
*	DESCRIPTION:	GZIP compression
*
*  The contents of this file are subject to the Initial
*  Developer's Public License Version 1.0 (the "License");
*  you may not use this file except in compliance with the
*  License. You may obtain a copy of the License at
*  http://www.ibphoenix.com/main.nfs?a=ibphoenix&page=ibp_idpl.
*
*  Software distributed under the License is distributed AS IS,
*  WITHOUT WARRANTY OF ANY KIND, either express or implied.
*  See the License for the specific language governing rights
*  and limitations under the License.
*
*  The Original Code was created by Dmitry Starodubov
*  for the Firebird Open Source RDBMS project
*
*  Copyright (c) 2016 Dmitry Starodubov <dmitry.starodubov@red-soft.ru>
*  and all contributors signed below.
*
*  All Rights Reserved.
*  Contributor(s): ______________________________________.
*
*/

#include "firebird.h"
#include "../common/classes/init.h"
#include "../common/os/path_utils.h"
#include "../common/os/os_utils.h"

#include <zlib.h>
#include "../common/os/mod_loader.h"
#define COMP_BUF_SIZE 8192

namespace {
	class ZLib
	{
	public:
		explicit ZLib(Firebird::MemoryPool&)
		{
#ifdef WIN_NT
			const char* name = "zlib1.dll";
#else
			const char* name = "libz." SHRLIB_EXT ".1";
#endif
			z.reset(ModuleLoader::fixAndLoadModule(name));
			if (z)
				symbols();
		}

		gzFile ZEXPORT(*gzopen)(const char *path, const char *mode);
		int ZEXPORT(*gzwrite)(gzFile file, voidpc buf, unsigned len);
		int ZEXPORT(*gzclose)(gzFile file);

		operator bool() { return z.hasData(); }
		bool operator!() { return !z.hasData(); }

	private:
		Firebird::AutoPtr<ModuleLoader::Module> z;

		void symbols()
		{
#define FB_ZSYMB(A) z->findSymbol(STRINGIZE(A), A); if (!A) { z.reset(NULL); return; }
			FB_ZSYMB(gzopen)
			FB_ZSYMB(gzwrite)
			FB_ZSYMB(gzclose)
#undef FB_ZSYMB
		}
	};

	Firebird::InitInstance<ZLib> zlib;
}

THREAD_ENTRY_DECLARE compress_file(THREAD_ENTRY_PARAM arg)
{
	char* source_file = (char*) arg;

	char buffer[COMP_BUF_SIZE];

	Firebird::PathName dst_filename(source_file);
	dst_filename += ".gz";

	Firebird::PathName path, filename;
	PathUtils::splitLastComponent(path, filename, source_file);

	try
	{
		gzFile gzip_file = NULL;
		gzip_file = zlib().gzopen(dst_filename.c_str(), "wb");

		if (!gzip_file)
		{
			gds__log("PluginLogWriter: operation \"open\" failed on gzip file \"%s\"", dst_filename.c_str());
			delete[] source_file;
			return 0;
		}

		const int fd = os_utils::open(source_file,
#ifdef WIN_NT
			O_RDONLY | O_SEQUENTIAL | O_BINARY);
#else
			O_RDONLY);
#endif // WIN_NT
		if (fd < 0)
		{
			zlib().gzclose(gzip_file);
			gds__log("PluginLogWriter: operation \"open\" failed on file \"%s\". Error code: %d", source_file, errno);
			delete[] source_file;
			return 0;
		}

		size_t bytes_readed;

		do
		{
			bytes_readed = ::read(fd, buffer, COMP_BUF_SIZE);
			if (bytes_readed > 0)
			{
				if (zlib().gzwrite(gzip_file, buffer, bytes_readed) == 0)
				{
					close(fd);
					zlib().gzclose(gzip_file);
					gds__log("PluginLogWriter: operation \"write\" failed on gzip file \"%s\"", dst_filename.c_str());
					delete[] source_file;
					return 0;
				}
			}
			else if (bytes_readed < 0)
			{
				close(fd);
				zlib().gzclose(gzip_file);
				gds__log("PluginLogWriter: operation \"read\" failed on file \"%s\". Error code: %d", source_file, errno);
				delete[] source_file;
				return 0;
			}

		} while (bytes_readed > 0);

		if (zlib().gzclose(gzip_file) != Z_OK)
			gds__log("PluginLogWriter: operation \"close\" failed on gzip file \"%s\"", dst_filename.c_str());
		if (close(fd) < 0)
			gds__log("PluginLogWriter: operation \"close\" failed on file \"%s\". Error code: %d", source_file, errno);
	}
	catch (Firebird::Exception&)
	{
	}

	unlink(source_file);
	delete[] source_file;
}
