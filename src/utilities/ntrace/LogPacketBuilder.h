/*
 *	PROGRAM:	SQL Trace plugin
 *	MODULE:		LogPacketBuilder.h
 *	DESCRIPTION:	Packet builder for binary log format
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
 *  The Original Code was created by Nickolay Samofatov
 *  for the Firebird Open Source RDBMS project.
 *
 *  Copyright (c) 2004 Nickolay Samofatov <nickolay@broadviewsoftware.com>
 *  and all contributors signed below.
 *
 *  All Rights Reserved.
 *  Contributor(s): ______________________________________.
 *
 */

#ifndef LOGPACKETBUILDER_H
#define LOGPACKETBUILDER_H

#include "../../common/dsc.h"
#include "../../common/classes/array.h"
#include "../../jrd/ntrace.h"
#include "../../common/classes/timestamp.h"

enum PacketType {
	ptInitialize,
	ptFinalize,
	ptEventAttach,
	ptEventDetach,
	ptEventTransactionStart,
	ptEventTransactionEnd,
	ptEventSetContext,
	ptEventProcStart,
	ptEventProcFinish,
	ptEventDsqlPrepare,
	ptEventDsqlExecuteStart,
	ptEventDsqlExecuteFinish,
	ptEventDsqlFree,
	ptEventBlrCompile,
	ptEventBlrExecute,
	ptEventDynExecute,
	ptEventServiceAttach,
	ptEventServiceStart,
	ptEventServiceQuery,
	ptEventServiceDetach,
	ptEventAuthFactor,
	ptEventTriggerStart,
	ptEventTriggerFinish,
	ptEventMandatoryAccess,
	ptEventObjectRelabeling,
	ptEventPrivilegeChange,
	ptEventFuncStart,
	ptEventFuncFinish,
	ptEventError,
	ptEventSweep
};

// Put data with platform-specific endianness and pointer size
// Packet writing should be as fast as possible. This is the responsibility of
// reader to adjust for platform specifics of writer.
class LogPacketBuilder {
public:
	LogPacketBuilder(UCHAR packetType);
	~LogPacketBuilder();
	const UCHAR* getPacket();
	const ULONG getPacketSize() const;

	void putObjectId(SLONG processId, void* object);

	void putTimeStamp(ISC_TIMESTAMP stamp);
	void putConnectionId(ntrace_connection_t connection);
	void putTransactionId(ntrace_transaction_t transaction);
	void putRelationId(ntrace_relation_t relation);
	void putStatementId(ntrace_statement_t statement);
	void putServiceId(ntrace_service_t service);
	void putBytes(ntrace_size_t length, const void* buffer);
	void putBoolean(ntrace_boolean_t value);
	void putCounter(ntrace_counter_t value);
	void putUSHORT(USHORT value);
	void putOffset();
	void putDsc(const dsc* param);

	static const size_t DSC_PACKET_SIZE = sizeof(((dsc*)NULL)->dsc_dtype) + 
		sizeof(((dsc*)NULL)->dsc_scale) + sizeof(((dsc*)NULL)->dsc_length) +
		sizeof(((dsc*)NULL)->dsc_sub_type) + sizeof(((dsc*)NULL)->dsc_flags);

private:
	// Packet buffer management routines
	void growBuffer(size_t requiredSize);
	template <typename T>
		T* alloc(size_t count = 1);
	// Most packets should fit into 1K
	UCHAR inlineBuffer[1024];
	UCHAR* buffer;
	size_t buffSize;
	ntrace_size_t packetLength;
};

#endif
