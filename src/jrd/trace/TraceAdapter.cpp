/*
 *	PROGRAM:	JRD Access Method
 *	MODULE:		TraceAdapter.cpp
 *	DESCRIPTION:	Adapter for binary trace log
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

#include "firebird.h"
#include <string.h>
#include <math.h>
#include <errno.h>
#include "../jrd/Adapter.h"
#include "../jrd/trace/TraceAdapter.h"
#include "../jrd/ibase.h"
#include "../jrd/req.h"
#include "../jrd/blb.h"
#include "../jrd/svc.h"
#include "../utilities/ntrace/os/FileObject.h"
#include "../../common/classes/ClumpletReader.h"
#include "gen/iberror.h"
#include "../jrd/err_proto.h"
#include "../utilities/ntrace/LogPacketBuilder.h"
//#include "../auth/trusted/AuthFactor.h"
#include "../jrd/obj.h"

#if defined _MSC_VER && _MSC_VER < 1400
// NS: in VS2003 these only work with static CRT
extern "C" {
int __cdecl _fseeki64(FILE*, __int64, int);
int __cdecl _ftelli64(FILE*);
}
#endif

#ifdef WIN_NT
#define FSEEK _fseeki64
#define FTELL _ftelli64
#else
#ifdef __LSB_VERSION__
#define FSEEK fseeko64
#define FTELL ftello64
#else
#define FSEEK fseeko
#define FTELL ftello
#endif
#endif

#define FBTRACE_PACKET_HEADER_LENGTH 8

// Space symbol
const size_t SPACE_SMBL	= 0x20;
const size_t NULL_SMBL	= 0x0;
const TEXT SVC_TRMNTR	= '\377';
const size_t MAX_PACKET_LENGTH = 10*1024*1024;	// 10 MB should be enough

using namespace Firebird;

FBTraceAdapter::FBTraceAdapter(USHORT rel_id, const char* filename) :
	buffer(inlineBuffer), buffSize(sizeof(inlineBuffer)),
	file_name(*getDefaultMemoryPool()),
	connected_users(getDefaultMemoryPool())
{
	bufferPtr = buffer;
	adapter_type = ext_fbtrace;
	header_length = 4;
	if (!(file = os_utils::fopen(filename, "rb")))
	{
		ERR_post(Arg::Gds(isc_io_error) << Arg::Str("fopen") << Arg::Str(filename) << 
				 Arg::Gds(isc_io_open_err) << SYS_ERR(errno));
	}
	this->rel_id = rel_id;
	file_name.assign(filename);
	check_file_format();
	fclose(file);
	file = NULL;

	UCHAR* ptr = DataBuffer;

	for (const AdapterField* adp_field = fbtraceFields;	adp_field->fld_name; adp_field++)
	{
		fields[adp_field->fld_id].data = ptr;
		fields[adp_field->fld_id].field_length = adp_field->fld_length;
		ptr += adp_field->fld_length;
	}
	fields_are_mapped = false;
}

FBTraceAdapter::~FBTraceAdapter() 
{
	if (buffSize > sizeof(inlineBuffer))
		delete[] buffer;
}

template <typename T>
inline void FBTraceAdapter::align_ptr() {
	size_t newStart = FB_ALIGN(bufferPtr - buffer, sizeof(T));
	increment_buffer_ptr(newStart - (bufferPtr - buffer));
}

template <typename T>
inline void FBTraceAdapter::convert_endian(T& number) {
	if (!needEndianConvert)
		return;
	size_t size = sizeof(T);
	UCHAR bytes[8];
	const UCHAR* p = reinterpret_cast<UCHAR*>(&number);
	p += size - 1;
	for (size_t i = 0; i < size; i++)
		bytes[i] = *p--;
	memcpy(&number, bytes, size);
}

USHORT FBTraceAdapter::datacpy(UCHAR* to, const UCHAR* from, size_t length) {
	if (length < max_blob_size) {
		memcpy(to, from, length);
		return length;
	}
	else {
		memcpy(to, from, max_blob_size - 3);
		memset(to + max_blob_size - 3, '.', 3);
		return max_blob_size;
	}
}

void inline FBTraceAdapter::increment_buffer_ptr(size_t step) {
	if (step + bufferPtr > buffer + buffSize)
		ERR_post(Arg::Gds(isc_bad_ext_record) << Arg::Str(file_name) <<
			Arg::Num(FTELL(file) - packetLength));
	else
		bufferPtr += step;
}

void inline FBTraceAdapter::verify_capacity(size_t given_size, size_t max_size) {
	if (given_size > max_size) {
		ERR_post(Arg::Gds(isc_bad_ext_record) << Arg::Str(file_name) <<
			Arg::Num(FTELL(file) - packetLength));
	}
}

ULONG FBTraceAdapter::addDBAttachementInfo(size_t length, const UCHAR* field_data, UCHAR blob_type, UCHAR* dest) {
	string username, remote_protocol, remote_address, hw_address;
	ClumpletReader dpb_reader(ClumpletReader::Tagged, field_data, length);
	while (!dpb_reader.isEof())
	{
		if (dpb_reader.getClumpTag() == isc_dpb_user_name) {
			dpb_reader.getString(username);
		}
		if (dpb_reader.getClumpTag() == isc_dpb_hw_address) {
			dpb_reader.getString(hw_address);
		}
		if (dpb_reader.getClumpTag() != isc_dpb_address_path) {
			dpb_reader.moveNext();
			continue;
		}
		ClumpletReader address_stack(ClumpletReader::UnTagged, 
			dpb_reader.getBytes(), dpb_reader.getClumpLength());
		while (!address_stack.isEof()) {
			if (address_stack.getClumpTag() != isc_dpb_address) {
				address_stack.moveNext();
				continue;
			}
			ClumpletReader address(ClumpletReader::UnTagged, 
				address_stack.getBytes(), address_stack.getClumpLength());
			while (!address.isEof()) {
				switch (address.getClumpTag()) {
					case isc_dpb_addr_protocol:
						address.getString(remote_protocol);
						break;
					case isc_dpb_addr_endpoint:
						address.getString(remote_address);
						break;
					default:
						break;
				}
				address.moveNext();
			}
			break;
		}
		dpb_reader.moveNext();
	}

	if (remote_protocol.isEmpty())
		remote_protocol = "LOCAL";

	if (remote_address.isEmpty())
		remote_address = "<unknown>";

	if (hw_address.isEmpty())
		hw_address = "<unknown>";

	if (username.isEmpty())
		username = "<unknown>";

	switch (blob_type) {
		case blb_event_user_dpb:
			return datacpy(dest, (const UCHAR*) username.c_str(), username.length());
		case blb_event_protocol_dpb:
			return datacpy(dest, (const UCHAR*) remote_protocol.c_str(), remote_protocol.length());
		case blb_event_hostname_dpb:
			return datacpy(dest, (const UCHAR*) remote_address.c_str(), remote_address.length());
		case blb_event_hw_address_dpb:
			return datacpy(dest, (const UCHAR*) hw_address.c_str(), hw_address.length());
	}
	return 0;
}

ULONG FBTraceAdapter::addSvcAttachementInfo(size_t length, const UCHAR* field_data, UCHAR blob_type, UCHAR* dest) {
	string username, dbname, remote_protocol, remote_address, hw_address;
	ClumpletReader spb_reader(ClumpletReader::SpbAttach, field_data, length);
	while (!spb_reader.isEof())
	{
		if (spb_reader.getClumpTag() == isc_spb_user_name) {
			spb_reader.getString(username);
		}
		if (spb_reader.getClumpTag() == isc_spb_dbname) {
			spb_reader.getString(dbname);
		}
		if (spb_reader.getClumpTag() == isc_spb_hw_address) {
			spb_reader.getString(hw_address);
		}
		if (spb_reader.getClumpTag() != isc_spb_address_path) {
			spb_reader.moveNext();
			continue;
		}
		ClumpletReader address_stack(ClumpletReader::UnTagged, 
			spb_reader.getBytes(), spb_reader.getClumpLength());
		while (!address_stack.isEof()) {
			if (address_stack.getClumpTag() != isc_dpb_address) {
				address_stack.moveNext();
				continue;
			}
			ClumpletReader address(ClumpletReader::UnTagged, 
				address_stack.getBytes(), address_stack.getClumpLength());
			while (!address.isEof()) {
				switch (address.getClumpTag()) {
					case isc_dpb_addr_protocol:
						address.getString(remote_protocol);
						break;
					case isc_dpb_addr_endpoint:
						address.getString(remote_address);
						break;
					default:
						break;
				}
				address.moveNext();
			}
			break;
		}
		spb_reader.moveNext();
	}

	if (remote_protocol.isEmpty())
		remote_protocol = "LOCAL";

	if (remote_address.isEmpty())
		remote_address = "<unknown>";

	if (hw_address.isEmpty())
		hw_address = "<unknown>";

	if (dbname.isEmpty())
		dbname = "<unknown>";

	if (username.isEmpty())
		username = "<unknown>";

	switch (blob_type) {
		case blb_event_user_spb:
			return datacpy(dest, (const UCHAR*) username.c_str(), username.length());
		case blb_event_protocol_spb:
			return datacpy(dest, (const UCHAR*) remote_protocol.c_str(), remote_protocol.length());
		case blb_event_hostname_spb:
			return datacpy(dest, (const UCHAR*) remote_address.c_str(), remote_address.length());
		case blb_event_database_spb:
			return datacpy(dest, (const UCHAR*) dbname.c_str(), dbname.length());
		case blb_event_hw_address_spb:
			return datacpy(dest, (const UCHAR*) hw_address.c_str(), hw_address.length());
	}
	return 0;
}

ULONG FBTraceAdapter::addTransactionInfo(size_t length, const UCHAR* field_data, UCHAR* dest) {
	string isolation, rec_version, wait, description;
	ClumpletReader tpb_reader(ClumpletReader::Tpb, field_data, length);

	// Extract some parameters from TPB
	while (!tpb_reader.isEof()) {
		switch(tpb_reader.getClumpTag()) {
			case isc_tpb_consistency:
				isolation = "CONSISTENCY";
				break;
			case isc_tpb_concurrency:
				isolation = "CONCURRENCY";
				break;
			case isc_tpb_read_committed:
				isolation = "READ_COMMITTED";
				break;
			case isc_tpb_rec_version:
				rec_version = "REC_VERSION";
				break;
			case isc_tpb_no_rec_version:
				rec_version = "NO_REC_VERSION";
				break;
			case isc_tpb_wait:
				wait = "WAIT";
				break;
			case isc_tpb_nowait:
				wait = "NOWAIT";
				break;
			case isc_tpb_lock_timeout:
				wait.printf("WAIT %d", tpb_reader.getInt());
				break;
		}
		tpb_reader.moveNext();
	}
	bool is_first = true;
	if (isolation.hasData()) {
		description.append(isolation);
		is_first = false;
	}

	if (rec_version.hasData()) {
		if (!is_first) 
			description.append(" | ");
		description.append(rec_version);
		is_first = false;
	}

	if (wait.hasData()) {
		if (!is_first)
			description.append(" | ");
		description.append(wait);
	}

	return datacpy(dest, (const UCHAR*) description.c_str(), description.length());
}

ULONG FBTraceAdapter::addParamsInfo(UCHAR* dest) {
	ntrace_size_t offset;
	ntrace_counter_t paramcount;
	FB_UINT64 begin_pos = FTELL(file);
	aread(&offset, sizeof(ntrace_size_t), 1);
	FB_UINT64 off = FB_ALIGN(FTELL(file) - begin_pos + offset, sizeof(ntrace_counter_t)) - offset;
	aseek(begin_pos + off, SEEK_SET);
	aread(&paramcount, sizeof(ntrace_counter_t), 1);
	convert_endian<ntrace_counter_t>(paramcount);
	string params, paramtype, paramvalue, temp;
	HalfStaticArray<UCHAR, BUFFER_TINY> value;

	for (ntrace_counter_t i = 0; i < paramcount; i++) {
		dsc parameter;
		UCHAR buf[LogPacketBuilder::DSC_PACKET_SIZE] = {0};
		UCHAR* ptr = buf;
		FB_UINT64 off = FB_ALIGN(FTELL(file) - begin_pos + offset, sizeof(char)) - offset;
		aseek(begin_pos + off, SEEK_SET);
		aread(&buf, LogPacketBuilder::DSC_PACKET_SIZE, 1);

		parameter.dsc_dtype = *ptr;
		ptr += sizeof(parameter.dsc_dtype);

		parameter.dsc_scale = *ptr;
		ptr += sizeof(parameter.dsc_scale);

		parameter.dsc_length = *(USHORT*)ptr;
		ptr += sizeof(parameter.dsc_length);

		parameter.dsc_flags = *(USHORT*)ptr;
		ptr += sizeof(parameter.dsc_flags);

		parameter.dsc_sub_type = *(SSHORT*)ptr;

		convert_endian<USHORT>(parameter.dsc_flags);
		convert_endian<USHORT>(parameter.dsc_length);
		convert_endian<SSHORT>(parameter.dsc_sub_type);
		verify_capacity(parameter.dsc_length, string::max_length());

		aread(value.getBuffer(parameter.dsc_length), parameter.dsc_length, 1);
		parameter.dsc_address = value.begin();

		// Assign type name
		switch (parameter.dsc_dtype) {
			case dtype_text:
				paramtype.printf("char(%d)", parameter.dsc_length);
				break;
			case dtype_cstring:
				paramtype.printf("cstring(%d)", parameter.dsc_length - 1);
				break;
			case dtype_varying:
				paramtype.printf("varchar(%d)", parameter.dsc_length - 2);
				break;
			case dtype_blob:
				paramtype = "blob";
				break;
			case dtype_array:
				paramtype = "array";
				break;
			case dtype_quad:
				paramtype = "quad";
				break;

			case dtype_short:
				if (parameter.dsc_scale)
					paramtype.printf("smallint(*, %d)", parameter.dsc_scale);
				else
					paramtype = "smallint";
				break;
			case dtype_long:
				if (parameter.dsc_scale)
					paramtype.printf("integer(*, %d)", parameter.dsc_scale);
				else
					paramtype = "integer";
				break;
			case dtype_double:
				if (parameter.dsc_scale)
					paramtype.printf("double precision(*, %d)", parameter.dsc_scale);
				else
					paramtype = "double precision";
				break;
			case dtype_int64:
				if (parameter.dsc_scale)
					paramtype.printf("bigint(*, %d)", parameter.dsc_scale);
				else
					paramtype = "bigint";
				break;

			case dtype_real:
				paramtype = "float";
				break;
			case dtype_sql_date:
				paramtype = "date";
				break;
			case dtype_sql_time:
				paramtype = "time";
				break;
			case dtype_timestamp:
				paramtype = "timestamp";
				break;
			default:
				paramtype.printf("<type%d>", parameter.dsc_dtype);
				break;
		}

		if (parameter.dsc_flags & DSC_null)
			paramvalue = "<NULL>";
		else {
			// Assign value
			switch (parameter.dsc_dtype) {
				// Handle potentially long string values
				case dtype_text:
					paramvalue.printf("\"%.*s\"", parameter.dsc_length, 
						parameter.dsc_address);
					break;
				case dtype_cstring:
					paramvalue.printf("\"%.*s\"", 
						strlen(reinterpret_cast<const char*>(parameter.dsc_address)), 
						parameter.dsc_address);
					break;
				case dtype_varying:
					paramvalue.printf("\"%.*s\"", *(USHORT*)parameter.dsc_address,
						parameter.dsc_address + 2);
					break;

				// Handle quad
				case dtype_quad:
				case dtype_blob: 
				case dtype_array: {
					ISC_QUAD *quad = (ISC_QUAD*) parameter.dsc_address;
					paramvalue.printf("\"%08X%08X\"", quad->gds_quad_high, quad->gds_quad_low);
					break;
				}

				case dtype_short:
					fb_utils::exactNumericToStr(*(SSHORT*) parameter.dsc_address, parameter.dsc_scale, paramvalue);
					break;

				case dtype_long:
					fb_utils::exactNumericToStr(*(SLONG*) parameter.dsc_address, parameter.dsc_scale, paramvalue);
					break;

				case dtype_int64:
					fb_utils::exactNumericToStr(*(SINT64*) parameter.dsc_address, parameter.dsc_scale, paramvalue);
					break;

				case dtype_real:
					if (!parameter.dsc_scale)
						paramvalue.printf("\"%f\"", *(float*)parameter.dsc_address);
					else
						paramvalue.printf("\"%f\"", 
							*(float*)parameter.dsc_address * pow((float)10, -parameter.dsc_scale));
					break;

				case dtype_double:
					if (!parameter.dsc_scale)
						paramvalue.printf("\"%f\"", *(double*)parameter.dsc_address);
					else
						paramvalue.printf("\"%f\"", 
							*(double*)parameter.dsc_address * pow((double)10, -parameter.dsc_scale));
					break;

				case dtype_sql_date: {
					struct tm times;
					Firebird::TimeStamp::decode_date(*(ISC_DATE*)parameter.dsc_address, &times);
					paramvalue.printf("\"%04d-%02d-%02d\"", times.tm_year + 1900, times.tm_mon + 1, times.tm_mday);
					break;
				}
				case dtype_sql_time: {
					int hours, minutes, seconds, fractions;
					Firebird::TimeStamp::decode_time(*(ISC_TIME*)parameter.dsc_address, 
						&hours, &minutes, &seconds, &fractions);

					paramvalue.printf("\"%02d:%02d:%02d.%04d\"", hours,	minutes, seconds, fractions);
					break;
				}
				case dtype_timestamp: {
					Firebird::TimeStamp ts(*(ISC_TIMESTAMP*)parameter.dsc_address);
					struct tm times;

					ts.decode(&times);

					paramvalue.printf("\"%04d-%02d-%02dT%02d:%02d:%02d.%04d\"", 
						times.tm_year + 1900, times.tm_mon + 1, times.tm_mday,
						times.tm_hour, times.tm_min, times.tm_sec, 
						ts.value().timestamp_time % ISC_TIME_SECONDS_PRECISION);
					break;
				}
				default:
					paramvalue = "<unknown>";
			}
		}
		temp.printf("param%d = %s, %s" NEWLINE, i, paramtype.c_str(), paramvalue.c_str());
		params.append(temp);
	}

	return datacpy(dest, (const UCHAR*) params.c_str(), params.length());
}

ULONG FBTraceAdapter::addSvcSwitchesInfo(size_t length, const UCHAR* field_data, UCHAR* dest) {
	string switches;
	switches.printf("%.*s", length, field_data);
	// Delete terminator symbols from service switches
	for (size_t i = 0; i < switches.length(); ++i)
		if (switches[i] == Firebird::SVC_TRMNTR) {
			switches.erase(i, 1);
			--i;
		}
	return datacpy(dest, (const UCHAR*) switches.c_str(), switches.length());
}

ULONG FBTraceAdapter::addSvcQueryInfo(UCHAR* dest) {
	string send_query, recv_query, query;
	USHORT l;
	SCHAR item;
	const SCHAR* items;
	ntrace_size_t offset;

	FB_UINT64 begin_pos = FTELL(file);
	aread(&offset, sizeof(ntrace_size_t), 1);
	ntrace_size_t send_length = 0;
	aread(&send_length, sizeof(ntrace_size_t), 1);
	convert_endian<ntrace_size_t>(send_length);

	if (send_length) {
		verify_capacity(send_length, string::max_length());

		HalfStaticArray<SCHAR, BUFFER_TINY> send_items;
		aread(send_items.getBuffer(send_length), send_length, 1);
		// Parse send portion first
		items = send_items.begin();
		const SCHAR* const end_items = items + send_length;
		while (items < end_items && *items != isc_info_end)
		{
			switch ((item = *items++))
			{
			case isc_info_end:
				break;

			default:
				if (items + 2 <= end_items)
				{
					l = (USHORT) gds__vax_integer(reinterpret_cast<const UCHAR*>(items), 2);
					items += 2;
					if (items + l <= end_items)
					{
						switch (item) {
						case isc_info_svc_line:
							send_query.printf(NEWLINE "\tsend line: %.*s", l, items);
							break;
						case isc_info_svc_message:
							send_query.printf(NEWLINE "\tsend message: %.*s", l + 3, items - 3);
							break;
						case isc_info_svc_timeout:
							send_query.printf(NEWLINE "\tset timeout: %d", 
								(USHORT) gds__vax_integer(reinterpret_cast<const UCHAR*>(items), l));
							break;
						case isc_info_svc_version:
							send_query.printf(NEWLINE "\tset version: %d", 
								(USHORT) gds__vax_integer(reinterpret_cast<const UCHAR*>(items), l));
							break;
						}
					}
					items += l;
				}
				else
					items += 2;
				break;
			}
		}	// while

		if (send_query.hasData())
		{
			query.append("Send portion of the query:");
			query.append(send_query);
		}
	}

	// Read recieve portion of query from file
	FB_UINT64 off = FB_ALIGN(FTELL(file) - begin_pos + offset, sizeof(ntrace_size_t)) - offset;
	aseek(begin_pos + off, SEEK_SET);
	ntrace_size_t recv_length = 0;
	aread(&recv_length, sizeof(ntrace_size_t), 1);
	convert_endian<ntrace_size_t>(recv_length);
	
	if (recv_length) {
		verify_capacity(recv_length, string::max_length());

		HalfStaticArray<SCHAR, BUFFER_TINY> recv_items;
		aread(recv_items.getBuffer(recv_length), recv_length, 1);
		items = recv_items.begin();
		const SCHAR* const end_items2 = items + recv_length;

		if (*items == isc_info_length) {
			items++;
		}

		while (items < end_items2 && *items != isc_info_end)
		{
			switch ((item = *items++))
			{
				case isc_info_end:
					break;

				case isc_info_svc_svr_db_info:
					recv_query.printf(NEWLINE "\tretrieve number of attachments and databases");
					break;

				case isc_info_svc_svr_online:
					recv_query.printf(NEWLINE "\tset service online");
					break;

				case isc_info_svc_svr_offline:
					recv_query.printf(NEWLINE "\tset service offline");
					break;

				case isc_info_svc_get_env:
					recv_query.printf(NEWLINE "\tretrieve the setting of $FIREBIRD");
					break;

				case isc_info_svc_get_env_lock:
					recv_query.printf(NEWLINE "\tretrieve the setting of $FIREBIRD_LCK");
					break;

				case isc_info_svc_get_env_msg:
					recv_query.printf(NEWLINE "\tretrieve the setting of $FIREBIRD_MSG");
					break;

				case isc_info_svc_dump_pool_info:
					recv_query.printf(NEWLINE "\t print memory counters");
					break;

				case isc_info_svc_get_config:
					recv_query.printf(NEWLINE "\tretrieve the parameters and values for IB_CONFIG");
					break;

				case isc_info_svc_default_config:
					recv_query.printf(NEWLINE "\treset the config values to defaults");
					break;

				case isc_info_svc_set_config:
					recv_query.printf(NEWLINE "\tset the config values");
					break;

				case isc_info_svc_version:
					recv_query.printf(NEWLINE "\tretrieve the version of the service manager");
					break;

				case isc_info_svc_capabilities:
					recv_query.printf(NEWLINE "\tretrieve a bitmask representing the server's capabilities");
					break;

				case isc_info_svc_server_version:
					recv_query.printf(NEWLINE "\tretrieve the version of the server engine");
					break;

				case isc_info_svc_implementation:
					recv_query.printf(NEWLINE "\tretrieve the implementation of the Firebird server");
					break;

				case isc_info_svc_user_dbpath:
					recv_query.printf(NEWLINE "\tretrieve the path to the security database in use by the server");
					break;

				case isc_info_svc_response:
					recv_query.printf(NEWLINE "\tretrieve service response");
					break;

				case isc_info_svc_response_more:
					recv_query.printf(NEWLINE "\tretrieve service response more");
					break;

				case isc_info_svc_total_length:
					recv_query.printf(NEWLINE "\tretrieve total length");
					break;

				case isc_info_svc_line:
					recv_query.printf(NEWLINE "\tretrieve 1 line of service output per call");
					break;

				case isc_info_svc_to_eof:
					recv_query.printf(NEWLINE "\tretrieve as much of the server output as will fit in the supplied bufferPtr");
					break;
				case isc_info_svc_limbo_trans:
					recv_query.printf(NEWLINE "\tretrieve the limbo transactions");
					break;
				case isc_info_svc_get_users:
					recv_query.printf(NEWLINE "\tretrieve the user information");
					break;
			}
		}	// while

		if (recv_query.hasData())
		{
			query.append("Receive portion of the query:");
			query.append(recv_query);
		}
	}

	return datacpy(dest, (const UCHAR*) query.c_str(), query.length());
}

ULONG FBTraceAdapter::addPerfInfo(UCHAR* dest) {
	string temp, perf;
	FB_UINT64 begin_pos = FTELL(file);

	ntrace_size_t offset;
	aread(&offset, sizeof(ntrace_size_t), 1);

	// Process global counters first
	ntrace_size_t counters_length = 0;
	aread(&counters_length, sizeof(ntrace_size_t), 1);
	convert_endian<ntrace_size_t>(counters_length);

	if (counters_length) {
		verify_capacity(counters_length, Jrd::RuntimeStatistics::REL_BASE_OFFSET * sizeof(ntrace_counter_t));

		static ntrace_counter_t dummy_counts[Jrd::RuntimeStatistics::REL_BASE_OFFSET];
		memset(&dummy_counts, 0, counters_length);
		ntrace_counter_t* pin_counters = dummy_counts;
		aread(pin_counters, counters_length, 1);
		
		ntrace_counter_t cnt;
		bool is_first = true;

		if ((cnt = pin_counters[Jrd::RuntimeStatistics::PAGE_READS]) != 0) {
			convert_endian<ntrace_counter_t>(cnt);
			temp.printf("%" QUADFORMAT"d reads", cnt);
			perf.append(temp);
			is_first = false;
		}

		if ((cnt = pin_counters[Jrd::RuntimeStatistics::PAGE_WRITES]) != 0) {
			convert_endian<ntrace_counter_t>(cnt);
			temp.printf("%" QUADFORMAT"d writes", cnt);
			if (!is_first)
				perf.append(", ");
			perf.append(temp);
			is_first = false;
		}

		if ((cnt = pin_counters[Jrd::RuntimeStatistics::PAGE_FETCHES]) != 0) {
			convert_endian<ntrace_counter_t>(cnt);
			temp.printf("%" QUADFORMAT"d fetches", cnt);
			if (!is_first)
				perf.append(", ");
			perf.append(temp);
			is_first = false;
		}

		if ((cnt = pin_counters[Jrd::RuntimeStatistics::PAGE_MARKS]) != 0) {
			convert_endian<ntrace_counter_t>(cnt);
			temp.printf("%" QUADFORMAT"d marks", cnt);
			if (!is_first)
				perf.append(", ");
			perf.append(temp);
			is_first = false;
		}

		if (!is_first)
			perf.insert(0, "Global: ");
	}

	// Then process counters for individual table
	FB_UINT64 off = FB_ALIGN(FTELL(file) - begin_pos + offset, sizeof(ntrace_counter_t)) - offset;
	aseek(begin_pos + off, SEEK_SET);
	ntrace_counter_t rel_counters_count = 0;
	aread(&rel_counters_count, sizeof(ntrace_counter_t), 1);
	convert_endian<ntrace_counter_t>(rel_counters_count);
	if (rel_counters_count) {
		for (ntrace_counter_t i = 0; i < rel_counters_count; i++) {
			// Read relation name length
			off = FB_ALIGN(FTELL(file) - begin_pos + offset, sizeof(ntrace_size_t)) - offset;
			aseek(begin_pos + off, SEEK_SET);
			ntrace_size_t len = 0;
			aread(&len, sizeof(ntrace_size_t), 1);
			convert_endian<ntrace_size_t>(len);

			if (len) {
				verify_capacity(len, MAX_SQL_IDENTIFIER_SIZE);
				// Read relation name
				HalfStaticArray<SCHAR, MAX_SQL_IDENTIFIER_SIZE> relation_name;
				aread(relation_name.getBuffer(len), len, 1);
				temp.printf("%.*s: ", len, relation_name.begin());
			}

			// Read relation counters
			off = FB_ALIGN(FTELL(file) - begin_pos + offset, sizeof(ntrace_size_t)) - offset;
			aseek(begin_pos + off, SEEK_SET);
			ntrace_size_t rel_counters_length = 0;
			aread(&rel_counters_length, sizeof(ntrace_size_t), 1);
			convert_endian<ntrace_size_t>(rel_counters_length);

			if (rel_counters_length) {
				verify_capacity(rel_counters_length, Jrd::RuntimeStatistics::REL_TOTAL_ITEMS * sizeof(ntrace_counter_t));
				perf.append(NEWLINE);
				perf.append(temp);
				static ntrace_counter_t dummy_rel_counts[Jrd::RuntimeStatistics::REL_TOTAL_ITEMS];
				memset(&dummy_rel_counts, 0, rel_counters_length);
				ntrace_counter_t* pin_rel_counters = dummy_rel_counts;
				off = FB_ALIGN(FTELL(file) - begin_pos + offset, sizeof(ntrace_size_t)) - offset;
				aseek(begin_pos + off, SEEK_SET);
				aread(pin_rel_counters, rel_counters_length, 1);

				bool is_first = true;
				for (int j = 0; j < Jrd::RuntimeStatistics::REL_TOTAL_ITEMS; j++)
					if (pin_rel_counters[j] != 0) {
						if (!is_first)
							perf.append(", ");
						convert_endian<ntrace_counter_t>(pin_rel_counters[j]);
						temp.printf("%" QUADFORMAT"d %s", pin_rel_counters[j], rel_counters_names[j]);
						perf.append(temp);
						is_first = false;
					}
			}
		}
	}

	return datacpy(dest, (const UCHAR*) perf.c_str(), perf.length());
}

ULONG FBTraceAdapter::addFactorTypeInfo(UCHAR* dest) {
	/*
	string type;

	// Read type from log file
	USHORT num_type = 0;
	aread(&num_type, sizeof(USHORT), 1);
	convert_endian<USHORT>(num_type);

	switch (num_type) {
		case AuthFactor::afWindowsNTLM:
			type = "WINDOWS_NTLM";
			break;
		case AuthFactor::afPassword:
			type = "PASSWORD";
			break;
		case AuthFactor::afCertX509:
			type = "CERT_X509";
			break;
		case AuthFactor::afEye:
			type = "EYE";
			break;
		case AuthFactor::afFingerPrint:
			type = "FINGERPRINT";
			break;
		case AuthFactor::afServerCert:
			type = "SERVER_CERTIFICATE";
			break;
		default:
			type = "UNKNOWN";
			break;
	}
	return datacpy(dest, (const UCHAR*) type.c_str(), type.length());
	*/
	return 0;
}


ULONG FBTraceAdapter::addPasswordInfo(size_t length, const UCHAR* field_data, UCHAR* dest) {
	/*
	string passwd_descr;
	string userHash, mixedHash;
	const UCHAR* randomBuffer;
	size_t randomBufferLength;
	const UCHAR* hashMethod;
	size_t hashMethodLength;
	ClumpletReader hashData(ClumpletReader::Tagged, field_data, length);

	while (!hashData.isEof()) {
		switch(hashData.getClumpTag()) {
			case hdUserHash:
				hashData.getString(userHash);
				break;
			case hdRandomBuffer:
				randomBuffer = hashData.getBytes();
				randomBufferLength = hashData.getClumpLength();
				break;
			case hdMixedHash:
				hashData.getString(mixedHash);
				break;
			case hdHashMethod:
				hashMethod = hashData.getBytes();
				hashMethodLength = hashData.getClumpLength();
				break;
		}
		hashData.moveNext();
	}

	if (userHash.isEmpty() || hashMethodLength == 0 || randomBufferLength == 0)
		// Legacy verify
		passwd_descr.printf("Received hash (legacy verify): %s", mixedHash.c_str());
	else
	{
		Firebird::string random_hex;
		fb_utils::printBuffer(random_hex, randomBuffer, randomBufferLength);

		passwd_descr.printf(
			"Original user hash: %s" NEWLINE 
			"Hash algorithm: %.*s" NEWLINE 
			"Random number: %s" NEWLINE 
			"Received mixed hash: %s", 
			userHash.c_str(), hashMethodLength, hashMethod, random_hex.c_str(),
			mixedHash.c_str());
	}
	
	return datacpy(dest, (const UCHAR*) passwd_descr.c_str(), passwd_descr.length());
	*/
	return 0;
}

ULONG FBTraceAdapter::addPrivilegeInfo(size_t length, const UCHAR* field_data, UCHAR* dest)
{
	string privileges = "";	
	for (size_t i = 0; i < length; ++i) {
		switch (UPPER7(field_data[i])) {
			case 'A' : privileges.append("ALL ");			break;
			case 'I' : privileges.append("INSERT ");		break;
			case 'U' : privileges.append("UPDATE ");		break;
			case 'D' : privileges.append("DELETE ");		break;
			case 'S' : privileges.append("SELECT ");		break;
			case 'X' : privileges.append("EXECUTE ");		break;
			case 'R' : privileges.append("REFERENCE ");		break;
			case 'C' : privileges.append("CREATE ");		break;
			case 'L' : privileges.append("ALTER ");			break;
			case 'T' : privileges.append("ALTER ANY ");		break;
			case 'O' : privileges.append("DROP ");			break;
			case 'P' : privileges.append("DROP ANY ");		break;
			case 'M' : privileges.append("ROLE ");			break;
			case 'K' : privileges.append("ENCRYPTION KEY ");break;
			default  : privileges.append("<unknown> ");		break;
		}
	}
	privileges.rtrim();
	
	return datacpy(dest, (const UCHAR*) privileges.c_str(), privileges.length());
}

// Read from file and raise error if fail
size_t FBTraceAdapter::aread(void* bufferPtr, size_t size, size_t count)
{
	size_t act_read = fread(bufferPtr, size, count, file);
	if (!act_read && size && count)
		ERR_post(Arg::Gds(isc_io_error) << Arg::Str("fread") << Arg::Str(file_name) << 
				 Arg::Gds(isc_io_open_err) << SYS_ERR(errno));
	return act_read;
}

void FBTraceAdapter::aseek(FB_UINT64 offset, int origin)
{
	if (FSEEK(file, offset, origin))
		ERR_post(Arg::Gds(isc_io_error) << Arg::Str("fseek") << Arg::Str(file_name) << 
				 Arg::Gds(isc_io_open_err) << SYS_ERR(errno));
}

void FBTraceAdapter::getCommonFields() {
	// Skip packet header
	increment_buffer_ptr(FBTRACE_PACKET_HEADER_LENGTH);

	getEventTime();
	getEventProcessId();
	getEventObjectId();
}

void FBTraceAdapter::getEventTime() {
	size_t typeSize = sizeof(ISC_TIMESTAMP);
	memcpy(fields[trace_event_time].data, bufferPtr, typeSize);
	fields[trace_event_time].isNull = false;
	increment_buffer_ptr(typeSize);
}

void FBTraceAdapter::getEventProcessId() {
	getLong(&fields[trace_event_process_id]);
	fields[trace_event_process_id].isNull = false;
}

void FBTraceAdapter::getEventObjectId() {
	getPointer(&fields[trace_event_object_id]);
	fields[trace_event_object_id].isNull = false;
}

void FBTraceAdapter::getEventAttId() {
	getInt64(&fields[trace_event_att_id]);
	fields[trace_event_att_id].isNull = false;
}

void FBTraceAdapter::getEventDatabase() {
	getBlob(&fields[trace_event_database]);
}

void FBTraceAdapter::getAuthFactorUser() {
	getBlob(&fields[trace_event_user]);
}

size_t FBTraceAdapter::getAuthFactorType() {
	size_t typeSize = type_lengths[dtype_blob];
	align_ptr<USHORT>();
	size_t cp = current_pos();
	USHORT factor_type = *(USHORT*) bufferPtr;
	convert_endian<USHORT>(factor_type);
	increment_buffer_ptr(sizeof(USHORT));
	blob_id id;
	id.relation_id = rel_id;
	id.blob_type = (UCHAR) blb_auth_factor_type;

	FB_UINT64 filePos = FTELL(file) - (packetLength - cp);
	id.bid_encode(filePos);

	memcpy(fields[trace_auth_factor_type].data, &id, typeSize);
	fields[trace_auth_factor_type].isNull = false;

	return factor_type;
}

void FBTraceAdapter::getAuthFactorData(size_t factor_type) {
	/*
	getBlob(&fields[trace_auth_factor_data]);

	// Set blob type only if factor type is a password
	if (factor_type == AuthFactor::afPassword)
		*(fields[trace_auth_factor_data].data + blb_type_offset) = (UCHAR) blb_auth_factor_data;
	*/
}

void FBTraceAdapter::getAttParams() {
	UCHAR* ptr_user = fields[trace_event_user].data;
	getBlob(&fields[trace_event_user]);
	bool nullable = fields[trace_event_user].isNull;
	
	// Set blob type
	*(ptr_user + blb_type_offset) = (UCHAR) blb_event_user_dpb;

	memcpy(fields[trace_event_protocol].data, ptr_user, 
		type_lengths[dtype_blob]);
	*(fields[trace_event_protocol].data + blb_type_offset) = (UCHAR) blb_event_protocol_dpb;
	fields[trace_event_protocol].isNull = nullable;

	memcpy(fields[trace_event_hostname].data, ptr_user, 
		type_lengths[dtype_blob]);
	*(fields[trace_event_hostname].data + blb_type_offset) = (UCHAR) blb_event_hostname_dpb;
	fields[trace_event_hostname].isNull = nullable;

	memcpy(fields[trace_event_hw_address].data, ptr_user, 
		type_lengths[dtype_blob]);
	*(fields[trace_event_hw_address].data + blb_type_offset) = (UCHAR) blb_event_hw_address_dpb;
	fields[trace_event_hw_address].isNull = nullable;
}

void FBTraceAdapter::getSvcParams() {
	UCHAR* ptr_user = fields[trace_event_user].data;
	getBlob(&fields[trace_event_user]);
	bool nullable = fields[trace_event_user].isNull;

	// Set blob type
	if (*ptr_user) {
		*(ptr_user + blb_type_offset) = (UCHAR) blb_event_user_spb;

		memcpy(fields[trace_event_protocol].data, ptr_user, 
			type_lengths[dtype_blob]);
		*(fields[trace_event_protocol].data + blb_type_offset) = (UCHAR) blb_event_protocol_spb;
		fields[trace_event_protocol].isNull = nullable;

		memcpy(fields[trace_event_hostname].data, ptr_user, 
			type_lengths[dtype_blob]);
		*(fields[trace_event_hostname].data + blb_type_offset) = (UCHAR) blb_event_hostname_spb;
		fields[trace_event_hostname].isNull = nullable;

		memcpy(fields[trace_event_database].data, ptr_user, 
			type_lengths[dtype_blob]);
		*(fields[trace_event_database].data + blb_type_offset) = (UCHAR) blb_event_database_spb;
		fields[trace_event_database].isNull = nullable;

		memcpy(fields[trace_event_hw_address].data, ptr_user, 
			type_lengths[dtype_blob]);
		*(fields[trace_event_hw_address].data + blb_type_offset) = (UCHAR) blb_event_hw_address_spb;
		fields[trace_event_hw_address].isNull = nullable;
	}
}

void FBTraceAdapter::getTransId() {
	getInt64(&fields[trace_trans_id]);
	fields[trace_trans_id].isNull = false;
}

void FBTraceAdapter::getTransOpt() {
	getBlob(&fields[trace_trans_opt]);
	// Set blob type
	if (fields[trace_trans_opt].data)
		*(fields[trace_trans_opt].data + blb_type_offset) = (UCHAR) blb_trans_opt;
}

void FBTraceAdapter::getStmtId() {
	getInt64(&fields[trace_stmt_id]);
	fields[trace_stmt_id].isNull = false;
}

void FBTraceAdapter::getStmtSql() {
	getBlob(&fields[trace_stmt_sql]);
}

void FBTraceAdapter::getBlrData() {
	getBlob(&fields[trace_blr_data]);
	// Set blob type
	if (*fields[trace_blr_data].data)
		*(fields[trace_blr_data].data + blb_type_offset) = (UCHAR) blb_blr_data;
}

void FBTraceAdapter::getDynData() {
	getBlob(&fields[trace_dyn_data]);
	// Set blob type
	if (*fields[trace_dyn_data].data)
		*(fields[trace_dyn_data].data + blb_type_offset) = (UCHAR) blb_dyn_data;
}

void FBTraceAdapter::getStmtAccessPath() {
	getBlob(&fields[trace_stmt_access_path]);
}

void FBTraceAdapter::getStmtParams() {
	getParameters(&fields[trace_stmt_params]);
}

void FBTraceAdapter::getVarName() {
	getBlob(&fields[trace_var_name]);
}

void FBTraceAdapter::getVarNs() {
	getBlob(&fields[trace_var_ns]);
}

void FBTraceAdapter::getVarValue() {
	getBlob(&fields[trace_var_value]);
}

void FBTraceAdapter::getPrivilegeExecutor() {
	getBlob(&fields[trace_event_user]);
}

void FBTraceAdapter::getPrivilegeGrantor() {
	getBlob(&fields[trace_privilege_grantor]);
}

void FBTraceAdapter::getPrivilegeObject() {
	getBlob(&fields[trace_privilege_object]);
}

void FBTraceAdapter::getPrivilegeGrantee() {
	getBlob(&fields[trace_privilege_grantee]);
}

void FBTraceAdapter::getPrivilegePrivilege() {
	getBlob(&fields[trace_privilege_privilege]);
	((blob_id*)(fields[trace_privilege_privilege].data))->blob_type = blb_privilege;
}

void FBTraceAdapter::getPrivilegeOption() {
	align_ptr<USHORT>();
	USHORT value = *(USHORT*) bufferPtr;
	convert_endian<USHORT>(value);
	memcpy(fields[trace_privilege_option].data, &value, sizeof(USHORT));
	increment_buffer_ptr(sizeof(USHORT));
	fields[trace_privilege_option].isNull = false;
}

void FBTraceAdapter::getSvcId() {
	getPointer(&fields[trace_svc_id]);
	fields[trace_svc_id].isNull = false;
}

void FBTraceAdapter::getSvcName() {
	getBlob(&fields[trace_svc_name]);
}

void FBTraceAdapter::getSvcSwitches() {
	getBlob(&fields[trace_svc_switches]);
	// Set blob type
	if (*fields[trace_svc_switches].data)
		*(fields[trace_svc_switches].data + blb_type_offset) = (UCHAR) blb_svc_switches;
}

void FBTraceAdapter::getSvcQuery() {
	size_t typeSize = type_lengths[dtype_blob];

	align_ptr<ntrace_size_t>();
	size_t cp = current_pos();
	increment_buffer_ptr(sizeof(ntrace_size_t));

	align_ptr<ntrace_size_t>();
	ntrace_size_t sendLength = *(ntrace_size_t*) bufferPtr;
	convert_endian<ntrace_size_t>(sendLength);
	increment_buffer_ptr(sizeof(ntrace_size_t) + sendLength);

	align_ptr<ntrace_size_t>();
	ntrace_size_t recvLength = *(ntrace_size_t*) bufferPtr;
	convert_endian<ntrace_size_t>(recvLength);
	increment_buffer_ptr(sizeof(ntrace_size_t) + recvLength);

	UCHAR* ptr = fields[trace_svc_query].data;
	if (!sendLength && !recvLength)
	{
		memset(ptr, NULL_SMBL, typeSize);
		return;
	}
	
	fields[trace_svc_query].isNull = false;

	// Relation id - first 2 bytes
	*ptr = rel_id;
	ptr += sizeof(USHORT);
	// Third byte - unused
	*ptr++ = 0;
	// Fourth byte - type of blob
	*ptr++ = (UCHAR) blb_svc_query;
	/*	ftell(file) point to beginning of next packet
		cp - buffer - shift of the blob record in the file relative 
		to beginning of packet record
		packetLength - length of current packet
	*/
	FB_UINT64 filePos = FTELL(file) - (packetLength - (cp));
	*(FB_UINT64*)ptr = filePos;
}

void FBTraceAdapter::getProcName() {
	getBlob(&fields[trace_proc_name]);
}

void FBTraceAdapter::getProcParams() {
	getParameters(&fields[trace_proc_params]);
}

void FBTraceAdapter::getFuncResult() {
	getParameters(&fields[trace_func_result]);
}

void FBTraceAdapter::getPerfTime() {
	getCounter(&fields[trace_perf_time]);
	fields[trace_perf_time].isNull = false;
}

void FBTraceAdapter::getPerfInfo() {
	getCounters(&fields[trace_perf_info]);
	getPerfTime();
}

void FBTraceAdapter::getRowFetched() {
	getCounter(&fields[trace_row_fetched]);
	fields[trace_row_fetched].isNull = false;
}

void FBTraceAdapter::getEventResult() {
	align_ptr<USHORT>();
	USHORT result = *(USHORT*) bufferPtr;
	convert_endian<USHORT>(result);
	char* res_text;
	switch (result) {
		case ITracePlugin::RESULT_SUCCESS:
			res_text = "SUCCESSFUL";
			break;
		case ITracePlugin::RESULT_UNAUTHORIZED:
			res_text = "UNAUTHORIZED";
			break;
		case ITracePlugin::RESULT_FAILED:
			res_text = "FAILED";
			break;
		default:
			res_text = "UNKNOWN";
			break;
	}
	
	USHORT length = (USHORT) strlen(res_text);
	UCHAR* ptr = fields[trace_event_result].data;
	fields[trace_event_result].isNull = false;
	*(USHORT*)ptr = length;
	ptr += sizeof(USHORT);
	memcpy(ptr, res_text, length);
	increment_buffer_ptr(sizeof(USHORT));
}

void FBTraceAdapter::getCachedAttUser() {
	ReadLockGuard lock(connected_users_lock, "FBTraceAdapter::getCachedAttUser");
	if (connected_users.locate((SLONG)*fields[trace_event_att_id].data)) {
		memcpy(fields[trace_event_user].data, &connected_users.current().bid,
			type_lengths[dtype_blob]);
		fields[trace_event_user].isNull = false;
	} else //if (!(SLONG)*fields[trace_event_att_id].data) {
		// it's some system attachment (e.g. garabage collection's one)
		fields[trace_event_user].isNull = true;
//	} else {
//		ERR_post(Arg::Gds(isc_bad_ext_record) << Arg::Str(file_name) <<
//			Arg::Num(FTELL(file) - packetLength));
//	}
}

/*
int FBTraceAdapter::getObjectType()
{
	align_ptr<USHORT>();
	USHORT object_type = *(USHORT*) bufferPtr;
	convert_endian<USHORT>(object_type);
	const char* object;
	switch(object_type)
	{
		case obj_record:
			object = "record";
			break;
		case obj_database:
			object = "database";
			break;
		case obj_relation:
			object = "table";
			break;
		case obj_view:
			object = "view";
			break;
		case obj_field:
			object = "field";
			break;
		case obj_procedure:
			object = "procedure";
			break;
		case obj_generator:
			object = "generator";
			break;
		case obj_exception:
			object = "exception";
			break;
		case obj_domain:
			object = "domain";
			break;
		case obj_function:
			object = "function";
			break;
	}
	
	USHORT length = (USHORT) strlen(object);
	UCHAR* ptr = fields[trace_object_type].data;
	fields[trace_object_type].isNull = false;
	*(USHORT*)ptr = length;
	ptr += sizeof(USHORT);
	memcpy(ptr, object, length);
	increment_buffer_ptr(sizeof(USHORT));
	return object_type;
}
*/
void FBTraceAdapter::getLong(FieldData* field)
{
	align_ptr<SLONG>();
	size_t typeSize = sizeof(SLONG);
	SLONG value = *(SLONG*) bufferPtr;
	convert_endian<SLONG>(value);
	memcpy(field->data, &value, typeSize);
	increment_buffer_ptr(typeSize);
}

void FBTraceAdapter::getInt64(FieldData* field)
{
	align_ptr<SINT64>();
	size_t typeSize = sizeof(SINT64);
	SINT64 value = *(SINT64*)bufferPtr;
	convert_endian<SINT64>(value);
	memcpy(field->data, &value, typeSize);
	increment_buffer_ptr(typeSize);
}

bool FBTraceAdapter::getBoolean()
{
	align_ptr<ntrace_boolean_t>();
	ntrace_boolean_t value = *(ntrace_boolean_t*)bufferPtr;
	convert_endian<ntrace_boolean_t>(value);
	increment_buffer_ptr(sizeof(ntrace_boolean_t));
	return value;
}

void FBTraceAdapter::getCounter(FieldData* field)
{
	align_ptr<ntrace_counter_t>();
	size_t typeSize = sizeof(ntrace_counter_t);
	ntrace_counter_t value = *(ntrace_counter_t*) bufferPtr;
	convert_endian<ntrace_counter_t>(value);
	memcpy(field->data, &value, typeSize);
	increment_buffer_ptr(typeSize);
}

void FBTraceAdapter::getPointer(FieldData* field)
{
	size_t typeSize = log_pointer_size;
	size_t length = typeSize * 2;
	UCHAR* ptr = field->data;
	*(USHORT*)ptr = (USHORT) length;
	ptr += sizeof(USHORT);
	// Get pointer
	HalfStaticArray<UCHAR, 8> pointer;
	(log_pointer_size == 4) ? align_ptr<SLONG>() : align_ptr<SINT64>();
	memcpy(pointer.getBuffer(typeSize), bufferPtr, typeSize);
	if (log_pointer_size == 4)
		convert_endian<SLONG>(*(SLONG*)pointer.begin());
	else
		convert_endian<SINT64>(*(SINT64*)pointer.begin());

	// Print pointer as a string
	char pBuf[17];
	if (log_pointer_size == 4)
		SNPRINTF(pBuf, sizeof(pBuf), "%08X", *(SLONG*) pointer.begin());
	else
		SNPRINTF(pBuf, sizeof(pBuf), "%016X", *(SINT64*)pointer.begin());
	
	// Copy printed pointer to the record Buffer
	memcpy(ptr, pBuf, length);
	increment_buffer_ptr(log_pointer_size);
}

void FBTraceAdapter::getBlob(FieldData* field)
{
	size_t typeSize = type_lengths[dtype_blob];
	align_ptr<ntrace_size_t>();
	size_t cp = current_pos();
	ntrace_size_t blobLength = *(ntrace_size_t*) bufferPtr;
	convert_endian<ntrace_size_t>(blobLength);
	increment_buffer_ptr(sizeof(ntrace_size_t) + blobLength);
	if (!blobLength)
	{
		memset(field->data, NULL_SMBL, typeSize);
		return;
	}

	field->isNull = false;

	blob_id id;
	id.relation_id = rel_id;
	id.blob_type = 0;
	/*	ftell(file) point to beginning of next packet
		bufferPtr - inlineBuffer - shift of the blob record in the file relative 
		to beginning of packet record
		packetLength - length of current packet
	*/
	FB_UINT64 filePos = FTELL(file) - (packetLength - cp);
	id.bid_encode(filePos);

	memcpy(field->data, &id, typeSize);
}

void FBTraceAdapter::getParameters(FieldData* field)
{
	size_t typeSize = type_lengths[dtype_blob];
	align_ptr<ntrace_size_t>();
	size_t cp = current_pos();
	increment_buffer_ptr(sizeof(ntrace_size_t));

	align_ptr<ntrace_counter_t>();
	ntrace_counter_t paramCount = *(ntrace_counter_t*) bufferPtr;
	convert_endian<ntrace_counter_t>(paramCount);
	increment_buffer_ptr(sizeof(ntrace_counter_t));
	if (!paramCount)
	{
		memset(field->data, NULL_SMBL, typeSize);
		return;
	}

	field->isNull = false;

	blob_id id;
	id.relation_id = rel_id;
	id.blob_type = (UCHAR) blb_params;
	/*	ftell(file) point to beginning of next packet
		bufferPtr - inlineBuffer - shift of the blob record in the file relative 
		to beginning of packet record
		packetLength - length of current packet
	*/
	FB_UINT64 filePos = FTELL(file) - (packetLength - cp);
	id.bid_encode(filePos);

	memcpy(field->data, &id, typeSize);

	// Skip parameters in bufferPtr
	for (ntrace_size_t i = 0; i < paramCount; i++) {
		UCHAR* ptr = bufferPtr;
		increment_buffer_ptr(LogPacketBuilder::DSC_PACKET_SIZE);
		ptr += sizeof(((dsc*)NULL)->dsc_dtype) + sizeof(((dsc*)NULL)->dsc_scale);
		ntrace_size_t len = *(USHORT*)ptr;
		convert_endian<ntrace_size_t>(len);
		increment_buffer_ptr(len);
	}
}

void FBTraceAdapter::getCounters(FieldData* field)
{
	size_t typeSize = type_lengths[dtype_blob];

	align_ptr<ntrace_size_t>();
	size_t cp = current_pos();
	increment_buffer_ptr(sizeof(ntrace_size_t));

	align_ptr<ntrace_size_t>();
	ntrace_size_t dbbCountersLength = *(ntrace_size_t*) bufferPtr;
	convert_endian<ntrace_size_t>(dbbCountersLength);
	increment_buffer_ptr(sizeof(ntrace_size_t) + dbbCountersLength);

	align_ptr<ntrace_counter_t>();
	ntrace_counter_t relCountersCount = *(ntrace_counter_t*) bufferPtr;
	convert_endian<ntrace_counter_t>(relCountersCount);
	increment_buffer_ptr(sizeof(ntrace_counter_t));
	
	if (!dbbCountersLength && !relCountersCount)
	{
		memset(field->data, NULL_SMBL, typeSize);
		return;
	}

	field->isNull = false;

	blob_id id;
	id.relation_id = rel_id;
	id.blob_type = (UCHAR) blb_perf_info;
	/*	ftell(file) point to beginning of next packet
		bufferPtr - inlineBuffer - shift of the blob record in the file relative 
		to beginning of packet record
		packetLength - length of current packet
	*/
	FB_UINT64 filePos = FTELL(file) - (packetLength - cp);
	id.bid_encode(filePos);

	memcpy(field->data, &id, typeSize);	

	for (ntrace_size_t i = 0; i < relCountersCount; i++) {
		ntrace_size_t len;
		// 2 iteration: relation name and counters for this relation
		int j = 0;
		do {
			align_ptr<ntrace_size_t>();
			len = *(ntrace_size_t*) bufferPtr;
			convert_endian<ntrace_size_t>(len);
			increment_buffer_ptr(sizeof(ntrace_size_t));
			if (len) 
				increment_buffer_ptr(len);
		}
		while (j++ < 1);
	}
}

void FBTraceAdapter::getError() {
	getBlob(&fields[trace_error_message]);
//	getBlob(&fields[trace_error_stacktrace]);
}

void FBTraceAdapter::getTrgName() {
	getBlob(&fields[trace_trg_name]);
}

void FBTraceAdapter::getTrgAction() {
	getBlob(&fields[trace_trg_action]);
}

void FBTraceAdapter::growBuffer(size_t requiredSize)
{
	UCHAR* newBuffer = FB_NEW_POOL(*getDefaultMemoryPool()) UCHAR[requiredSize];
	memset(newBuffer, 0, requiredSize);
	memcpy(newBuffer, buffer, FBTRACE_PACKET_HEADER_LENGTH);
	if (buffSize > sizeof(inlineBuffer))
		delete[] buffer;
	buffer = newBuffer;
	buffSize = requiredSize;
	bufferPtr = buffer;
}

void FBTraceAdapter::putEventType(const char* event_type)
{
	size_t len = strlen(event_type);
	UCHAR *ptr = fields[trace_event_type].data;
	memcpy(ptr, event_type, len);
	fields[trace_event_type].isNull = false;
	if (len < FBTRACE_EVENT_TYPE_LENGTH) {
		memset(ptr + len, SPACE_SMBL, 
			FBTRACE_EVENT_TYPE_LENGTH - len);
	}
}

void FBTraceAdapter::set_file_handle(FILE* file_handle)
{
	fb_assert(file_handle);
	file = file_handle;
	parse_log_header();
}

void FBTraceAdapter::check_file_format()
{
	fb_assert(file);

	UCHAR f[1];
	aseek(1, SEEK_SET);
	aread(f, 1, 1);
	if (*f != LOG_FORMAT_VERSION)
	{
		fclose(file);
		file = NULL;
		ERR_post(Arg::Gds(isc_bad_ext_file));
	}
	else
		fmt_version = *f;
}

void FBTraceAdapter::parse_log_header()
{
	fb_assert(file);

	UCHAR f[2];
	aseek(2, SEEK_SET);
	aread(f, 2, 1);
	log_bigendian = f[0];
#if defined(WORDS_BIGENDIAN)
	needEndianConvert = !log_bigendian; 
#else
	needEndianConvert = log_bigendian;
#endif
	log_pointer_size = f[1];
}


void FBTraceAdapter::read_packet_from_file()
{
	// Prepare bufferPtr for new packet
	bufferPtr = buffer;
	memset(inlineBuffer, 0, sizeof(inlineBuffer));
	// Read packet header to extract packet length
	UCHAR *ptr = bufferPtr;
	if (! fread(ptr, FBTRACE_PACKET_HEADER_LENGTH, 1, file)) {
		packetLength = 0;
		return;
	}
	ptr += 4;
	packetLength = *(ntrace_size_t*) ptr;
	convert_endian<ntrace_size_t>(packetLength);
	verify_capacity(packetLength, MAX_PACKET_LENGTH);
	if (packetLength > buffSize) {
		growBuffer(packetLength);
	}
	// Read the rest of packet
	ptr = bufferPtr + FBTRACE_PACKET_HEADER_LENGTH;
	if (! fread(ptr, packetLength - FBTRACE_PACKET_HEADER_LENGTH, 1, file)) {
		packetLength = 0;
		return;
	}
	if (packetLength > buffSize)
		buffSize = packetLength;
}

void FBTraceAdapter::clear_record_buffer()
{
	recordPtr = recordBuffer;
	memset(recordBuffer, 0, sizeof(recordBuffer));

	memset(DataBuffer, 0, sizeof(DataBuffer));

	if (!fields[0].isNull) {
		for (USHORT i = 0; i < trace_flds_count; ++i) {
			fields[i].isNull = true;
		}
	}
}

void FBTraceAdapter::parse_event_initialize()
{
	putEventType("TRACE INIT");
	getEventDatabase();
}

void FBTraceAdapter::parse_event_finalize()
{
	putEventType("TRACE FINI");
}

void FBTraceAdapter::parse_event_attach() {
	getEventAttId();

	bool create_db = getBoolean();
	putEventType(create_db ? "CREATE DATABASE" : "ATTACH DATABASE");

	getAttParams();

	ConnectedUser user;
	memcpy(&user.bid, fields[trace_event_user].data, type_lengths[dtype_blob]);
	user.attachment_ID = (SLONG)*fields[trace_event_att_id].data;
	{	
		WriteLockGuard lock(connected_users_lock, "FBTraceAdapter::parse_event_attach");
		connected_users.add(user);
	}

//	getError();
	getEventResult();
}

void FBTraceAdapter::parse_event_detach() {
	getEventAttId();
	getCachedAttUser();

	bool drop_db = getBoolean();
	putEventType(drop_db ? "DROP DATABASE" : "DETACH DATABASE");
}

void FBTraceAdapter::parse_event_transaction_start() {
	putEventType("START TRANSACTION");
	getEventAttId();
	getCachedAttUser();
	getTransId();
	getTransOpt();
//	getError();
	getEventResult();
}

void FBTraceAdapter::parse_event_transaction_end() {
	getEventAttId();
	getCachedAttUser();
	getTransId();

	bool commit = getBoolean();
	bool retain_context = getBoolean();
	putEventType(commit ? 
		retain_context ? "COMMIT RETAINING" : "COMMIT TRANSACTION" :
		retain_context ? "ROLLBACK RETAINING" : "ROLLBACK TRANSACTION");

	getPerfInfo();
//	getError();
	getEventResult();
}

void FBTraceAdapter::parse_event_set_context() {
	putEventType("SET CONTEXT");
	getEventAttId();
	getCachedAttUser();
	getTransId();
	getVarNs();
	getVarName();
	getVarValue();
}

void FBTraceAdapter::parse_event_proc_start(bool isFunction = false) {
	putEventType(isFunction ? "START FUNCTION" : "START PROCEDURE");
	getEventAttId();
	getCachedAttUser();
	getTransId();
	getProcName();
	getProcParams();
//	getError();
	getEventResult();
}

void FBTraceAdapter::parse_event_proc_finish(bool isFunction = false) {
	putEventType(isFunction ? "FINISH FUNCTION" : "FINISH PROCEDURE");
	getEventAttId();
	getCachedAttUser();
	getTransId();
	getProcName();
	getProcParams();
	if (isFunction)
		getFuncResult();
	getPerfInfo();
	getRowFetched();
//	getError();
	getEventResult();
}

void FBTraceAdapter::parse_event_dsql_prepare() {
	putEventType("PREPARE STATEMENT");
	getEventAttId();
	getCachedAttUser();
	getTransId();
	getStmtId();
	getStmtSql();
	getPerfTime();
	getStmtAccessPath();
//	getError();
	getEventResult();
}

void FBTraceAdapter::parse_event_dsql_execute_start() {
	putEventType("START EXECUTE STATEMENT");
	getEventAttId();
	getCachedAttUser();
	getTransId();
	getStmtId();
	getStmtParams();
//	getError();
	getEventResult();
}

void FBTraceAdapter::parse_event_dsql_execute_finish() {
	putEventType("FINISH EXECUTE STATEMENT");
	getEventAttId();
	getCachedAttUser();
	getTransId();
	getStmtId();
	getStmtParams();
	getPerfInfo();
	getRowFetched();
//	getError();
	getEventResult();
}

void FBTraceAdapter::parse_event_dsql_free() {
	getEventAttId();
	getCachedAttUser();
	getStmtId();
	
	align_ptr<USHORT>();
	USHORT option = *(USHORT*) bufferPtr;
	convert_endian<USHORT>(option);
	increment_buffer_ptr(sizeof(USHORT));
	putEventType(option == DSQL_drop ? "FREE STATEMENT" : "CLOSE CURSOR");
}

void FBTraceAdapter::parse_event_blr_compile() {
	putEventType("COMPILE BLR");
	getEventAttId();
	getCachedAttUser();
	getTransId();
	getStmtId();
	getBlrData();
	getPerfTime();
//	getError();
	getEventResult();
}

void FBTraceAdapter::parse_event_blr_execute() {
	putEventType("EXECUTE BLR");
	getEventAttId();
	getCachedAttUser();
	getTransId();
	getStmtId();
	getPerfInfo();
//	getError();
	getEventResult();
}

void FBTraceAdapter::parse_event_dyn_execute() {
	putEventType("EXECUTE DYN");
	getEventAttId();
	getCachedAttUser();
	getTransId();
	getDynData();
	getPerfTime();
//	getError();
	getEventResult();
}

void FBTraceAdapter::parse_event_service_attach() {
	putEventType("ATTACH SERVICE");
	getSvcId();
	getSvcParams();
	getEventResult();
}

void FBTraceAdapter::parse_event_service_start() {
	putEventType("START SERVICE");
	getSvcId();
	getSvcName();
	getSvcSwitches();
	getEventResult();
}

void FBTraceAdapter::parse_event_service_query() {
	putEventType("QUERY SERVICE");
	getSvcId();
	getSvcQuery();
	getEventResult();
}

void FBTraceAdapter::parse_event_service_detach() {
	putEventType("DETACH SERVICE");
	getSvcId();
	getEventResult();
}

void FBTraceAdapter::parse_event_auth_factor() {
	putEventType("VERIFY AUTH FACTOR");
	getAuthFactorUser();
	size_t type = getAuthFactorType();
	getAuthFactorData(type);
	getEventResult();
}

void FBTraceAdapter::parse_event_trigger_start() {
	putEventType("START TRIGGER");
	getEventAttId();
	getCachedAttUser();
	getTransId();
	getTrgName();
	getTrgAction();
//	getError();
	getEventResult();
}

void FBTraceAdapter::parse_event_trigger_finish() {
	putEventType("FINISH TRIGGER");
	getEventAttId();
	getCachedAttUser();
	getTransId();
	getTrgName();
	getTrgAction();
	getPerfInfo();
//	getError();
	getEventResult();
}

void FBTraceAdapter::parse_event_privilege_change() {
	/*
	align_ptr<USHORT>();
	USHORT type = *(USHORT*) bufferPtr;
	convert_endian<USHORT>(type);
	increment_buffer_ptr(sizeof(USHORT));
	
	const char* event_str;
	switch (type) {
		case PRIVILEGE_ADD:
			event_str = "ADD PRIVILEGE";
			break;
		case PRIVILEGE_DELETE:
			event_str = "DELETE PRIVILEGE";
			break;
		default:
			event_str = "UNKNOW PRIVILEGE CHANGE";
			fb_assert(false);
			break;
	}
	putEventType(event_str);

	getEventAttId();
	getTransId();
	getPrivilegeExecutor();
	getPrivilegeGrantor();
	getPrivilegeObject();
	getPrivilegeGrantee();
	getPrivilegePrivilege();
	getPrivilegeOption();
	getError();
	getEventResult();
	*/
}

void FBTraceAdapter::parse_event_mandatory_access() {
	/*
	putEventType("MANDATORY_ACCESS");
	getBlob(&fields[trace_security_decision]);
	getEventResult();
	*/
}

void FBTraceAdapter::parse_event_object_relabeling() {
	/*
	putEventType("OBJECT_RELABELING");
	int object_type = getObjectType();
	switch(object_type)
	{
		case obj_relation:
		case obj_view:
		case obj_procedure:
		case obj_generator:
		case obj_exception:
		case obj_domain:
		case obj_function:
			getBlob(&fields[trace_object_name]);
			break;
		case obj_field:
			getBlob(&fields[trace_object_name]);
			getBlob(&fields[trace_object_name]);
			break;
	}
	getBlob(&fields[trace_label]);
	getEventResult();
	*/
}

void FBTraceAdapter::parse_event_error() {
	getProcName();
	bool isWarning = getBoolean();
	putEventType(isWarning ? "WARNING" : "ERROR");
	getError();
}

size_t FBTraceAdapter::read_record(const Jrd::jrd_rel* relation, Jrd::Record* record)
{
	read_packet_from_file();
	if (!packetLength)
		return 0;
	clear_record_buffer();

	UCHAR packet_type = *bufferPtr;
	getCommonFields();

	switch (packet_type)
	{
		case ptInitialize:
			parse_event_initialize();
			break;
		case ptFinalize: 
			parse_event_finalize();
			break;
		case ptEventAttach:
			parse_event_attach();
			break;
		case ptEventDetach:
			parse_event_detach();
			break;
		case ptEventTransactionStart:
			parse_event_transaction_start();
			break;
		case ptEventTransactionEnd:
			parse_event_transaction_end();
			break;
		case ptEventSetContext:
			parse_event_set_context();
			break;
		case ptEventProcStart:
			parse_event_proc_start();
			break;
		case ptEventProcFinish:
			parse_event_proc_finish();
			break;
		case ptEventFuncStart:
			parse_event_proc_start(true);
			break;
		case ptEventFuncFinish:
			parse_event_proc_finish(true);
			break;
		case ptEventDsqlPrepare:
			parse_event_dsql_prepare();
			break;
		case ptEventDsqlExecuteStart:
			parse_event_dsql_execute_start();
			break;
		case ptEventDsqlExecuteFinish:
			parse_event_dsql_execute_finish();
			break;
		case ptEventDsqlFree:
			parse_event_dsql_free();
			break;
		case ptEventBlrCompile:
			parse_event_blr_compile();
			break;
		case ptEventBlrExecute:
			parse_event_blr_execute();
			break;
		case ptEventDynExecute:
			parse_event_dyn_execute();
			break;
		case ptEventServiceAttach:
			parse_event_service_attach();
			break;
		case ptEventServiceStart:
			parse_event_service_start();
			break;
		case ptEventServiceQuery:
			parse_event_service_query();
			break;
		case ptEventServiceDetach:
			parse_event_service_detach();
			break;
		case ptEventAuthFactor:
			parse_event_auth_factor();
			break;
		case ptEventTriggerStart:
			parse_event_trigger_start();
			break;
		case ptEventTriggerFinish:
			parse_event_trigger_finish();
			break;
		case ptEventPrivilegeChange:
			parse_event_privilege_change();
			break;
		case ptEventMandatoryAccess:
			parse_event_mandatory_access();
			break;
		case ptEventObjectRelabeling:
			parse_event_object_relabeling();
			break;
		case ptEventError:
			parse_event_error();
			break;
		default:
			ERR_post(Arg::Gds(isc_bad_ext_record) << Arg::Str(file_name) <<
					 Arg::Num(FTELL(file) - packetLength));
			break;
	}

	if (!fields_are_mapped)
		mapFields(relation);

	const Jrd::Format* format = record->getFormat();
	const SSHORT offset = (SSHORT) (IPTR) format->fmt_desc[0].dsc_address;
	UCHAR *p = record->getData() + offset;
	SSHORT l = record->getLength() - offset;
	memset(p, NULL_SMBL, l);

	for (SSHORT i = 0; i < format->fmt_count; ++i)
	{
		p = record->getData() + (IPTR) format->fmt_desc[i].dsc_address;
		
		if (fields[relation_fields_map[i]].isNull)
			record->setNull(i);
		else {
			memcpy(p, fields[relation_fields_map[i]].data, fields[relation_fields_map[i]].field_length);
			record->clearNull(i);
		}
	}

	return packetLength;
}

const AdapterField* FBTraceAdapter::get_adapter_fields()
{
	return &fbtraceFields[0];
}

void FBTraceAdapter::get_blob_data(Jrd::blb* blob)
{
	const blob_id* id = reinterpret_cast<const blob_id*>(&blob->getBlobId()->bid_internal);
	FB_UINT64 offset = id->bid_decode();
	UCHAR blob_type = id->blob_type;
	blob->blb_sub_type = (blob_type == blb_blr_data || blob_type == blb_dyn_data) ?
		isc_blob_blr : isc_blob_text;

	aseek(offset, SEEK_SET);
	ntrace_size_t data_length = 0;
	USHORT blob_length = 0;
	HalfStaticArray<UCHAR, BUFFER_MEDIUM> field_data_buf;
	if (blob_type != blb_params && 
		blob_type != blb_svc_query && 
		blob_type != blb_perf_info &&
		blob_type != blb_auth_factor_type) 
	{
		aread(&data_length, sizeof(ntrace_size_t), 1);
		convert_endian<ntrace_size_t>(data_length);
		verify_capacity(data_length, string::max_length());
		aread(field_data_buf.getBuffer(data_length), data_length, 1);
	}
	UCHAR* field_data = field_data_buf.begin();
	UCHAR* p = blob->getBuffer() + 2;
	max_blob_size = blob->getClumpSize() - 2;

	switch (blob_type) {
		case blb_text:
			if (*field_data == 0xa) 
				// Blob doesn't processed correctly if its first symbol - 0xa
				blob_length = datacpy(p, field_data + 1, data_length - 1);
			else
				blob_length = datacpy(p, field_data, data_length);
			break;
		case blb_event_user_dpb:
		case blb_event_protocol_dpb:
		case blb_event_hostname_dpb:
		case blb_event_hw_address_dpb:
			blob_length = addDBAttachementInfo(data_length, field_data, 
				blob_type, p);
			break;
		case blb_event_user_spb:
		case blb_event_protocol_spb:
		case blb_event_hostname_spb:
		case blb_event_database_spb:
		case blb_event_hw_address_spb:
			blob_length = addSvcAttachementInfo(data_length, field_data, 
				blob_type, p);
			break;
		case blb_trans_opt:
			blob_length = addTransactionInfo(data_length, field_data, p);
			break;
		case blb_blr_data:
		case blb_dyn_data:
			blob_length = datacpy(p, field_data, data_length);
			break;
		case blb_params:
			blob_length = addParamsInfo(p);
			break;
		case blb_svc_switches:
			blob_length = addSvcSwitchesInfo(data_length, field_data, p);
			break;
		case blb_svc_query:
			blob_length = addSvcQueryInfo(p);
			break;
		case blb_perf_info:
			blob_length = addPerfInfo(p);
			break;
		case blb_auth_factor_type:
			blob_length = addFactorTypeInfo(p);
			break;
		case blb_auth_factor_data:
			blob_length = addPasswordInfo(data_length, field_data, p);
			break;
		case blb_privilege:
			blob_length = addPrivilegeInfo(data_length, field_data, p);
			break;
		default:
			ERR_post(Arg::Gds(isc_bad_ext_record) << Arg::Str(file_name) <<
					 Arg::Num(FTELL(file) - packetLength));
			break;
	}
	blob->blb_length = blob_length;
	memcpy(blob->getBuffer(), &blob_length, sizeof(USHORT));
}

int FBTraceAdapter::write_record(const Jrd::Record* record)
{
	return 0;
}

void FBTraceAdapter::mapFields(const class Jrd::jrd_rel* relation)
{
	// fill relation with fields' data
	USHORT i = 0;
	for (Jrd::vec<Jrd::jrd_fld*>::iterator itr = relation->rel_fields->begin();
			*itr; ++itr, ++i)
	{
		const Jrd::jrd_fld* field = *itr;
		
		for (const AdapterField* adp_field = fbtraceFields; 
				adp_field->fld_name; adp_field++) 
		{
			if (!strcmp(field->fld_name.c_str(), adp_field->fld_name))
			{
				relation_fields_map[i] = adp_field->fld_id;
				if (adp_field->fld_id == trace_event_object_id
					|| adp_field->fld_id == trace_stmt_id 
					|| adp_field->fld_id == trace_svc_id)
				{
					fields[adp_field->fld_id].field_length = log_pointer_size * 2 + sizeof(USHORT);
				}
				break;
			}
		}
	}

	fields_are_mapped = true;
}