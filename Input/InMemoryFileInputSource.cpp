/*
 *  Copyright (C) 2010, 2011, 2012, 2013 Stephen F. Booth <me@sbooth.org>
 *  All Rights Reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are
 *  met:
 *
 *    - Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    - Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *    - Neither the name of Stephen F. Booth nor the names of its 
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <fcntl.h>
#include <unistd.h>

#include "InMemoryFileInputSource.h"
#include "Logger.h"

#pragma mark Creation and Destruction

InMemoryFileInputSource::InMemoryFileInputSource(CFURLRef url)
	: InputSource(url), mMemory(nullptr), mCurrentPosition(nullptr)
{
	memset(&mFilestats, 0, sizeof(mFilestats));
}

InMemoryFileInputSource::~InMemoryFileInputSource()
{
	if(IsOpen())
		Close();
}

bool InMemoryFileInputSource::Open(CFErrorRef *error)
{
	if(IsOpen()) {
		LOGGER_WARNING("org.sbooth.AudioEngine.InputSource.InMemoryFile", "Open() called on an InputSource that is already open");
		return true;
	}

	UInt8 buf [PATH_MAX];
	Boolean success = CFURLGetFileSystemRepresentation(mURL, FALSE, buf, PATH_MAX);
	if(!success) {
		if(error)
			*error = CFErrorCreate(kCFAllocatorDefault, kCFErrorDomainPOSIX, EIO, nullptr);
		return false;
	}

	int fd = open(reinterpret_cast<const char *>(buf), O_RDONLY);
	if(-1 == fd) {
		if(error)
			*error = CFErrorCreate(kCFAllocatorDefault, kCFErrorDomainPOSIX, errno, nullptr);
		return false;
	}

	if(-1 == fstat(fd, &mFilestats)) {
		if(error)
			*error = CFErrorCreate(kCFAllocatorDefault, kCFErrorDomainPOSIX, errno, nullptr);

		if(-1 == close(fd))
			LOGGER_WARNING("org.sbooth.AudioEngine.InputSource.InMemoryFile", "Unable to close the file: " << strerror(errno));

		return false;
	}

	// Perform the allocation
	mMemory = static_cast<int8_t *>(malloc(mFilestats.st_size));
	if(nullptr == mMemory) {
		if(error)
			*error = CFErrorCreate(kCFAllocatorDefault, kCFErrorDomainPOSIX, errno, nullptr);

		if(-1 == close(fd))
			LOGGER_WARNING("org.sbooth.AudioEngine.InputSource.InMemoryFile", "Unable to close the file: " << strerror(errno));

		memset(&mFilestats, 0, sizeof(mFilestats));

		return false;
	}

	// Read the file
	if(-1 == read(fd, mMemory, mFilestats.st_size)) {
		if(error)
			*error = CFErrorCreate(kCFAllocatorDefault, kCFErrorDomainPOSIX, errno, nullptr);

		if(-1 == close(fd))
			LOGGER_WARNING("org.sbooth.AudioEngine.InputSource.InMemoryFile", "Unable to close the file: " << strerror(errno));

		memset(&mFilestats, 0, sizeof(mFilestats));
		free(mMemory), mMemory = nullptr;

		return false;
	}

	if(-1 == close(fd)) {
		LOGGER_WARNING("org.sbooth.AudioEngine.InputSource.InMemoryFile", "Unable to close the file: " << strerror(errno));

		if(error)
			*error = CFErrorCreate(kCFAllocatorDefault, kCFErrorDomainPOSIX, errno, nullptr);

		memset(&mFilestats, 0, sizeof(mFilestats));
		free(mMemory), mMemory = nullptr;

		return false;
	}

	mCurrentPosition = mMemory;

	mIsOpen = true;
	return true;
}

bool InMemoryFileInputSource::Close(CFErrorRef */*error*/)
{
	if(!IsOpen()) {
		LOGGER_WARNING("org.sbooth.AudioEngine.InputSource.InMemoryFile", "Close() called on an InputSource that hasn't been opened");
		return true;
	}

	memset(&mFilestats, 0, sizeof(mFilestats));

	if(mMemory)
		free(mMemory), mMemory = nullptr;

	mCurrentPosition = nullptr;

	mIsOpen = false;
	return true;
}

SInt64 InMemoryFileInputSource::Read(void *buffer, SInt64 byteCount)
{
	if(!IsOpen() || nullptr == buffer)
		return -1;

	ptrdiff_t remaining = (mMemory + mFilestats.st_size) - mCurrentPosition;

	if(byteCount > remaining)
		byteCount = remaining;

	memcpy(buffer, mCurrentPosition, byteCount);
	mCurrentPosition += byteCount;
	return byteCount;
}

bool InMemoryFileInputSource::SeekToOffset(SInt64 offset)
{
	if(!IsOpen() || offset > mFilestats.st_size)
		return false;
	
	mCurrentPosition = mMemory + offset;
	return true;
}
