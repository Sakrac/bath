#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <vector> // Note: Avoiding std includes but vector is useful.
#include <errno.h>

#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <pthread.h>
#endif

#define STRUSE_IMPLEMENTATION
#include "struse/struse.h"

#define WINDOWS_FOLDER_SEPARATOR '\\'
#define LINUX_FOLDER_SEPARATOR '/'
#ifdef _WIN32
#define WANTED_FOLDER_SEPARATOR WINDOWS_FOLDER_SEPARATOR
#define UNWANTED_FOLDER_SEPARATOR LINUX_FOLDER_SEPARATOR
#else
#define WANTED_FOLDER_SEPARATOR LINUX_FOLDER_SEPARATOR
#define UNWANTED_FOLDER_SEPARATOR WINDOWS_FOLDER_SEPARATOR
#endif

#ifdef _WIN32
typedef HANDLE ThreadType;
typedef LPTHREAD_START_ROUTINE ThreadFunction;
typedef DWORD ThreadReturn;
typedef void* ThreadArg;
typedef LONG AtomicIntType;
typedef CRITICAL_SECTION MutexType;
typedef CONDITION_VARIABLE ConditionVariable;
#else
typedef pthread_t ThreadType;
typedef void* (*ThreadFunction)(void*);
typedef void* ThreadReturn;
typedef void* ThreadArg;
typedef int AtomicIntType;
typedef pthread_mutex_t MutexType;
typedef pthread_cond_t ConditionVariable;
#endif

typedef enum BathStatus : uint8_t {
	BathStatus_Unset = 0,
	BathStatus_Tools,
	BathStatus_Execute,
	BathStatus_Finalize
} BathStatus;

typedef struct StringBuffer {
	char buffer[4096 - 16];
	size_t length;
	StringBuffer* next;
} StringBuffer;

typedef struct BathTool {
	strref name;        // Required "x65"
	strref command;     // Required "../x65/x65"
	strref mapping;     // Optional "fixed_args $Args -source=$In -out=$Out"
	strref auto_out;    // Optional $Out = obj/$In.filename.x65
} BathTool;

// controlled from command line arguments
bool ShowCommands = true;
bool RunCommands = true;
bool RunParallell = false;
bool ForceSingleThread = false;
bool Clean = false;
bool Rebuild = false;
bool Verbose = false;

// string buffers for parallell command execution
StringBuffer* pParallellCommandLines = nullptr;

std::vector<char*> IncludedScriptFiles;
std::vector<BathTool> RegisteredTools;
std::vector<char*> LoadedFiles;

AtomicIntType NumberOfParallellCommands = 0;
int MaxNumberOfParallellCommands = 8;
int ParallellCommandError = 0;

ConditionVariable ParallellCommandWait;
MutexType ParallellCommandMutex;

int TotalInputFiles = 0;
int TotalOutputFiles = 0;

static const strref match_filename = "filename";
static const strref match_noext = "noext";
static const strref match_noext_all = "noext_all";
static const strref match_path = "path";

#ifdef _WIN32
typedef time_t FileTime;
#define AtomicIncrement(ptr) InterlockedIncrement((volatile LONG*)(ptr))
#define AtomicDecrement(ptr) InterlockedDecrement((volatile LONG*)(ptr))

void PrintfW(const char *format, ...) {
	va_list args;
	va_start(args, format);
	char buffer[4096];
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);
    wchar_t wStr[4096];
	MultiByteToWideChar(CP_UTF8, 0, buffer, -1, wStr, sizeof(wStr) / sizeof(wStr[0]));
	wprintf(L"%s", wStr);
}

bool FOpenRB(FILE*& f, const char* n) {
	wchar_t wStr[_MAX_PATH];
	MultiByteToWideChar(CP_UTF8, 0, n, -1, wStr, sizeof(wStr) / sizeof(wStr[0]));
	return _wfopen_s(&f, wStr, L"rb") == 0;
}

double GetMonotonicSeconds() {
	static LARGE_INTEGER frequency = {};
	static bool frequencyInitialized = false;
	if (!frequencyInitialized) {
		QueryPerformanceFrequency(&frequency);
		frequencyInitialized = true;
	}

	LARGE_INTEGER counter = {};
	QueryPerformanceCounter(&counter);
	return static_cast<double>(counter.QuadPart) / static_cast<double>(frequency.QuadPart);
}

void StartThread(ThreadType* thread, size_t stack, ThreadFunction func, void* arg, const char* name) {
	ThreadType newThread = CreateThread(NULL, stack, func, arg, 0, NULL);
	if (thread) {
		*thread = newThread;
	}
}

void InitMutex(MutexType* m) {
	InitializeCriticalSection(m);
}

void DestroyMutex(MutexType* m) {
	DeleteCriticalSection(m);
}

void LockMutex(MutexType* m) {
	EnterCriticalSection(m);
}

void UnlockMutex(MutexType* m) {
	LeaveCriticalSection(m);
}

void InitConditionVariable(ConditionVariable* c) {
	InitializeConditionVariable(c);
}

void DestroyConditionVariable(ConditionVariable* c) {
	(void)c;
}

void WaitConditionVariable(ConditionVariable* c, MutexType* m) {
	SleepConditionVariableCS(c, m, INFINITE);
}

void AwakeConditionVariable(ConditionVariable* c) {
	WakeConditionVariable(c);
}

#else
typedef __time_t FileTime;
#define AtomicIncrement(ptr) __sync_add_and_fetch((ptr), 1)
#define AtomicDecrement(ptr) __sync_sub_and_fetch((ptr), 1)
#define PrintfW printf
#define FOpenRB(f, n) (f = fopen(n, "rb"))
#define _MAX_PATH 2048

double GetMonotonicSeconds() {
	struct timespec timeValue = {};
	clock_gettime(CLOCK_MONOTONIC, &timeValue);
	return static_cast<double>(timeValue.tv_sec) + static_cast<double>(timeValue.tv_nsec) / 1000000000.0;
}

void StartThread(ThreadType* thread, size_t stack, ThreadFunction func, void* arg, const char* name) {
	ThreadType newThread = pthread_create(thread, 0, func, arg);
	if (thread) {
		*thread = newThread;
	}
}

void InitMutex(MutexType* m) {
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutex_init(m, &attr);
}

void DestroyMutex(MutexType* m) {
	pthread_mutex_destroy(m);
}

void LockMutex(MutexType* m) {
	pthread_mutex_lock(m);
}

void UnlockMutex(MutexType* m) {
	pthread_mutex_unlock(m);
}

void InitConditionVariable(ConditionVariable* c) {
	pthread_cond_init(c, 0);
}

void DestroyConditionVariable(ConditionVariable* c) {
	pthread_cond_destroy(c);
}

void WaitConditionVariable(ConditionVariable* c, MutexType* m) {
	pthread_cond_wait(c, m);
}

void AwakeConditionVariable(ConditionVariable* c) {
	pthread_cond_signal(c);
}

#endif

static char* dupCString(const char* text) {
	size_t len = strlen(text) + 1;
	char* copy = (char*)malloc(len);
	if (copy) {
		memcpy(copy, text, len);
	}
	return copy;
}

int runExternalCommand(strref commandline) {
	strown<_MAX_PATH> fullCommand(commandline);
	fullCommand.replace(UNWANTED_FOLDER_SEPARATOR, WANTED_FOLDER_SEPARATOR);

	const int exitCode = system(fullCommand.c_str());
	if (exitCode != 0) {
		PrintfW("Command failed with exit code %d (" STRREF_FMT ")\n", exitCode, STRREF_ARG(fullCommand));
		return exitCode;
	}
	return 0;
}

uint8_t* LoadFile(const char* path, size_t* outSize) {
	FILE* file = nullptr;
	if (FOpenRB(file, path) && file) {
		fseek(file, 0, SEEK_END);
		long size = ftell(file);
		fseek(file, 0, SEEK_SET);
		if (size < 0) {
            if (Verbose) {
                PrintfW("Failed to get file size for: %s\n", path);
            }
			fclose(file);
			return nullptr;
		}

		uint8_t* buffer = (uint8_t*)malloc(size_t(size));
		if (buffer) {
			size_t bytesRead = fread(buffer, 1, size_t(size), file);
			if (bytesRead != size_t(size)) {
                if (Verbose) {
                    PrintfW("Failed to read file data: %s\n", path);
                }
				fclose(file);
				free(buffer);
				return nullptr;
			}
			if (outSize) {
				*outSize = size_t(size);
			}
			fclose(file);
			return buffer;
        } else if (Verbose) {
            PrintfW("Failed to alloc buffer for: %s\n", path);
        }
		fclose(file);
	}
    
    if (Verbose) {
        PrintfW("Could not open file errno %d: %s\n", errno, path);
    }

	return nullptr;
}

int registerTool(strref line) {
	// get name of tool
	strref name = line.split_lang();

	if (name.get_first() == '"' && name.get_last() == '"') { name.skip(1); name.clip(1); name.trim_whitespace(); }

	if (!name.valid()) {
		PrintfW("Error: Tool name is missing in line: " STRREF_FMT "\n", STRREF_ARG(line));
		return 1;
	}

	if (line.get_first() == '=') {
		++line; line.skip_whitespace();
	}

	strref command = line.split_path();
	command.trim_surrounding_quotes().trim_whitespace();

	if (!command.valid()) {
		PrintfW("Error: Tool command is missing in line: " STRREF_FMT "\n", STRREF_ARG(line));
		return 1;
	}

	strref mapping = line.get_trimmed_ws();
	mapping.trim_surrounding_quotes().trim_whitespace();

	if (!mapping.valid()) {
		PrintfW("Error: Tool mapping is missing in line: " STRREF_FMT "\n", STRREF_ARG(line));
		return 1;
	}

	BathTool tool = { name, command, mapping };

	// check if the same tool is already registered but with different command or mapping
	for(BathTool& existingTool : RegisteredTools) {
		if (existingTool.name.same_str(tool.name)) {
			if (!command.same_str(existingTool.command) || !mapping.same_str(existingTool.mapping)) {
				PrintfW("Error: Tool already registered with different command or mapping: " STRREF_FMT "\n", STRREF_ARG(name));
				return 0;
			}
		}
	}

	RegisteredTools.push_back(tool);

	if (Verbose) {
		PrintfW("Registered tool " STRREF_FMT "\n", STRREF_ARG(name));
	}

	return 1;
}

ThreadReturn ParallellCommandThread(ThreadArg Arg) {
	// Run the command
	const int exitCode = system((const char*)Arg);

	// Output the failure and set the error code if the command failed
	if (exitCode != 0) {
		PrintfW("Command failed with exit code %d (%s)\n", exitCode, (const char*)Arg);
		LockMutex(&ParallellCommandMutex);
		ParallellCommandError = exitCode;
		UnlockMutex(&ParallellCommandMutex);
	}

	LockMutex(&ParallellCommandMutex);
	AtomicDecrement(&NumberOfParallellCommands);
	AwakeConditionVariable(&ParallellCommandWait);
	UnlockMutex(&ParallellCommandMutex);
	return 0;
}

bool SyncParallellCommands() {
	LockMutex(&ParallellCommandMutex);
	while (NumberOfParallellCommands > 0) {
		WaitConditionVariable(&ParallellCommandWait, &ParallellCommandMutex);
	}
	UnlockMutex(&ParallellCommandMutex);

	// Free the command line buffers that are not needed anymore
	StringBuffer* buffer = pParallellCommandLines;
	pParallellCommandLines = nullptr;
	while (buffer) {
		StringBuffer* next = buffer->next;
		free(buffer);
		buffer = next;
	}

	return ParallellCommandError == 0;
}

int RunParallellCommand(strref commandline) {
	LockMutex(&ParallellCommandMutex);
	while (NumberOfParallellCommands >= MaxNumberOfParallellCommands) {
		WaitConditionVariable(&ParallellCommandWait, &ParallellCommandMutex);
	}

	if (ParallellCommandError != 0) {
		UnlockMutex(&ParallellCommandMutex);
		PrintfW("Skipping command because a previous command failed with exit code %d\n", ParallellCommandError);
		return 1;
	}

	// ensure space in the command line string buffer
	if (pParallellCommandLines == nullptr || pParallellCommandLines->length + commandline.get_len() + 1 >= sizeof(pParallellCommandLines->buffer)) {
		StringBuffer* newBuffer = (StringBuffer*)malloc(sizeof(StringBuffer));
		newBuffer->length = 0;
		newBuffer->next = pParallellCommandLines;
		pParallellCommandLines = newBuffer;
	}

	char* commandlinePtr = pParallellCommandLines->buffer + pParallellCommandLines->length;
	memcpy(commandlinePtr, commandline.get(), commandline.get_len());
	commandlinePtr[commandline.get_len()] = 0;
	pParallellCommandLines->length += commandline.get_len() + 1;

	AtomicIncrement(&NumberOfParallellCommands);
	UnlockMutex(&ParallellCommandMutex);

	StartThread(nullptr, 0, ParallellCommandThread, (void*)commandlinePtr, "ParallellCommand");
	return 0;
}

strovl replaceFileMatch(strovl shared, strref match, strref replace) {
	int pos = 0;

	strref all_files = replace;

	// in case of multple arguments pick only the first
	if (replace.get_first() == '(') {
		all_files.trim_surrounding_parens().trim_whitespace();
		++replace;
		replace = replace.get_path();
	}

	do {
		strown<2048> workspace;
		pos = shared.find(match, pos);
		strref rep = replace;
		strl_t match_len = match.get_len();
		if (pos >= 0) {
			if (shared[pos + match_len] == '.') {
				strref shared_match = (shared + pos + match_len + 1);
				if (shared_match.has_prefix(match_filename)) {
					match_len += match_filename.get_len() + 1;
					rep = rep.split_path();
					rep = rep.after_last_or_full(LINUX_FOLDER_SEPARATOR, WINDOWS_FOLDER_SEPARATOR).before_last_or_full('.', '.');
				} else if (shared_match.has_prefix(match_noext_all)) {
					match_len += match_noext_all.get_len() + 1;
					workspace.clear();
					while(strref file = rep.split_path()) {
						workspace.append(file.before_last('.')).append(' ');
					}
					rep = workspace.get_strref();
				} else if (shared_match.has_prefix(match_noext)) {
					match_len += match_noext.get_len() + 1;
					rep = rep.split_path();
					rep = rep.before_last_or_full('.');
				} else if (shared_match.has_prefix(match_path)) {
					match_len += match_path.get_len() + 1;
					rep = rep.before_last_or_full(LINUX_FOLDER_SEPARATOR, WINDOWS_FOLDER_SEPARATOR	);
				} else if (strref::is_number(shared[pos + match_len + 1])) {
					rep = all_files;
					int index = (shared + pos + match_len + 1).atoi();
					while (index > 1) {
						rep.split_path();
						rep.skip_whitespace();
						--index;
					}
					rep = rep.get_path();

					// if the next character is a dot, we can assume it's a file extension and skip it
					match_len += 1 + (shared + pos + match_len + 1).len_numeric();
				}
			}

			shared.erase(pos, match_len);
			shared.insert(rep, pos);
			pos += rep.get_len();
		}
	} while (pos >= 0);
	return shared;
}

bool cleanFile(strref file) {
#ifdef _WIN32
	// Note: since file is not zero terminated apply zero termination after converting to wide char
	wchar_t wStr[_MAX_PATH];
	int length = MultiByteToWideChar(CP_UTF8, 0, file.get(), file.get_len(), wStr, sizeof(wStr) / sizeof(wStr[0]));
	if(length >= 0 ) { wStr[length] = 0; }
    if (length > 0 && DeleteFileW(wStr) != 0) {
#else
    if(	remove(strown<_MAX_PATH>(file).c_str()) == 0 ) {
#endif
        if (Verbose) {
            PrintfW("Removed output file: " STRREF_FMT "\n", STRREF_ARG(file));
        }
        return true;
    }
    return false;
}

int GetFileStat(strref file, struct stat* outStat) {
	return stat(strown<_MAX_PATH>(file).replace(UNWANTED_FOLDER_SEPARATOR, WANTED_FOLDER_SEPARATOR).c_str(), outStat);
}

int executeLine(strref line, strref scriptFolder) {
	strref name = line.split_lang();
	line.skip_whitespace();

	if (name.get_first() == '"' && name.get_last() == '"') { name.skip(1); name.clip(1); name.trim_whitespace(); }

	// # is a comment except in quotes!
	strl_t pos = 0;
	while(pos < line.get_len()) {
		strl_t pos_next = line.get_len(), block_end = pos_next;
		int q = line.find('"', pos);
		if (q>=0) {
			strref quote = (line + q).get_quote_xml();	// returns string within quotes excluding the quotes so we need to add 2 to the length to get the full quote
			block_end = (strl_t)q;
			pos_next = block_end + quote.get_len() + 2;
		}
		int h = strref(line.get(), block_end).find('#', pos);
		if(h >=0 ) {
			line.clip(h);
			break;
		}
		pos = pos_next;
	}

	line = line.before_or_full('#').get_trimmed_ws(); // remove comments

	// params are in, out, args
	strref params = line.get_trimmed_ws();
	if (params.get_first() == '(' && params.get_last() == ')') { params.skip(1); params.clip(1); params.trim_whitespace(); }

	// command out : in args
	strref out = params.next_token(':').get_trimmed_ws(); ++params; params.skip_whitespace();
	strref in;
	if (params.get_first()=='(') {
		int close_param = params.find(')');
		if (close_param<0) {
			printf("Expected closing parenthesis\n");
			return 1;
		}
		in = params.get_clipped((strl_t)close_param + 1);
		params.skip(in.get_len());
		params.skip_whitespace();
	} else {
		in = params.next_token(' ').get_trimmed_ws();
	}
	strref args = params.get_trimmed_ws();

	in.trim_surrounding_quotes().trim_whitespace();
	out.trim_surrounding_quotes().trim_whitespace();

	struct stat stat_in = {}, stat_out = {};

	FileTime newest_in_time = 0;
	FileTime oldest_out_time = 0;

    bool in_exists = false;
	bool out_exists = false;

	if (in.get_first() == '(' && in.get_last() == ')') {
		strref all_in = in.trim_surrounding_parens().get_trimmed_ws();
		while (strref in_multi = all_in.split_path()) {
			int stat_in_result = GetFileStat(in_multi, &stat_in);
			if (stat_in_result == 0) {
				in_exists = true;
				if (stat_in.st_mtime > newest_in_time || newest_in_time == 0) {
					newest_in_time = stat_in.st_mtime;
				}
			} else {
				__nop();
			}
			++TotalInputFiles;
		}
	} else {
		int stat_in_result = GetFileStat(in, &stat_in);
		in_exists = (stat_in_result == 0);
		newest_in_time = in_exists ? stat_in.st_mtime : 0;
		++TotalInputFiles;
	}
	if (!in_exists && (!Clean || Rebuild)) {
		PrintfW("Input file does not exist: " STRREF_FMT "\n", STRREF_ARG(in));
		return 1;
	}

	if (out.get_first() == '(' && out.get_last() == ')') {
		strref all_out = out.trim_surrounding_parens().get_trimmed_ws();
		while (strref out_multi = all_out.split_path()) {
			if (Clean) {
                cleanFile(out_multi);
			} else {
				int stat_out_result = GetFileStat(out_multi, &stat_out);
				if (stat_out_result == 0) {
					out_exists = true;
					if (stat_out.st_mtime < oldest_out_time || oldest_out_time == 0) {
						oldest_out_time = stat_out.st_mtime;
					}
				}
			}
			++TotalOutputFiles;
		}
	} else if (Clean) {
        cleanFile(out);
		++TotalOutputFiles;
	} else {
		int stat_out_result = GetFileStat(out, &stat_out);
		out_exists = (stat_out_result == 0);
		oldest_out_time = out_exists ? stat_out.st_mtime : 0;
		++TotalOutputFiles;
	}

	bool in_newer = false;
	if (in_exists && out_exists) {
		in_newer = newest_in_time > oldest_out_time;
	} else if (in_exists && !out_exists) {
		in_newer = true;
	} else if (!in_exists && out_exists) {
		in_newer = false;
	} else {
		in_newer = true;
	}

	// If the user requested a clean build and not a rebuild, we can skip execution since the outputs have been removed.
	if (Clean && !Rebuild) {
		return 0;
	}

	if (!in_newer && !Rebuild) {
		if (Verbose) {
			PrintfW("Skipping command because output is newer than input: " STRREF_FMT "\n", STRREF_ARG(line));
		}
		return 0;
	}

	for (BathTool& tool : RegisteredTools) {
		if (name.same_str(tool.name)) {
			strown<_MAX_PATH> fullCommand(tool.command);
			fullCommand.append(" ").append(tool.mapping);
			fullCommand.replace("$Args", args);
			fullCommand.set_len(replaceFileMatch(fullCommand, "$In", in).get_len());
			fullCommand.set_len(replaceFileMatch(fullCommand, "$Out", out).get_len());

			fullCommand.replace(UNWANTED_FOLDER_SEPARATOR, WANTED_FOLDER_SEPARATOR);

			if (ShowCommands || Verbose) {
				PrintfW(STRREF_FMT "\n", STRREF_ARG(fullCommand));
			}

			if (!RunCommands) {
				return 0;
			}

			if (!in_newer && !Rebuild) {
				if (Verbose) {
					PrintfW("Skipping command because output is newer than input: " STRREF_FMT "\n", STRREF_ARG(line));
				}
				return 0;
			}

			if (RunParallell && !ForceSingleThread) {
				return RunParallellCommand(fullCommand);
			}
			return runExternalCommand(fullCommand);
		}
	}
	PrintfW("Error: Tool not registered: " STRREF_FMT "\n", STRREF_ARG(name));
	return 1;
}
	
int runBath(const char* scriptFile);

int includeScript(strref line, strref scriptFolder) {
	strref includePath = line.get_trimmed_ws();
	strown<_MAX_PATH> fullIncludePath;

	if ((includePath.get_len() > 1 && (includePath.has_prefix("/") || includePath.has_prefix("\\"))) || (includePath.get_len() >= 2 && includePath[1] == ':')) {
		fullIncludePath.copy(includePath);
	} else {
		fullIncludePath.copy(scriptFolder);
		fullIncludePath.append('/');
		fullIncludePath.append(includePath);
	}

	fullIncludePath.cleanup_path();
	for (char* PreviousInclude : IncludedScriptFiles) {
		if (fullIncludePath.same_str_case(PreviousInclude)) {
			if (Verbose) {
				PrintfW("Include skipped because it is already loaded: " STRREF_FMT "\n", STRREF_ARG(fullIncludePath));
			}
			return 0;
		}
	}
	IncludedScriptFiles.push_back(dupCString(fullIncludePath.c_str()));

	return runBath(fullIncludePath.c_str());
}

int runBath(const char* scriptFile) {
	size_t scriptSize = 0;
	uint8_t* script = LoadFile(scriptFile, &scriptSize);

	if (!script) {
		PrintfW("Failed to load bath script: %s\n", scriptFile);
		return 1;
	} else if (Verbose) {
		PrintfW("Loaded bath script: %s (%zu bytes)\n", scriptFile, scriptSize);
	}

	// remember the loaded script so it can be freed later
	LoadedFiles.push_back((char*)script);

	BathStatus Status = BathStatus_Unset;

	// path from current working folder
	strref path(strref(scriptFile).before_last_or_full(LINUX_FOLDER_SEPARATOR, WINDOWS_FOLDER_SEPARATOR));

	strref scriptRef((const char*)script, (strl_t)scriptSize), file(scriptRef);
	while (strref line = file.line()) {
		line.trim_whitespace();
		if (!line.valid() || line.get_first() == '#' || line.has_prefix("---")) {
			continue;
		}
		if (line.grab_prefix("$")) {
			strref command = line.get_word();
			line += command.get_len();
			if (command.same_str("tools")) {
				if (Verbose) {
					PrintfW("Switching to tools registration mode\n");
				}
				Status = BathStatus_Tools;
				continue;
			} else if (command.same_str("execution")) {
				if (Verbose) {
					PrintfW("Switching to execution mode\n");
				}
				Status = BathStatus_Execute;
				continue;
			} else if (command.same_str("parallel") || command.same_str("parallell")) {
				RunParallell = true;
				Status = BathStatus_Execute;
				line.skip_whitespace();
				if (strref::is_number(line.get_first())) {
					MaxNumberOfParallellCommands = line.atoi();
					if (MaxNumberOfParallellCommands < 1) {
						MaxNumberOfParallellCommands = 1;
					}
				}
				if (Verbose) {
					PrintfW("Parallellizing\n");
				}
				continue;
			} else if (command.same_str("sequential")) {
				RunParallell = false;
				SyncParallellCommands();
				Status = BathStatus_Execute;
				if (Verbose) {
					PrintfW("Sequentializing\n");
				}
				continue;
			} else if (command.same_str("sync")) {
				if (Verbose) {
					PrintfW("Synchronizing\n");
				}
				SyncParallellCommands();
				continue;
			} else if (command.same_str("finalize")) {
				if (Verbose) {
					PrintfW("Finalizing\n");
				}
				SyncParallellCommands();
				Status = BathStatus_Finalize;
				continue;
			} else if (command.same_str("include")) {
				if (Verbose) {
					PrintfW("Including script " STRREF_FMT "\n", STRREF_ARG(line));
				}
				includeScript(line, path);
				continue;
			}
			continue;
		}
		switch (Status) {
			case BathStatus_Unset:
				PrintfW("Error: No status set before executing line: " STRREF_FMT "\n", STRREF_ARG(line));
				return 1;
			case BathStatus_Tools:
				if (!registerTool(line)) {
					PrintfW("Stopping at line %d of %s\n", scriptRef.count_lines(line), scriptFile);
					return 1;
				}
				break;
			case BathStatus_Execute:
			{
				int errorCode = executeLine(line, path);
				if (errorCode != 0) {
					PrintfW("Failed execution at line %d of %s with error code %d\n", scriptRef.count_lines(line), scriptFile, errorCode);
					return 1;
				}
				break;
			}
			case BathStatus_Finalize:
				if (runExternalCommand(line) != 0) {
					PrintfW("Finalize command failed at line %d of %s\n", scriptRef.count_lines(line), scriptFile);
					return 1;
				}
				break;
		}
	}
	return 0;
}

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[]) {
	char scriptFileUtf8[_MAX_PATH];
#else
int main(int argc, char** argv) {
#endif
	const char* scriptFile = nullptr;
	bool sawScript = false;

	double startTime = GetMonotonicSeconds();

	// comamnd line options
	for (int i = 1; i < argc; ++i) {
#ifdef _WIN32
		char tmp[1024];
		strref arg(tmp, (strl_t)WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, tmp, sizeof(tmp), NULL, NULL)-1);
#else
		strref arg(argv[i]);
#endif
		if (arg.grab_prefix("-")) {
			if (arg.same_str("nocommands")) { RunCommands = false; ShowCommands = true; }
			else if (arg.same_str("commands")) { RunCommands = true; }
			else if (arg.same_str("simulate")) { RunCommands = false; }
			else if (arg.same_str("force-single-thread")) { ForceSingleThread = true; }
			else if (arg.same_str("single")) { ForceSingleThread = true; }
			else if (arg.same_str("clean")) { Clean = true; }
			else if (arg.same_str("clear")) { Clean = true; }
			else if (arg.same_str("rebuild")) { Rebuild = true; }
			else if (arg.same_str("verbose")) { Verbose = true; }
			else if (arg.same_str("echo_off")) { ShowCommands = false; }
			else {
				PrintfW("Error: Unknown argument: %s\n", arg.get());
				scriptFile = nullptr;
				sawScript = true;
				break;
			}
		} else if (!sawScript) {
#ifdef _WIN32
			strovl(scriptFileUtf8, (strl_t)sizeof(scriptFileUtf8)).copy(arg);
			scriptFileUtf8[arg.get_len()] = 0;
			scriptFile = scriptFileUtf8;
#else
			scriptFile = argv[i];
#endif
			sawScript = true;
		} else {
			PrintfW("Error: Unexpected argument: %s\n", argv[i]);
			scriptFile = nullptr;
			sawScript = true;
			break;
		}
	}

	if (!sawScript || scriptFile == nullptr) {
		PrintfW("Usage: bath <script.sh>\n");
		return 1;
	}

	InitMutex(&ParallellCommandMutex);
	InitConditionVariable(&ParallellCommandWait);

	int errorCode = runBath(scriptFile);

	// Wait for all parallel commands to finish before exiting
	SyncParallellCommands();

	// Shut down systems
	DestroyConditionVariable(&ParallellCommandWait);
	DestroyMutex(&ParallellCommandMutex);

	for (char* loadedFile : LoadedFiles) {
		free(loadedFile);
	}
	LoadedFiles.clear();
	for (char* includedFile : IncludedScriptFiles) {
		free(includedFile);
	}
	IncludedScriptFiles.clear();

	if (Verbose) {
		double endTime = GetMonotonicSeconds();
		PrintfW("Total execution time: %.9f seconds\n", endTime - startTime);
		PrintfW("Total input files: %d\n", TotalInputFiles);
		PrintfW("Total output files: %d\n", TotalOutputFiles);
	}

	return errorCode;
}
