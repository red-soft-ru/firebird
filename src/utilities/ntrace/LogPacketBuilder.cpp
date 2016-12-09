/*
 *	PROGRAM:	SQL Trace plugin
 *	MODULE:		LogPacketBuilder.cpp
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

#include "../../include/firebird.h"

#include "LogPacketBuilder.h"
#include "os/platform.h"


LogPacketBuilder::LogPacketBuilder(UCHAR packetType) : buffer(inlineBuffer), buffSize(sizeof(inlineBuffer)) 
{
	memset(inlineBuffer, 0, sizeof(inlineBuffer));
	buffer[0] = packetType;
	packetLength = 8; // Reserve space for packet length

	putTimeStamp(Firebird::TimeStamp::getCurrentTimeStamp().value());
	putObjectId(get_process_id(), this);
}

LogPacketBuilder::~LogPacketBuilder() {
	if (buffer != inlineBuffer)
		delete[] buffer;
}

const UCHAR* LogPacketBuilder::getPacket()
{
	// Adjust packet size in packet header
	*reinterpret_cast<ULONG*>(buffer + 4) = packetLength;
	return buffer;
}

const ULONG LogPacketBuilder::getPacketSize() const {
	return packetLength;
}

void LogPacketBuilder::growBuffer(ntrace_size_t requiredSize) {
	if (requiredSize < buffSize * 2)
		requiredSize = buffSize * 2;
	UCHAR* newBuffer = FB_NEW_POOL(*getDefaultMemoryPool()) UCHAR[requiredSize];
	memcpy(newBuffer, buffer, packetLength);
	// Zero-initialize the rest of buffer. Needed because we align various stuff before writing
	memset(newBuffer + packetLength, 0, requiredSize - packetLength);
	if (buffer != inlineBuffer)
		delete[] buffer;
	buffer = newBuffer;
	buffSize = requiredSize;
}

template <typename T>
inline T* LogPacketBuilder::alloc(ntrace_size_t count) {
	ntrace_size_t newStart = FB_ALIGN(packetLength, sizeof(T));
	ntrace_size_t newLength = newStart + sizeof(T) * count;
	if (newLength > buffSize)
		growBuffer(newLength);
	packetLength = newLength;
	return reinterpret_cast<T*>(buffer + newStart);
}

void LogPacketBuilder::putObjectId(SLONG processId, void* object) {
	*alloc<SLONG>() = processId;
	*alloc<void*>() = object;
}

void LogPacketBuilder::putTimeStamp(ISC_TIMESTAMP stamp) {
	*alloc<ISC_TIMESTAMP>() = stamp;
}

void LogPacketBuilder::putConnectionId(ntrace_connection_t connection) {
	*alloc<ntrace_connection_t>() = connection;
}

void LogPacketBuilder::putTransactionId(ntrace_transaction_t transaction) {
	*alloc<ntrace_transaction_t>() = transaction;
}

void LogPacketBuilder::putRelationId(ntrace_relation_t relation) {
	*alloc<ntrace_relation_t>() = relation;
}

void LogPacketBuilder::putStatementId(ntrace_statement_t statement) {
	*alloc<ntrace_statement_t>() = statement;
}

void LogPacketBuilder::putServiceId(ntrace_service_t service) {
	*alloc<ntrace_service_t>() = service;
}

void LogPacketBuilder::putBytes(ntrace_size_t length, const void* buffer) {
	*alloc<ntrace_size_t>() = length;
	char* dest = alloc<char>(length);
	memcpy(dest, buffer, length);
}

void LogPacketBuilder::putBoolean(ntrace_boolean_t value) {
	*alloc<ntrace_boolean_t>() = value;
}

void LogPacketBuilder::putCounter(ntrace_counter_t value) {
	*alloc<ntrace_counter_t>() = value;
}

void LogPacketBuilder::putUSHORT(USHORT value) {
	*alloc<USHORT>() = value;
}

void LogPacketBuilder::putOffset() {
	// packetLenght is changed inside alloc, so use "offset" var
	ntrace_size_t offset = FB_ALIGN(packetLength, sizeof(ntrace_size_t));
	*alloc<ntrace_size_t>() = offset;
}

void LogPacketBuilder::putDsc(const dsc* param) {
	char* dest = alloc<char>(DSC_PACKET_SIZE + param->dsc_length);

	*dest = param->dsc_dtype;
	dest += sizeof(param->dsc_dtype);

	*dest = param->dsc_scale;
	dest += sizeof(param->dsc_scale);

	*(USHORT*)dest = param->dsc_length;
	dest += sizeof(param->dsc_length);

	*(USHORT*)dest = param->dsc_flags;
	dest += sizeof(param->dsc_flags);

	*(SSHORT*)dest = param->dsc_sub_type;
	dest += sizeof(param->dsc_sub_type);

	memcpy(dest, param->dsc_address, param->dsc_length);
}

void LogPacketBuilder::putParams(Firebird::ITraceParams* params) {
	putOffset();
	if (!params)
	{
		putCounter(0);
		return;
	}

	const unsigned paramcount = params->getCount();
	putCounter(paramcount);
	for (unsigned i = 0; i < paramcount; i++)
		putDsc(params->getParam(i));
}