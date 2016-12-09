/*
 *	PROGRAM:	JRD Access Method
 *	MODULE:		Adapter.h
 *	DESCRIPTION:	External file adapter definitions
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
 *  The Original Code was created by Dmitriy Starodubov 
 *   <dmitry.starodubov (at) red-soft.biz> for Red Soft Corporation.
 *                                                                    
 *  Copyright (c) 2007 Red Soft Corporation <info (at) red-soft.biz>
 *  and all contributors signed below.
 *
 *  All Rights Reserved.
 *  Contributor(s): ______________________________________.
 *
 */

#ifndef JRD_ADAPTER_H
#define JRD_ADAPTER_H

//#include <stdio.h>
//#include "../common/classes/array.h"
//#include "../jrd/ntrace.h"
//#include "../../common/classes/rwlock.h"
//#include "../../common/classes/locks.h"
#include "../common/classes/MetaName.h"
//#include "../jrd/align.h"

static const char* ext_default_adapter	= "DEFAULT";
static const char* ext_trace_adapter	= "FBTRACE";

enum adapter_types {
	ext_default	= 0,
	ext_fbtrace	= 1,
};

class AdapterField {
public:
	USHORT		fld_id;
	const TEXT*	fld_name;
	USHORT		fld_dtype;
	USHORT		fld_length;
	SSHORT		fld_scale;
	SSHORT		fld_sub_type;
	USHORT		fld_precision;
	USHORT		fld_character_length;
	bool		fld_not_nullable;
};

namespace Jrd {
	class jrd_rel;
	class Record;
	class blb;
}

class BaseAdapter
{
public:
	virtual size_t read_record(const class Jrd::jrd_rel* relation, class Jrd::Record* record) = 0;
	virtual int write_record(const class Jrd::Record* record) = 0;
	BaseAdapter() {};
	virtual ~BaseAdapter() {};
	virtual void set_file_handle(FILE* file_handle) = 0;
	virtual void get_blob_data(class Jrd::blb* blob) = 0;
	virtual const AdapterField* get_adapter_fields() = 0;
	USHORT adapter_type;
	USHORT header_length;
protected:
	virtual void check_file_format() = 0;
	UCHAR fmt_version;		/* External file format version*/
};

class DefaultAdapter : public BaseAdapter {
public:
	DefaultAdapter(const char* filename)
	{
		adapter_type = ext_default;
		header_length = 0;
	}
	
	~DefaultAdapter() {};
	
	virtual void set_file_handle(FILE* file_handle)
	{
		fb_assert(file_handle);
		file = file_handle;
	}
	
	virtual size_t read_record(const class Jrd::jrd_rel* relation, class Jrd::Record* record)
	{
		return 0;
	}
	
	virtual int write_record(const class Jrd::Record* record)
	{
		return 0;
	}

	virtual const AdapterField* get_adapter_fields()
	{
		return NULL;
	};

	virtual void get_blob_data(class Jrd::blb* blob) {};
private:
	FILE* file;
	virtual void check_file_format() {};
};

#endif // JRD_ADAPTER_H