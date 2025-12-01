#define CC_NO_UPDATER
#define CC_NO_DYNLIB
#define CC_NO_SOCKETS
#define CC_NO_THREADING

#include "../Stream.h"
#include "../ExtMath.h"
#include "../SystemFonts.h"
#include "../Funcs.h"
#include "../Window.h"
#include "../Utils.h"
#include "../Errors.h"
#include "../PackedCol.h"

#include <sys/ksys.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

const cc_result ReturnCode_FileShareViolation = 1000000000;
const cc_result ReturnCode_FileNotFound     = ENOENT;
const cc_result ReturnCode_DirectoryExists  = EEXIST;
const cc_result ReturnCode_SocketInProgess  = 1000000;
const cc_result ReturnCode_SocketWouldBlock = 1000000;
const cc_result ReturnCode_SocketDropped    = 1000000;

const char* Platform_AppNameSuffix = " KolibriOS";
cc_uint8 Platform_Flags = PLAT_FLAG_SINGLE_PROCESS;
cc_bool  Platform_ReadonlyFilesystem;
#include "../_PlatformBase.h"

/*########################################################################################################################*
*-----------------------------------------------------Main entrypoint-----------------------------------------------------*
*#########################################################################################################################*/
#include "../main_impl.h"

int main(int argc, char** argv) {
	cc_result res;
	SetupProgram(argc, argv);

	do {
		res = RunProgram(argc, argv);
	} while (Window_Main.Exists);

	Window_Free();
	Process_Exit(res);
	return res;
}

/*########################################################################################################################*
*------------------------------------------------------Logging/Time-------------------------------------------------------*
*#########################################################################################################################*/
void Platform_Log(const char* msg, int len) {
	char buffer[2048 + 1];
	len = min(len, 2048);
	Mem_Copy(buffer, msg, len);
	buffer[len] = '\0';
	_ksys_debug_puts(buffer);
}

TimeMS DateTime_CurrentUTC(void) {
	ksys_date_bcd_t date = _ksys_get_date();
	ksys_time_bcd_t time = _ksys_get_time();
	
	// Конвертируем BCD в нормальные числа
	int year   = ((date.year >> 4) * 10 + (date.year & 0x0F)) + 2000;
	int month  = (date.month >> 4) * 10 + (date.month & 0x0F);
	int day    = (date.day >> 4) * 10 + (date.day & 0x0F);
	int hour   = (time.hour >> 4) * 10 + (time.hour & 0x0F);
	int minute = (time.min >> 4) * 10 + (time.min & 0x0F);
	int second = (time.sec >> 4) * 10 + (time.sec & 0x0F);
	
	// Простой подсчёт миллисекунд (не учитывает часовые пояса)
	TimeMS ms = 0;
	ms += (year - 1970) * 365 * 24 * 60 * 60 * 1000ULL;
	ms += (month - 1) * 30 * 24 * 60 * 60 * 1000ULL;
	ms += (day - 1) * 24 * 60 * 60 * 1000ULL;
	ms += hour * 60 * 60 * 1000ULL;
	ms += minute * 60 * 1000ULL;
	ms += second * 1000ULL;
	
	return ms;
}

void DateTime_CurrentLocal(struct cc_datetime* t) {
	ksys_date_bcd_t date = _ksys_get_date();
	ksys_time_bcd_t time = _ksys_get_time();
	
	t->year   = ((date.year >> 4) * 10 + (date.year & 0x0F)) + 2000;
	t->month  = (date.month >> 4) * 10 + (date.month & 0x0F);
	t->day    = (date.day >> 4) * 10 + (date.day & 0x0F);
	t->hour   = (time.hour >> 4) * 10 + (time.hour & 0x0F);
	t->minute = (time.min >> 4) * 10 + (time.min & 0x0F);
	t->second = (time.sec >> 4) * 10 + (time.sec & 0x0F);
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
	return _ksys_get_tick_count() * 10000ULL; // тики в микросекунды
}

cc_uint64 Stopwatch_ElapsedMicroseconds(cc_uint64 beg, cc_uint64 end) {
	if (end < beg) return 0;
	return end - beg;
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

void Directory_GetCachePath(cc_string* path) {
	String_AppendConst(path, "/tmp/");
}

cc_result Directory_Create(const cc_filepath* path) {
	return _ksys_mkdir(path->buffer) == 0 ? 0 : errno;
}

int File_Exists(const cc_filepath* path) {
	ksys_file_info_t info;
	return _ksys_file_info(path->buffer, &info) == 0;
}

cc_result Directory_Enum(const cc_string* dirPath, void* obj, Directory_EnumCallback callback) {
	return ERR_NOT_SUPPORTED; // TODO
}

cc_result File_Open(cc_file* file, const cc_filepath* path) {
	FILE* f = fopen(path->buffer, "rb");
	*file = (cc_file)f;
	return f ? 0 : errno;
}

cc_result File_Create(cc_file* file, const cc_filepath* path) {
	FILE* f = fopen(path->buffer, "wb");
	*file = (cc_file)f;
	return f ? 0 : errno;
}

cc_result File_OpenOrCreate(cc_file* file, const cc_filepath* path) {
	FILE* f = fopen(path->buffer, "ab");
	*file = (cc_file)f;
	return f ? 0 : errno;
}

cc_result File_Read(cc_file file, void* data, cc_uint32 count, cc_uint32* bytesRead) {
	*bytesRead = fread(data, 1, count, (FILE*)file);
	return ferror((FILE*)file) ? errno : 0;
}

cc_result File_Write(cc_file file, const void* data, cc_uint32 count, cc_uint32* bytesWrote) {
	*bytesWrote = fwrite(data, 1, count, (FILE*)file);
	return ferror((FILE*)file) ? errno : 0;
}

cc_result File_Close(cc_file file) {
	return fclose((FILE*)file) == EOF ? errno : 0;
}

cc_result File_Seek(cc_file file, int offset, int seekType) {
	static cc_uint8 modes[3] = { SEEK_SET, SEEK_CUR, SEEK_END };
	return fseek((FILE*)file, offset, modes[seekType]) == -1 ? errno : 0;
}

cc_result File_Position(cc_file file, cc_uint32* pos) {
	*pos = ftell((FILE*)file);
	return *pos == -1 ? errno : 0;
}

cc_result File_Length(cc_file file, cc_uint32* len) {
	long curPos = ftell((FILE*)file);
	if (curPos == -1) return errno;
	
	if (fseek((FILE*)file, 0, SEEK_END) == -1) return errno;
	*len = ftell((FILE*)file);
	
	fseek((FILE*)file, curPos, SEEK_SET);
	return 0;
}

/*########################################################################################################################*
*--------------------------------------------------------Threading--------------------------------------------------------*
*#########################################################################################################################*/
void Thread_Sleep(cc_uint32 milliseconds) {
	_ksys_delay(milliseconds / 10); // KolibriOS delay в сотых долях секунды
}

/*########################################################################################################################*
*-----------------------------------------------------Process/Module------------------------------------------------------*
*#########################################################################################################################*/
cc_bool Process_OpenSupported = false;

int Platform_GetCommandLineArgs(int argc, STRING_REF char** argv, cc_string* args) {
	return GetGameArgs(args);
}

cc_result Platform_SetDefaultCurrentDirectory(int argc, char **argv) {
	return 0;
}

cc_result Process_StartGame2(const cc_string* args, int numArgs) {
	return SetGameArgs(args, numArgs);
}

void Process_Exit(cc_result code) { 
	_ksys_exit();
	for(;;) { }
}

cc_result Process_StartOpen(const cc_string* args) {
	return ERR_NOT_SUPPORTED;
}

/*########################################################################################################################*
*--------------------------------------------------------Platform---------------------------------------------------------*
*#########################################################################################################################*/
void Platform_Free(void) { }

cc_bool Platform_DescribeError(cc_result res, cc_string* dst) {
	char chars[NATIVE_STR_LEN];
	int len;

	if (res >= 1000000000) return false;
	len = snprintf(chars, NATIVE_STR_LEN, "Error %d", (int)res);
	String_AppendUtf8(dst, chars, len);
	return true;
}

void Platform_Init(void) {
	_ksys_setcwd("/tmp0/1");
}

cc_result Platform_Encrypt(const void* data, int len, cc_string* dst) {
	return ERR_NOT_SUPPORTED;
}

cc_result Platform_Decrypt(const void* data, int len, cc_string* dst) {
	return ERR_NOT_SUPPORTED;
}

cc_result Platform_GetEntropy(void* data, int len) {
	return ERR_NOT_SUPPORTED;
}