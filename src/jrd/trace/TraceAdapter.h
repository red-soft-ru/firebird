/*
 *	PROGRAM:	JRD Access Method
 *	MODULE:		TraceAdapter.h
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

#ifndef JRD_TRACE_ADAPTER_H
#define JRD_TRACE_ADAPTER_H

#include <stdio.h>
#include "../common/classes/array.h"
#include "../jrd/ntrace.h"
#include "../../common/classes/rwlock.h"
#include "../../common/classes/locks.h"
#include "../../common/classes/tree.h"
#include "../common/classes/MetaName.h"
#include "../jrd/align.h"
#include "../jrd/Adapter.h"

enum {
	blb_text = 0,
	blb_event_user_dpb,
	blb_event_protocol_dpb,
	blb_event_hostname_dpb,
	blb_event_hw_address_dpb,
	blb_trans_opt,
	blb_blr_data,
	blb_dyn_data,
	blb_params,
	blb_event_user_spb,
	blb_event_protocol_spb,
	blb_event_hostname_spb,
	blb_event_hw_address_spb,
	blb_event_database_spb,
	blb_svc_switches,
	blb_svc_query,
	blb_perf_info,
	blb_auth_factor_type,
	blb_auth_factor_data,
	blb_privilege
};

static const TEXT* rel_counters_names[] = 
{
	"natural", 
	"indexed", 
	"updates", 
	"inserts", 
	"deletes", 
	"backouts", 
	"purges", 
	"expunges", 
	0
};

#define FBTRACE_EVENT_TYPE_LENGTH 24
#ifdef WORDS_BIGENDIAN
#define blb_type_offset 1
#else
#define blb_type_offset 2
#endif

enum {
	trace_event_type = 0,
	trace_event_time,
	trace_event_process_id,
	trace_event_object_id,
	trace_event_att_id,
	trace_event_database,
	trace_event_user,
	trace_event_protocol,
	trace_event_hostname,
	trace_event_hw_address,
	trace_auth_factor_type,
	trace_auth_factor_data,
	trace_trans_id,
	trace_trans_opt,
	trace_stmt_id,
	trace_stmt_sql,
	trace_blr_data,
	trace_dyn_data,
	trace_stmt_access_path,
	trace_stmt_params,
	trace_var_name,
	trace_var_ns,
	trace_var_value,
	trace_privilege_grantor,
	trace_privilege_grantee,
	trace_privilege_privilege,
	trace_privilege_object,
	trace_privilege_option,
	trace_svc_id,
	trace_svc_name,
	trace_svc_switches,
	trace_svc_query,
	trace_proc_name,
	trace_proc_params,
	trace_func_result,
	trace_trg_name,
	trace_trg_action,
	trace_perf_time,
	trace_perf_info,
	trace_row_fetched,
	trace_error_message,
//	trace_error_stacktrace,
	trace_event_result,
/*
	trace_security_decision,
	trace_object_type,
	trace_object_name,
	trace_field_name,
	trace_label,
*/
	trace_flds_count
};

struct blob_id{
#ifdef WORDS_BIGENDIAN
	UCHAR bid_number_up;
	UCHAR blob_type;
	USHORT relation_id;
	ULONG bid_number;
#else
	USHORT relation_id;
	UCHAR blob_type;
	UCHAR bid_number_up;
	ULONG bid_number;
#endif

	inline void bid_encode(FB_UINT64 value)
	{
		// Use explicit casts to suppress 64-bit truncation warnings
		// Store lower 32 bits of number
		bid_number = static_cast<ULONG>(value);
		// Store high 8 bits of number
		bid_number_up = static_cast<UCHAR>(value >> 32);
	}

	inline FB_UINT64 bid_decode() const
	{
		return bid_number + (static_cast<FB_UINT64>(bid_number_up) << 32);
	}
};

static const class AdapterField fbtraceFields[] = {
	//id						name				dtype				length						scale	sub_type	fld_precision character_length not_nullable
	{trace_event_type,			"EVENT_TYPE",		dtype_text,			FBTRACE_EVENT_TYPE_LENGTH,		0,	0,					0,	FBTRACE_EVENT_TYPE_LENGTH ,	1},	// 24
	{trace_event_time,			"EVENT_TIME",		dtype_timestamp,	type_lengths[dtype_timestamp],	0,	0,					0,	0,	1},	// 8
	{trace_event_process_id,	"EVENT_PROCESS_ID",	dtype_long,			type_lengths[dtype_long],		0,	0,					0,	0,	1},	// 4
	{trace_event_object_id,		"EVENT_OBJECT_ID",	dtype_varying,		sizeof(USHORT) + 16,			0,	0,					0,	16,	1},	// 18
	{trace_event_att_id,		"EVENT_ATT_ID",		dtype_int64,		type_lengths[dtype_int64],		0,	0,					0,	0,	0},	// 8
	{trace_event_database,		"EVENT_DATABASE",	dtype_blob,			type_lengths[dtype_blob],		0,	isc_blob_text,		0,	0,	0},	// 8
	{trace_event_user,			"EVENT_USER",		dtype_blob,			type_lengths[dtype_blob],		0,	isc_blob_text,		0,	0,	0},	// 8
	{trace_event_protocol,		"EVENT_PROTOCOL",	dtype_blob,			type_lengths[dtype_blob],		0,	isc_blob_text,		0,	0,	0},	// 8
	{trace_event_hostname,		"EVENT_HOSTNAME",	dtype_blob,			type_lengths[dtype_blob],		0,	isc_blob_text,		0,	0,	0},	// 8
	{trace_event_hw_address,	"EVENT_HW_ADDRESS",	dtype_blob,			type_lengths[dtype_blob],		0,	isc_blob_text,		0,	0,	0},	// 8
	{trace_auth_factor_type,	"AUTH_FACTOR_TYPE",	dtype_blob,			type_lengths[dtype_blob],		0,	isc_blob_text,		0,	0,	0},	// 8
	{trace_auth_factor_data,	"AUTH_FACTOR_DATA",	dtype_blob,			type_lengths[dtype_blob],		0,	isc_blob_text,		0,	0,	0},	// 8
	{trace_trans_id,			"TRANS_ID",			dtype_int64,		type_lengths[dtype_int64],		0,	0,					0,	0,	0},	// 8
	{trace_trans_opt,			"TRANS_OPT",		dtype_blob,			type_lengths[dtype_blob],		0,	isc_blob_text,		0,	0,	0},	// 8
	{trace_stmt_id,				"STMT_ID",			dtype_int64,		type_lengths[dtype_int64],		0,	0,					0,	0,	0},	// 8
	{trace_stmt_sql,			"STMT_SQL",			dtype_blob,			type_lengths[dtype_blob],		0,	isc_blob_text,		0,	0,	0},	// 8
	{trace_blr_data,			"BLR_DATA",			dtype_blob,			type_lengths[dtype_blob],		0,	isc_blob_blr,		0,	0,	0},	// 8
	{trace_dyn_data,			"DYN_DATA",			dtype_blob,			type_lengths[dtype_blob],		0,	isc_blob_dyn,		0,	0,	0},	// 8
	{trace_stmt_access_path,	"STMT_ACCESS_PATH",	dtype_blob,			type_lengths[dtype_blob],		0,	isc_blob_text,		0,	0,	0},	// 8
	{trace_stmt_params,			"STMT_PARAMS",		dtype_blob,			type_lengths[dtype_blob],		0,	isc_blob_text,		0,	0,	0},	// 8
	{trace_var_name,			"VAR_NAME",			dtype_blob,			type_lengths[dtype_blob],		0,	isc_blob_text,		0,	0,	0},	// 8
	{trace_var_ns,				"VAR_NS",			dtype_blob,			type_lengths[dtype_blob],		0,	isc_blob_text,		0,	0,	0},	// 8
	{trace_var_value,			"VAR_VALUE",		dtype_blob,			type_lengths[dtype_blob],		0,	isc_blob_text,		0,	0,	0},	// 8
	{trace_privilege_grantor,	"PRIVILEGE_GRANTOR",dtype_blob,			type_lengths[dtype_blob],		0,	isc_blob_text,		0,	0,	0},	// 8
	{trace_privilege_grantee,	"PRIVILEGE_GRANTEE",dtype_blob,			type_lengths[dtype_blob],		0,	isc_blob_text,		0,	0,	0},	// 8
	{trace_privilege_privilege,	"PRIVILEGE",		dtype_blob,			type_lengths[dtype_blob],		0,	isc_blob_text,		0,	0,	0},	// 8
	{trace_privilege_object,	"PRIVILEGE_OBJECT",	dtype_blob,			type_lengths[dtype_blob],		0,	isc_blob_text,		0,	0,	0},	// 8
	{trace_privilege_option,	"PRIVILEGE_OPTION",	dtype_long,			type_lengths[dtype_long],		0,	0,					0,	0,	0},	// 4
	{trace_svc_id,				"SVC_ID",			dtype_varying,		sizeof(USHORT) + 16,			0,	0,					0,	16,	0},	// 18
	{trace_svc_name,			"SVC_NAME",			dtype_blob,			type_lengths[dtype_blob],		0,	isc_blob_text,		0,	0,	0},	// 8
	{trace_svc_switches,		"SVC_SWITCHES",		dtype_blob,			type_lengths[dtype_blob],		0,	isc_blob_text,		0,	0,	0},	// 8
	{trace_svc_query,			"SVC_QUERY",		dtype_blob,			type_lengths[dtype_blob],		0,	isc_blob_text,		0,	0,	0},	// 8
	{trace_proc_name,			"PROC_NAME",		dtype_blob,			type_lengths[dtype_blob],		0,	isc_blob_text,		0,	0,	0},	// 8
	{trace_proc_params,			"PROC_PARAMS",		dtype_blob,			type_lengths[dtype_blob],		0,	isc_blob_text,		0,	0,	0},	// 8
	{trace_func_result,			"FUNC_RESULT",		dtype_blob,			type_lengths[dtype_blob],		0,	isc_blob_text,		0,	0,	0},	// 8
	{trace_trg_name,			"TRIG_NAME",		dtype_blob,			type_lengths[dtype_blob],		0,	isc_blob_text,		0,	0,	0},	// 8
	{trace_trg_action,			"TRIG_ACTION",		dtype_blob,			type_lengths[dtype_blob],		0,	isc_blob_text,		0,	0,	0},	// 8
	{trace_perf_time,			"PERF_TIME",		dtype_int64,		type_lengths[dtype_int64],		0,	0,					0,	0,	0},	// 8
	{trace_perf_info,			"PERF_INFO",		dtype_blob,			type_lengths[dtype_blob],		0,	isc_blob_text,		0,	0,	0},	// 8
	{trace_row_fetched,			"ROW_FETCHED",		dtype_int64,		type_lengths[dtype_int64],		0,	0,					0,	0,	0},	// 8
	{trace_error_message,		"ERROR_MESSAGE",	dtype_blob,			type_lengths[dtype_blob],		0,	isc_blob_text,		0,	0,	0},	// 8
//	{trace_error_stacktrace,	"ERROR_STACKTRACE",	dtype_blob,			type_lengths[dtype_blob],		0,	isc_blob_text,		0,	0,	0},	// 8
	{trace_event_result,		"EVENT_RESULT",		dtype_varying,		sizeof(USHORT) + 12,			0,	0,					0,	12,	0},	// 14
/*
	{trace_security_decision,	"SECURITY_DECISION",dtype_blob,			type_lengths[dtype_blob],		0,	isc_blob_text,		0,	0,	0},	// 8
	{trace_object_type,			"OBJECT_TYPE",		dtype_varying,		sizeof(USHORT) + 16,			0,	0,					0,	16,	0},	// 18
	{trace_object_name,			"OBJECT_NAME",		dtype_blob,			type_lengths[dtype_blob],		0,	isc_blob_text,		0,	0,	0},	// 8
	{trace_field_name,			"FIELD_NAME",		dtype_blob,			type_lengths[dtype_blob],		0,	isc_blob_text,		0,	0,	0},	// 8
	{trace_label,				"LABEL",			dtype_blob,			type_lengths[dtype_blob],		0,	isc_blob_text,		0,	0,	0},	// 8
*/
	{trace_flds_count,			NULL,				0,					0,								0,	0,					0,	0,	0}	//
};

static UCHAR DataBuffer[BUFFER_MEDIUM];

namespace Jrd {
	class jrd_rel;
	class Record;
	class blb;
}

class FBTraceAdapter : public BaseAdapter {
public:
	FBTraceAdapter(USHORT rel_id, const char* filename);
	~FBTraceAdapter();
	virtual void set_file_handle(FILE* file_handle);
	virtual size_t read_record(const class Jrd::jrd_rel* relation, class Jrd::Record* record);
	virtual int write_record(const class Jrd::Record* record);
	virtual void get_blob_data(class Jrd::blb* blob);
	virtual const AdapterField* get_adapter_fields();

private:
	struct FieldData {
		bool isNull;
		UCHAR* data;
		USHORT field_length;
	};
	FieldData fields[trace_flds_count];

	struct ConnectedUser {
		SLONG attachment_ID;
		blob_id bid;

		static const SLONG& generate(const void* /*sender*/, const ConnectedUser& item)
		{
			return item.attachment_ID;
		}
	};
	typedef Firebird::BePlusTree<ConnectedUser, SLONG, Firebird::MemoryPool, ConnectedUser>
		ConnectedUsersTree;
	ConnectedUsersTree connected_users;
	Firebird::RWLock connected_users_lock;
	
	SINT64 relation_fields_map[trace_flds_count];
	bool fields_are_mapped;

	virtual void check_file_format();
	void parse_log_header();
	USHORT datacpy(UCHAR* to, const UCHAR* from, size_t length);

	// Read / seek file with error processing
	size_t aread(void* buffer, size_t size, size_t count);
	void aseek(FB_UINT64 offset, int origin);

	void read_packet_type(USHORT val);

	// Methods for parsing log packets of different types
	void parse_event_initialize();
	void parse_event_finalize();
	void parse_event_attach();
	void parse_event_detach();
	void parse_event_transaction_start();
	void parse_event_transaction_end();
	void parse_event_set_context();
	void parse_event_proc_start(bool isFunction);
	void parse_event_proc_finish(bool isFunction);
	void parse_event_dsql_prepare();
	void parse_event_dsql_execute_start();
	void parse_event_dsql_execute_finish();
	void parse_event_dsql_free();
	void parse_event_blr_compile();
	void parse_event_blr_execute();
	void parse_event_dyn_execute();
	void parse_event_service_attach();
	void parse_event_service_start();
	void parse_event_service_query();
	void parse_event_service_detach();
	void parse_event_auth_factor();
	void parse_event_trigger_start();
	void parse_event_trigger_finish();
	void parse_event_privilege_change();
	void parse_event_mandatory_access();
	void parse_event_object_relabeling();
	void parse_event_error();

	void read_packet_from_file();
	void growBuffer(size_t requiredSize);
	void clear_record_buffer();
	size_t current_pos(){ return bufferPtr - buffer; };
	template <typename T>
		void align_ptr();
	template <typename T>
		void convert_endian(T& number);
	void increment_buffer_ptr(size_t step);
	void verify_capacity(size_t given_size, size_t max_size);

	// Extract data from fields of packet
	void getCommonFields();

	void getEventTime();
	void getEventProcessId();
	void getEventObjectId();
	void getEventAttId();
	void getEventDatabase();
	void getAuthFactorUser();
	size_t getAuthFactorType();
	void getAuthFactorData(size_t factor_type);
	void getAttParams();
	void getTransId();
	void getTransOpt();
	void getStmtId();
	void getStmtSql();
	void getBlrData();
	void getDynData();
	void getStmtAccessPath();
	void getStmtParams();
	void getVarName();
	void getVarNs();
	void getVarValue();
	void getPrivilegeExecutor();
	void getPrivilegeGrantor();
	void getPrivilegeObject();
	void getPrivilegeGrantee();
	void getPrivilegePrivilege();
	void getPrivilegeOption();
	void getSvcId();
	void getSvcName();
	void getSvcParams();
	void getSvcSwitches();
	void getSvcQuery();
	void getProcName();
	void getProcParams();
	void getFuncResult();
	void getTrgName();
	void getTrgAction();
	void getPerfTime();
	void getPerfInfo();
	void getRowFetched();
	void getError();
	void getEventResult();
	void getCachedAttUser();
	int getObjectType();

	// Read data of particular type
	void getVarying(FieldData* field);
	void getTimeStamp(FieldData* field);
	void getLong(FieldData* field);
	void getInt64(FieldData* field);
	bool getBoolean();
	void getCounter(FieldData* field);
	void getPointer(FieldData* field);
	void getBlob(FieldData* field);
	void getParameters(FieldData* field);
	void getCounters(FieldData* field);

	void putEventType(const char* event_type);

	ULONG addDBAttachementInfo(size_t length, const UCHAR* field_data, UCHAR blob_type, UCHAR* dest);
	ULONG addSvcAttachementInfo(size_t length, const UCHAR* field_data, UCHAR blob_type, UCHAR* dest);
	ULONG addTransactionInfo(size_t length, const UCHAR* field_data, UCHAR* dest);
	ULONG addParamsInfo(UCHAR* dest);
	ULONG addSvcSwitchesInfo(size_t length, const UCHAR* field_data, UCHAR* dest);
	ULONG addSvcQueryInfo(UCHAR* dest);
	ULONG addPerfInfo(UCHAR* dest);
	ULONG addFactorTypeInfo(UCHAR* dest);
	ULONG addPasswordInfo(size_t length, const UCHAR* field_data, UCHAR* dest);
	ULONG addPrivilegeInfo(size_t length, const UCHAR* field_data, UCHAR* dest);

	void mapFields(const class Jrd::jrd_rel* relation);
	
	// Platform dependent
	USHORT log_pointer_size;
	bool log_bigendian;

	// True if log was created with other endian architecture
	bool needEndianConvert;
	USHORT rel_id;
	USHORT max_blob_size;
	Firebird::PathName file_name;
	FILE* file;
	
	UCHAR inlineBuffer[1024];
	UCHAR recordBuffer[512];
	UCHAR* bufferPtr;
	UCHAR* buffer;
	UCHAR* recordPtr;
	size_t buffSize;
	ntrace_size_t packetLength;
};

# endif // JRD_TRACE_ADAPTER_H