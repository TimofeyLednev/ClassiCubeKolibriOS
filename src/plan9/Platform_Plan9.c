#include "../Core.h"
#if defined CC_BUILD_PLAN9

#define CC_NO_UPDATER
#define CC_NO_DYNLIB
#define CC_NO_SOCKETS
#define CC_NO_THREADING
#define DEFAULT_COMMANDLINE_FUNC

/* Native Plan 9 (9front) platform layer.
   Uses the native libc from <u.h>/<libc.h> (kencc toolchain: 6c/8c + mk),
   not POSIX/APE. File and time primitives map onto Plan 9 system calls. */
#include <u.h>
#include <libc.h>

#include "../Stream.h"
#include "../ExtMath.h"
#include "../SystemFonts.h"
#include "../Funcs.h"
#include "../Window.h"
#include "../Utils.h"
#include "../Errors.h"
#include "../PackedCol.h"

/* Plan 9 reports errors as descriptive strings via errstr(), not errno codes.
   We map our cc_result space onto small synthetic codes plus a generic
   "operation failed" sentinel returned by Plat_Err(). */
#define PLAN9_ERR_BASE 0x39000000

const cc_result ReturnCode_FileShareViolation = PLAN9_ERR_BASE + 1;
const cc_result ReturnCode_FileNotFound       = PLAN9_ERR_BASE + 2;
const cc_result ReturnCode_DirectoryExists    = PLAN9_ERR_BASE + 3;
const cc_result ReturnCode_PathNotFound       = PLAN9_ERR_BASE + 7;

const char* Platform_AppNameSuffix = " Plan9";
cc_uint8 Platform_Flags = PLAT_FLAG_SINGLE_PROCESS;
cc_bool  Platform_ReadonlyFilesystem;

/* Returns a non-zero cc_result for a just-failed Plan 9 syscall. */
static cc_result Plat_Err(void) {
	char buf[ERRMAX];
	buf[0] = '\0';
	errstr(buf, sizeof(buf));
	if (strstr(buf, "does not exist") || strstr(buf, "no such file"))
		return ReturnCode_FileNotFound;
	if (strstr(buf, "exists"))
		return ReturnCode_DirectoryExists;
	return PLAN9_ERR_BASE + 0x100;
}

#include "../_PlatformBase.h"

/*########################################################################################################################*
*-----------------------------------------------------Main entrypoint-----------------------------------------------------*
*#########################################################################################################################*/
#include "../main_impl.h"

void main(int argc, char** argv) {
	cc_result res;
	SetupProgram(argc, argv);

	do {
		res = RunProgram(argc, argv);
	} while (Window_Main.Exists);

	Window_Free();
	Process_Exit(res);
}

/*########################################################################################################################*
*------------------------------------------------------Logging/Time-------------------------------------------------------*
*#########################################################################################################################*/
void Platform_Log(const char* msg, int len) {
	write(2, msg, len);
	write(2, "\n", 1);
}

#define UNIX_EPOCH_SECONDS 62135596800ULL

TimeMS DateTime_CurrentUTC(void) {
	/* time() returns seconds since the Unix epoch on Plan 9 */
	return (TimeMS)time(nil) + UNIX_EPOCH_SECONDS;
}

void DateTime_CurrentLocal(struct cc_datetime* t) {
	Tm* tm;
	long now = time(nil);
	tm = localtime(now);

	t->year   = tm->year + 1900;
	t->month  = tm->mon  + 1;
	t->day    = tm->mday;
	t->hour   = tm->hour;
	t->minute = tm->min;
	t->second = tm->sec;
}

/*########################################################################################################################*
*-------------------------------------------------------Crash handling----------------------------------------------------*
*#########################################################################################################################*/
void CrashHandler_Install(void) { }

void Process_Abort2(cc_result result, const char* raw_msg) {
	Logger_DoAbort(result, raw_msg, NULL);
}

/*########################################################################################################################*
*--------------------------------------------------------Stopwatch--------------------------------------------------------*
*#########################################################################################################################*/
cc_uint64 Stopwatch_Measure(void) {
	/* nsec() returns nanoseconds since an arbitrary epoch (9front) */
	return (cc_uint64)nsec();
}

cc_uint64 Stopwatch_ElapsedMicroseconds(cc_uint64 beg, cc_uint64 end) {
	if (end < beg) return 0;
	return (end - beg) / 1000;
}

/*########################################################################################################################*
*-----------------------------------------------------Directory/File------------------------------------------------------*
*#########################################################################################################################*/
void Platform_EncodePath(cc_filepath* dst, const cc_string* path) {
	char* str = dst->buffer;
	Mem_Copy(str, path->buffer, path->length);
	str[path->length] = '\0';
}

void Platform_DecodePath(cc_string* dst, const cc_filepath* path) {
	String_AppendConst(dst, path->buffer);
}

void Directory_GetCachePath(cc_string* path) { }

cc_result Directory_Create2(const cc_filepath* path) {
	int fd = create(path->buffer, OREAD, DMDIR | 0775);
	if (fd < 0) return Plat_Err();
	close(fd);
	return 0;
}

int File_Exists(const cc_filepath* path) {
	Dir* d = dirstat(path->buffer);
	if (!d) return false;
	free(d);
	return true;
}

cc_result Directory_Enum(const cc_string* dirPath, void* obj, Directory_EnumCallback callback) {
	cc_filepath str;
	cc_string path; char pathBuffer[FILENAME_SIZE];
	Dir* entries;
	long n;
	int i, fd, is_dir;

	Platform_EncodePath(&str, dirPath);
	fd = open(str.buffer, OREAD);
	if (fd < 0) return Plat_Err();

	String_InitArray(path, pathBuffer);
	for (;;) {
		n = dirread(fd, &entries);
		if (n <= 0) break;

		for (i = 0; i < n; i++) {
			path.length = 0;
			String_Format2(&path, "%s/%c", dirPath, entries[i].name);

			is_dir = (entries[i].qid.type & QTDIR) != 0;
			callback(&path, obj, is_dir);
		}
		free(entries);
	}
	close(fd);
	return 0;
}

static cc_result File_Do(cc_file* file, const char* path, int mode) {
	int fd = open(path, mode);
	*file = fd;
	return fd < 0 ? Plat_Err() : 0;
}

cc_result File_Open(cc_file* file, const cc_filepath* path) {
	return File_Do(file, path->buffer, OREAD);
}

cc_result File_Create(cc_file* file, const cc_filepath* path) {
	int fd = create(path->buffer, ORDWR, 0644);
	*file = fd;
	return fd < 0 ? Plat_Err() : 0;
}

cc_result File_OpenOrCreate(cc_file* file, const cc_filepath* path) {
	int fd = open(path->buffer, ORDWR);
	if (fd < 0) fd = create(path->buffer, ORDWR, 0644);
	*file = fd;
	return fd < 0 ? Plat_Err() : 0;
}

cc_result File_Read(cc_file file, void* data, cc_uint32 count, cc_uint32* bytesRead) {
	long n = read(file, data, count);
	if (n < 0) { *bytesRead = 0; return Plat_Err(); }
	*bytesRead = n;
	return 0;
}

cc_result File_Write(cc_file file, const void* data, cc_uint32 count, cc_uint32* bytesWrote) {
	long n = write(file, data, count);
	if (n < 0) { *bytesWrote = 0; return Plat_Err(); }
	*bytesWrote = n;
	return 0;
}

cc_result File_Close(cc_file file) {
	return close(file) < 0 ? Plat_Err() : 0;
}

cc_result File_Seek(cc_file file, int offset, int seekType) {
	static cc_uint8 modes[3] = { 0, 1, 2 };
	vlong res = seek(file, offset, modes[seekType]);
	return res < 0 ? Plat_Err() : 0;
}

cc_result File_Position(cc_file file, cc_uint32* pos) {
	vlong res = seek(file, 0, 1);
	if (res < 0) { *pos = 0; return Plat_Err(); }
	*pos = (cc_uint32)res;
	return 0;
}

cc_result File_Length(cc_file file, cc_uint32* len) {
	Dir* d = dirfstat(file);
	if (!d) { *len = 0; return Plat_Err(); }
	*len = (cc_uint32)d->length;
	free(d);
	return 0;
}

/*########################################################################################################################*
*--------------------------------------------------------Threading--------------------------------------------------------*
*#########################################################################################################################*/
void Thread_Sleep(cc_uint32 milliseconds) {
	sleep(milliseconds);
}

/*########################################################################################################################*
*-----------------------------------------------------Process/Module------------------------------------------------------*
*#########################################################################################################################*/
cc_bool Process_OpenSupported = false;

cc_result Platform_SetDefaultCurrentDirectory(int argc, char** argv) {
	return 0;
}

cc_result Process_StartGame2(const cc_string* args, int numArgs) {
	return SetGameArgs(args, numArgs);
}

void Process_Exit(cc_result code) {
	exits(code ? "error" : nil);
}

cc_result Process_StartOpen(const cc_string* args) {
	return ERR_NOT_SUPPORTED;
}

/*########################################################################################################################*
*--------------------------------------------------------Platform---------------------------------------------------------*
*#########################################################################################################################*/
void Platform_Free(void) { }

cc_bool Platform_DescribeError(cc_result res, cc_string* dst) {
	if (res >= PLAN9_ERR_BASE) {
		String_Format1(dst, "Plan 9 error 0x%h", &res);
		return true;
	}
	return false;
}

void Platform_Init(void) { }

cc_result Platform_Encrypt(const void* data, int len, cc_string* dst) {
	return ERR_NOT_SUPPORTED;
}

cc_result Platform_Decrypt(const void* data, int len, cc_string* dst) {
	return ERR_NOT_SUPPORTED;
}

cc_result Platform_GetEntropy(void* data, int len) {
	return ERR_NOT_SUPPORTED;
}
#endif
