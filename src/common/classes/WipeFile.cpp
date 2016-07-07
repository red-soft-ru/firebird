/*
 *	PROGRAM:	Client/Server Common Code
 *	MODULE:		WipeFile.cpp
 *	DESCRIPTION:	Wipe and delete file
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
 *   <dmitry.starodubov (at) red-soft.biz> for Red Soft Corporation.
 *
 *  Copyright (c) 2010 Red Soft Corporation <info (at) red-soft.biz>
 *  and all contributors signed below.
 *
 *  All Rights Reserved.
 *  Contributor(s): ______________________________________.
 *
 */

#include "WipeFile.h"
#include <time.h>
#include "../../common/config/config.h"

using namespace Firebird;

#ifdef WIN_NT
#include <windows.h>

#define SLASH '\\'
#ifndef MAXPATHLEN
#define MAXPATHLEN MAX_PATH
#endif

typedef UINT NTSTATUS;

// File System Control commands related to defragging
#define FSCTL_GET_VOLUME_INFORMATION	0x90064
#define	FSCTL_READ_MFT_RECORD			0x90068
#define FSCTL_GET_VOLUME_BITMAP			0x9006F
#define FSCTL_GET_RETRIEVAL_POINTERS	0x90073
#define FSCTL_MOVE_FILE					0x90074

// Error codes returned by NtFsControlFile (see NTSTATUS.H)
#define STATUS_SUCCESS			         ((NTSTATUS)0x00000000L)
#define STATUS_BUFFER_OVERFLOW           ((NTSTATUS)0x80000005L)
#define STATUS_INVALID_PARAMETER         ((NTSTATUS)0xC000000DL)
#define STATUS_BUFFER_TOO_SMALL          ((NTSTATUS)0xC0000023L)
#define STATUS_ACCESS_DENIED	         ((NTSTATUS)0xC0000011L)
#define STATUS_ALREADY_COMMITTED         ((NTSTATUS)0xC0000021L)
#define STATUS_INVALID_DEVICE_REQUEST    ((NTSTATUS)0xC0000010L)

typedef struct {
	ULONGLONG			Vcn;
	ULONGLONG			Lcn;
} MAPPING_PAIR, *PMAPPING_PAIR;

typedef struct {
	ULONG				NumberOfPairs;
	ULONGLONG			StartVcn;
	MAPPING_PAIR		Pair[1];
} GET_RETRIEVAL_DESCRIPTOR, *PGET_RETRIEVAL_DESCRIPTOR;

typedef struct _IO_STATUS_BLOCK {
	union {
		NTSTATUS  Status;
		PVOID  Pointer;
	};
	ULONG_PTR  Information;
} IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;


typedef void (*PIO_APC_ROUTINE) (
				PVOID ApcContext,
				PIO_STATUS_BLOCK IoStatusBlock,
				ULONG Reserved);
#else // WIN_NT
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#define SLASH '/'
#define MAXPATHLEN PATH_MAX
// default sector size
#define SECTOR_SIZE 512
// default buffer size -- 4MB
#define CHUNK_SIZE (4 << 20)
#ifdef O_SYNC
#define SYNC		O_SYNC
#endif
#if (!(defined SYNC) && (defined O_FSYNC))
#define SYNC		O_FSYNC
#endif
#ifdef O_DSYNC
#undef SYNC
#define SYNC		O_DSYNC
#endif
#ifndef SYNC
#define SYNC		0
#endif
#endif // WIN_NT


int do_unlink(const char* filename){
	if (MemoryPool::wipePasses > 0)
		return WipeFile(filename);
	else
		return unlink(filename);
}

void OverwriteFileName(const char* FileName, char* LastFileName)
{
	char		newName[MAXPATHLEN];
	char*		lastSlash;
	size_t		i, j, index;

	strcpy(LastFileName, FileName);
	lastSlash = strrchr(LastFileName, SLASH);
	index = (lastSlash - LastFileName) / sizeof(char);

	strcpy(newName, FileName);
	for (i = 0; i < 26; i++) {
		for (j = index+1 ; j < strlen(FileName); j++) {
			if(FileName[j] != '.')
				newName[j] = (char) i + 'A';
		}

#ifdef WIN_NT
		if(!MoveFile(LastFileName, newName)) {
			return;
		}
#else
		if(rename(LastFileName, newName)) {
			return;
		}
#endif

		strcpy(LastFileName, newName);
	}
}


void FillBuffer(UCHAR **buffer, size_t bufSize){
	// Seed the random number generator
	srand((unsigned)time(NULL));

	for (size_t i = 0; i < 3; i++) {
#ifdef WIN_NT
		buffer[i] = (UCHAR*) VirtualAlloc(NULL, bufSize, MEM_COMMIT, PAGE_READWRITE);
#else
		buffer[i] = new UCHAR[bufSize];
#endif
		// Fill each buffer with a different signature
		switch (i) {
		case 0:
			// fill with zero on non-Windows OS
#ifndef WIN_NT
			memset(buffer[i], 0, bufSize);
#endif
			break;
		case 1:
			// fill with complement of 0 - 0xFF
			memset(buffer[i], 0xFF, bufSize);
			break;
		case 2:
			// fill with a random value
			for(size_t j = 0; j < bufSize; j++)
				buffer[i][j] = (UCHAR) rand();
			break;
		}
	}
}


void FreeBuffer(UCHAR** buffer){
	for (size_t i = 0; i < 3; i++)
#ifdef WIN_NT
		VirtualFree(buffer[i], 0, MEM_RELEASE);
#else
		delete[] buffer[i];
#endif

}


#ifdef WIN_NT

typedef NTSTATUS (__stdcall *NtFsControlFileProc)(
		HANDLE FileHandle,
		HANDLE Event,
		PIO_APC_ROUTINE ApcRoutine,
		PVOID ApcContext,
		PIO_STATUS_BLOCK IoStatusBlock,
		ULONG FsControlCode,
		PVOID InputBuffer,
		ULONG InputBufferLength,
		PVOID OutputBuffer,
		ULONG OutputBufferLength
);
NtFsControlFileProc NtFsControlFile;

#define LLINVALID		((ULONGLONG) -1)
#define	FILEMAPSIZE		(16384+2)

void LocateNativeEntryPoints()
{
	// If we're on Win9x, just return
	if (GetVersion() >= 0x80000000)
		return;

	// Load the NTDLL entry point we need
	if( !(NtFsControlFile = (NtFsControlFileProc) GetProcAddress(GetModuleHandle("ntdll.dll"),
			"NtFsControlFile"))) {
		fatal_exception::raiseFmt("Could not find NtFsControlFile entry point in NTDLL.DLL");
	}
}


bool SecureOverwrite(HANDLE FileHandle, ULONGLONG Length)
{
#define CLEANBUFSIZE 65536
	UCHAR*		cleanBuffer[3];
	int		i, passes;
	ULONGLONG	totalWritten;
	ULONG		bytesWritten, bytesToWrite;
	LONG		seekLength;
	bool		status;

	FillBuffer(cleanBuffer, CLEANBUFSIZE);

	// Do the overwrite
	seekLength = (LONG) Length;
	for (passes = 0; passes < MemoryPool::wipePasses; passes++) {
		if (passes != 0) {
			SetFilePointer(FileHandle, -seekLength, NULL, FILE_CURRENT);
		}
		for (i = 0; i < 3; i++) {

			// Move back to the start of where we're overwriting
			if (i != 0) {
				SetFilePointer(FileHandle, -seekLength, NULL, FILE_CURRENT);
			}

			// Loop and overwrite
			totalWritten = 0;
			while (totalWritten < Length) {
				if (Length - totalWritten > 1024*1024) {
					bytesToWrite = 1024*1024;
				}
				else {
					bytesToWrite = (ULONG) (Length - totalWritten );
				}
				if (bytesToWrite > CLEANBUFSIZE)
					bytesToWrite = CLEANBUFSIZE;

				status = WriteFile(FileHandle, cleanBuffer[i], bytesToWrite, &bytesWritten, NULL);
				if (!status) {
					FreeBuffer(cleanBuffer);
					return false;
				}

				// Note: no need to flush since the file is opened with write-through or
				// no cache buffering
				totalWritten += bytesWritten;
			}
		}
	}

	FreeBuffer(cleanBuffer);

	return true;
}


bool ScanFile(HANDLE VolumeHandle,
			  DWORD ClusterSize,
			  HANDLE FileHandle,
			  bool& ReallyCompressed,
			  bool& ZappedFile)
{
	DWORD						status;
	int							i;
	IO_STATUS_BLOCK				ioStatus;
	ULONGLONG					startVcn, prevVcn;
	LARGE_INTEGER				clusterOffset;
	ULONGLONG					endOfPrevRun;
	PGET_RETRIEVAL_DESCRIPTOR	fileMappings;
	ULONGLONG					fileMap[FILEMAPSIZE];
	int							lines = 0;

	// Assume file is in an MFT record.
	ReallyCompressed = false;
	ZappedFile = false;

	startVcn = 0;
	endOfPrevRun = LLINVALID;
	fileMappings = (PGET_RETRIEVAL_DESCRIPTOR) fileMap;
	while (!(status = NtFsControlFile(FileHandle, NULL, NULL, 0, &ioStatus,
						FSCTL_GET_RETRIEVAL_POINTERS, &startVcn, sizeof(startVcn),
						fileMappings, FILEMAPSIZE * sizeof(ULONGLONG))) ||
			status == STATUS_BUFFER_OVERFLOW ||
			status == STATUS_PENDING )
	{
		// If the operation is pending, wait for it to finish
		if (status == STATUS_PENDING) {
			WaitForSingleObject(FileHandle, INFINITE );

			// Get the status from the status block
			if (ioStatus.Status != STATUS_SUCCESS &&
				ioStatus.Status != STATUS_BUFFER_OVERFLOW)
			{
				return ioStatus.Status == STATUS_SUCCESS;
			}
		}

		// Loop through the buffer of number/cluster pairs, printing them out.
		startVcn = fileMappings->StartVcn;
		prevVcn  = fileMappings->StartVcn;
		for (i = 0; i < (ULONGLONG) fileMappings->NumberOfPairs; i++) {
			// On NT 4.0, a compressed virtual run (0-filled) is
			// identified with a cluster offset of -1
			if (fileMappings->Pair[i].Lcn != LLINVALID) {
				// Its compressed and outside the zone
				ReallyCompressed = true;

				// Overwrite the clusters if we were able to open the volume
				// for write access.
				if (VolumeHandle != INVALID_HANDLE_VALUE) {
					clusterOffset.QuadPart = fileMappings->Pair[i].Lcn * ClusterSize;
					SetFilePointer(VolumeHandle, clusterOffset.LowPart,
						&clusterOffset.HighPart, FILE_BEGIN);
					if (!SecureOverwrite(VolumeHandle,
						ClusterSize * (DWORD) (fileMappings->Pair[i].Vcn - startVcn))) {

						// Couldn't zap the clusters, so we'll have to clean the free space
						return true;
					}
				}
				else
					return true;
			}
			startVcn = fileMappings->Pair[i].Vcn;
		}

		// If the buffer wasn't overflowed, then we're done
		if (!status)
			break;
	}

	// If we made through with no errors we've overwritten all the file's clusters.
	if (status == STATUS_SUCCESS )
		ZappedFile = true;

	return status == STATUS_SUCCESS;
}


bool SecureDeleteCompressed(const char* FileName) {
	bool			reallyCompressed = false;
	bool			zappedFile = false;
	char			lastFileName[MAX_PATH];
	static char		volumeName[] = "\\\\.\\A:";
	static char		volumeRoot[] = "A:\\";
	HANDLE			hFile;
	static HANDLE	hVolume = INVALID_HANDLE_VALUE;
	static DWORD	clusterSize;
	DWORD			sectorsPerCluster, bytesPerSector, freeClusters, totalClusters;

	// Open the volume name
	volumeName[4] = FileName[0];
	hVolume = CreateFile(volumeName, GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
		0, 0);

	volumeRoot[0] = FileName[0];
	GetDiskFreeSpace(volumeRoot, &sectorsPerCluster, &bytesPerSector,
		&freeClusters, &totalClusters);

	clusterSize = bytesPerSector * sectorsPerCluster;

	hFile = CreateFile(FileName, GENERIC_READ,
						0,NULL, OPEN_EXISTING, 0, NULL);

	// Scan the location of the file
	if (!ScanFile(hVolume, clusterSize, hFile, reallyCompressed, zappedFile)) {
		CloseHandle(hFile);
		return true;
	}

	CloseHandle(hFile);

	// If the file is really compressed (it is non-resident),
	// we can delete it now.
	if (reallyCompressed) {
		// Rename the file a few times
		OverwriteFileName(FileName, lastFileName);

		if (!DeleteFile(lastFileName)) {
			DWORD error = GetLastError();
			// Rename back to the original name on error
			if (!MoveFile( lastFileName, FileName ))
				fatal_exception::raiseFmt("IO error (%d) deleting file: %s.\nFile is left as %s",
					error,	FileName, lastFileName);
			else
				fatal_exception::raiseFmt("IO error (%d) deleting file: %s",
					error,	FileName);
			return true;
		}
	}

	// Return TRUE if the file had clusters outside the MFT
	return reallyCompressed;
}


void SecureDelete(const char* FileName)
{
	HANDLE	hFile;
	ULONGLONG bytesToWrite, bytesWritten;
	ULARGE_INTEGER fileLength;
	DWORD dwSizeHigh = 0;
	DWORD dwSizeLow = 0;
	char   lastFileName[MAX_PATH];

	// First, open the file in overwrite mode
	hFile = CreateFile(FileName, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
					   NULL, OPEN_EXISTING, FILE_FLAG_WRITE_THROUGH, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		fatal_exception::raiseFmt("IO error (%d) opening file: %s",
			GetLastError(),	FileName);
	}

	dwSizeLow = GetFileSize(hFile, &dwSizeHigh);

	// If the file has a non-zero length, fill it with 0's first in order
	// to preserve is cluster allocation.
	if (dwSizeLow || dwSizeHigh) {

		// Seek to the last byte of the file
		dwSizeLow--;
		if (dwSizeLow == (DWORD) -1 && dwSizeHigh)
			dwSizeHigh--;
		SetFilePointer(hFile, dwSizeLow, (PLONG) &dwSizeHigh, FILE_BEGIN);

		// Write one zero byte, which causes the file system to fill the entire
		// file's on-disk contents with 0.
		if (!SecureOverwrite(hFile, 1)) {
			fatal_exception::raiseFmt("IO error (%d) overwriting file: %s",
				GetLastError(),	FileName);
			CloseHandle(hFile);
			return;
		}

		// Now go back to the start of the file and overwrite the rest of the
		// file.
		SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
		fileLength.LowPart = dwSizeLow;
		fileLength.HighPart = dwSizeHigh;
		bytesWritten = 0;
		while (bytesWritten < fileLength.QuadPart ) {
			int differ = fileLength.QuadPart - bytesWritten;
			bytesToWrite = differ < 65536 ? differ : 65536;
			if (!SecureOverwrite(hFile, (DWORD) bytesToWrite)) {
				fatal_exception::raiseFmt("IO error (%d) overwriting file: %s",
					GetLastError(),	FileName);
				CloseHandle(hFile);
				return;
			}
			bytesWritten += bytesToWrite;
		}
	}

	CloseHandle(hFile);

	// Rename the file a few times
	OverwriteFileName(FileName, lastFileName);

	// Delete the file
	if (!DeleteFile(lastFileName)) {
			DWORD error = GetLastError();
			// Rename back to the original name on error
			if (!MoveFile( lastFileName, FileName ))
				fatal_exception::raiseFmt("IO error (%d) deleting file: %s.\nFile is left as %s",
					error,	FileName, lastFileName);
			else
				fatal_exception::raiseFmt("IO error (%d) deleting file: %s",
					error,	FileName);
			return;
	}
}


int WipeFile(const char* filename) {
	PathName device;
	PathName file;
	USHORT dtype = 0;
	TEXT fullname[MAXPATHLEN];
	TEXT *p;

	// Get full path name
	const int l = GetFullPathName(filename, MAXPATHLEN, fullname, &p);
	if (l && l < MAXPATHLEN)
		file.assign(fullname);
	else
		file.assign(filename);

	LocateNativeEntryPoints();
	DWORD fileAttrs = GetFileAttributes(filename);
	if (fileAttrs == INVALID_FILE_ATTRIBUTES)
		fatal_exception::raiseFmt("IO error (%d) retrieving attributes for file: %s",
			GetLastError(),	filename);

	const size_t colon_pos = file.find(':');
	if (colon_pos != PathName::npos) {
		if (colon_pos == 1) {
			device = file.substr(0, 1) + ":\\";
			dtype = GetDriveType(device.c_str());
		}
	}

	if (dtype != DRIVE_REMOTE &&
		fileAttrs & (FILE_ATTRIBUTE_COMPRESSED |
					FILE_ATTRIBUTE_ENCRYPTED |
					FILE_ATTRIBUTE_SPARSE_FILE))
	{
		if (SecureDeleteCompressed(file.c_str()))
			return 0;
	}
	SecureDelete(file.c_str());
	return 0;
}

#else // not WIN_NT


void SyncFile(const int fd) {
#ifndef SYNC
#ifdef HAVE_FDATASYNC
	fdatasync(fd);
#elif defined(HAVE_FSYNC)
	fsync(fd);
#endif
#endif
}


void DoWrite(const char* FileName, const int fd, UCHAR *buf, size_t count) {
	UCHAR* ptr = buf;
	ssize_t c, written = 0;
	while (written < count)
	{
		c = write(fd, ptr, count - written);

		if (c > 0) // full or partial success
		{
			written += c;
			ptr += c;
		}
		else // failed, c <= 0
		{
			fatal_exception::raiseFmt("IO error (%d) writing file: %s", errno, FileName);
		}
	}
}


void SecureDelete(const char* FileName) {
	UCHAR*	buf[3];
	size_t	bufSize;
	FB_UINT64	fSize, loop, loopr;
	int		handle;
	off_t	nFileLen = 0;
	struct stat fileStat;
	size_t	passes;

	if (!stat(FileName, &fileStat))
		fSize = fileStat.st_size;
	else
		fatal_exception::raiseFmt("IO error (%d) file stat: %s", errno, FileName);

	bufSize = CHUNK_SIZE;

	if (fSize % fileStat.st_blksize)
		fSize += fileStat.st_blksize - (fSize % fileStat.st_blksize);
	else
		++fSize;

	if (fSize > CHUNK_SIZE)
	{
		loop = fSize / CHUNK_SIZE;
		loopr = fSize % CHUNK_SIZE;
	}
	else
	{
		loop = 1;
		loopr = 0;
		if (fSize)
			bufSize = fSize;
	}

	try	{
		FillBuffer(buf, bufSize);

		handle = open(FileName,  O_WRONLY | O_EXCL | SYNC);
		if (handle < 0)
			fatal_exception::raiseFmt("Error (%d) opening file: %s", errno, FileName);

		for (passes = 0; passes < MemoryPool::wipePasses; passes++) {
			for (size_t j = 0; j < 3; j++) {
				if (lseek(handle, 0, SEEK_SET) == (off_t) -1)
					fatal_exception::raiseFmt("IO error (%d) seeking file: %s",	errno, FileName);
				size_t i = 0;
				while (i++ < loop)
					DoWrite(FileName, handle, buf[j], bufSize);
				if (loopr)
					DoWrite(FileName, handle, buf[j], loopr);
				SyncFile(handle);
			}
		}
		if (handle >= 0)
			close(handle);
	}
	catch (const Firebird::Exception&) {
		FreeBuffer(buf);
		throw;
	}
	FreeBuffer(buf);
}


int WipeFile(const char* filename) {
	char   lastFileName[PATH_MAX];

	SecureDelete(filename);

	OverwriteFileName(filename, lastFileName);
	// Delete the file
	if (unlink(lastFileName)) {
		int error = errno;
		// Rename back to the original name on error
		if (rename(lastFileName, filename))
			fatal_exception::raiseFmt("IO error (%d) deleting file: %s.\nFile is left as %s",
				error,	filename, lastFileName);
		else
			fatal_exception::raiseFmt("IO error (%d) deleting file: %s",
				error,	filename);
	}
	return 0;
}
#endif // WIN_NT
