#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#endif
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <time.h>
#include <unistd.h>
#endif
#define STRUSE_IMPLEMENTATION
#include "struse/struse.h"

#if defined(_MSC_VER)
#define FOpen(f, n, t) (fopen_s(&f, n, t) == 0)
#else
#define FOpen(f, n, t) (f = fopen(n, t))
#define _MAX_PATH 2048
#endif

// TODO:
//  Commands can have multiple out files (c64addr -split)
//  Commands can have multiple in files (minipak)

typedef enum BathStatus : uint8_t {
    BathStatus_Unset = 0,
    BathStatus_Tools,
    BathStatus_Execute,
    BathStatus_Finalize
} BathStatus;

#define MAX_REGISTERED_TOOLS 128
#define MAX_PREVIOUS_INCLUDES 256

static bool ShowCommands = true;
static bool RunCommands = false;

static char* dupCString(const char* text) {
    size_t len = strlen(text) + 1;
    char* copy = (char*)malloc(len);
    if (copy) {
        memcpy(copy, text, len);
    }
    return copy;
}

bool runExternalCommand(strref commandline) {
	strown<_MAX_PATH> fullCommand(commandline);
#ifdef _WIN32
    fullCommand.replace('/', '\\');
#else
    fullCommand.replace('\\', '/');
#endif

    const int exitCode = system(fullCommand.c_str());
    if (exitCode != 0) {
        printf( "Command failed with exit code %d (" STRREF_FMT ")\n", exitCode, STRREF_ARG( fullCommand ));
        return false;
    }
    return true;
}

uint8_t *LoadFile(const char* path, size_t *outSize) {
    FILE *file = nullptr;
    if (FOpen(file, path, "rb") && file) {
        fseek(file, 0, SEEK_END);
        size_t size = ftell(file);
        fseek(file, 0, SEEK_SET);
        uint8_t *buffer = (uint8_t*)malloc(size);
        if (buffer) {
            fread(buffer, 1, size, file);
            fclose(file);
            if (outSize) {
                *outSize = size;
            }
        }
        return buffer;
    }
    return nullptr;
}


// register a tool for the bath script
typedef struct BathTool {
    strref name;        // Required "x65"
    strref command;     // Required "../x65/x65"
    strref mapping;     // Optional "fixed_args $Args -source=$In -out=$Out"
    strref auto_out;    // Optional $Out = obj/$In.filename.x65
} BathTool;

BathTool RegisteredTools[MAX_REGISTERED_TOOLS];
int RegisteredToolCount = 0;

int registerTool(strref line) {

    // get name of tool
    strref name = line.split_lang();
    line += name.get_len();

    if(name.get_first()=='"' && name.get_last() == '"') { name.skip(1); name.clip(1); name.trim_whitespace(); }

    if (!name.valid()) {
        printf("Error: Tool name is missing in line: " STRREF_FMT "\n", STRREF_ARG(line));
        return 1;
    }

    if (line.get_first()=='=') {
        ++line; line.skip_whitespace();
    }

    printf("line before command: " STRREF_FMT "\n", STRREF_ARG(line));

    strref command = line.split_path();
    command.trim_surrounding_quotes().trim_whitespace();

    if (!command.valid()) {
        printf("Error: Tool command is missing in line: " STRREF_FMT "\n", STRREF_ARG(line));
        return 1;
    }

    printf("line after command: " STRREF_FMT "\n", STRREF_ARG(line));

    strref mapping = line.get_trimmed_ws();
    mapping.trim_surrounding_quotes().trim_whitespace();

    if (!mapping.valid()) {
        printf("Error: Tool mapping is missing in line: " STRREF_FMT "\n", STRREF_ARG(line));
        return 1;
    }

    printf("mapping: " STRREF_FMT "\n", STRREF_ARG(mapping));

    printf("line after mapping: " STRREF_FMT "\n", STRREF_ARG(line));

    if (RegisteredToolCount >= MAX_REGISTERED_TOOLS) {
        printf("Error: Too many tools registered\n");
        return 0;
    }

    BathTool tool = { name, command, mapping };
    RegisteredTools[RegisteredToolCount++] = tool;
    return 1;
}


static const strref match_filename = "filename";

strovl replaceFileMatch(strovl shared, strref match, strref replace) {
    int pos = 0;
    do {
        pos = shared.find(match, pos);
        strref rep = replace;
		strl_t match_len = match.get_len();
        if (pos >= 0) {
            if (shared[pos + match_len] == '.') {
                if ((shared + pos + match_len + 1).has_prefix(match_filename)) {
                    match_len += match_filename.get_len() + 1;
                    rep = rep.after_last_or_full('/', '\\').before_last_or_full('.', '.');
                }
            }

            shared.erase(pos, match_len);
            shared.insert(rep, pos);
            pos += rep.get_len();
		}
    } while (pos >= 0);
    return shared;
}

int executeLine(strref line, strref scriptFolder) {
    strref name = line.split_lang();
    line.skip_whitespace();

    if(name.get_first()=='"' && name.get_last() == '"') { name.skip(1); name.clip(1); name.trim_whitespace(); }

    line = line.before_or_full('#').get_trimmed_ws(); // remove comments

    // params are in, out, args
    strref params = line.get_trimmed_ws();
    if(params.get_first() == '(' && params.get_last() == ')') { params.skip(1); params.clip(1); params.trim_whitespace(); }

	// command out : in args
    strref out = params.next_token(':').get_trimmed_ws(); ++params; params.skip_whitespace();
    strref in = params.next_token(' ').get_trimmed_ws();
    strref args = params.get_trimmed_ws();

    in.trim_surrounding_quotes().trim_whitespace();
    out.trim_surrounding_quotes().trim_whitespace();

    struct stat stat_in = {}, stat_out = {};

    int stat_in_result = stat(strown<_MAX_PATH>(in).c_str(), &stat_in);
    int stat_out_result = stat(strown<_MAX_PATH>(out).c_str(), &stat_out); 

    bool in_exists = (stat_in_result == 0);
    bool out_exists = (stat_out_result == 0);

    if (!in_exists) {
        printf("Input file does not exist: " STRREF_FMT "\n", STRREF_ARG(in));
        return 1;
    }

    bool in_newer = false;
    if (in_exists && out_exists) {
#if defined(_WIN32)
        in_newer = stat_in.st_mtime > stat_out.st_mtime;
#else
        in_newer = stat_in.st_mtim.tv_sec > stat_out.st_mtim.tv_sec;
#endif
    } else if (in_exists && !out_exists) {
        in_newer = true;
    } else if (!in_exists && out_exists) {
        in_newer = false;
    } else {
        in_newer = true;
    }

    if (!ShowCommands && !in_newer) {
        printf("Skipping command because output is newer than input: " STRREF_FMT "\n", STRREF_ARG(line));
        return 0;
	}
 
    for (int i = 0; i < RegisteredToolCount; ++i) {
        const BathTool& tool = RegisteredTools[i];
        if (name.same_str(tool.name)) {
//            printf("tool.mapping: " STRREF_FMT "\n", STRREF_ARG(tool.mapping));
            strown<_MAX_PATH> fullCommand(tool.command);
//            printf("tool.command: " STRREF_FMT "\n", STRREF_ARG(fullCommand));
            fullCommand.append(" ");
//            printf("append(" "): " STRREF_FMT "\n", STRREF_ARG(fullCommand));
            fullCommand.append(tool.mapping);
//            printf("append(tool.mapping): " STRREF_FMT "\n", STRREF_ARG(fullCommand));
            fullCommand.replace("$Args", args);
//            printf("replace(\"$Args\"): " STRREF_FMT "\n", STRREF_ARG(fullCommand));
            fullCommand.set_len(replaceFileMatch(fullCommand, "$In", in).get_len());
//            printf("$In " STRREF_FMT "\n", STRREF_ARG(fullCommand));
			fullCommand.set_len(replaceFileMatch(fullCommand, "$Out", out).get_len());

#ifdef _WIN32
            fullCommand.replace('/', '\\');
#else
            fullCommand.replace('\\', '/');
#endif
            if(ShowCommands) {
                printf( STRREF_FMT "\n", STRREF_ARG(fullCommand));
			}

            if (!RunCommands) {
                return 0;
            }

            if (!in_newer) {
				printf("Skipping command because output is newer than input: " STRREF_FMT "\n", STRREF_ARG(line));
				return 0;
			}
            

            return runExternalCommand(fullCommand);
        }
    }
    printf("Error: Tool not registered: " STRREF_FMT "\n", STRREF_ARG(name));
    return 1;
}


int runBath(const char* scriptFile, const char **args, int argn);

char* PreviousIncludes[MAX_PREVIOUS_INCLUDES];
int PreviousIncludeCount = 0;

int includeScript(strref line, strref scriptFolder, const char** args, int argn) {
    strref includePath = line.get_trimmed_ws();
    strown<_MAX_PATH> fullIncludePath;

    if (includePath.has_prefix("/") || includePath.has_prefix("\\") || includePath[1] == ':') {
        fullIncludePath.copy(includePath);
    } else {
        fullIncludePath.copy(scriptFolder);
        fullIncludePath.append("/");
        fullIncludePath.append(includePath);
    }

    fullIncludePath.cleanup_path();
    for (int i = 0; i < PreviousIncludeCount; ++i) {
        if (fullIncludePath.same_str_case(PreviousIncludes[i])) {
            printf("Include skipped because it is already loaded: " STRREF_FMT "\n", STRREF_ARG(fullIncludePath));
            return 0;
        }
    }
    if (PreviousIncludeCount >= MAX_PREVIOUS_INCLUDES) {
        printf("Error: Too many included scripts\n");
        return 1;
    }
    PreviousIncludes[PreviousIncludeCount++] = dupCString(fullIncludePath.c_str());

    return runBath(fullIncludePath.c_str(), args, argn);
}

int runBath(const char* scriptFile, const char **args, int argn) {
    size_t scriptSize = 0;
    uint8_t *script = LoadFile(scriptFile, &scriptSize);

    if (!script) {
        printf("Failed to load bath script: %s\n", scriptFile);
        return 1;
    }

    BathStatus Status = BathStatus_Unset;

    // path from current working folder
    strref path(strref(scriptFile).before_last_or_full('/', '\\'));

    strref scriptRef((const char*)script, (strl_t)scriptSize), file(scriptRef);
    while(strref line = file.line()) {
        line.trim_whitespace();
        if (!line.valid() || line.get_first() == '#' || line.has_prefix("---")) {
            continue;
        }
        if( line.grab_prefix("$")) {
            strref command = line.get_word();
            line += command.get_len();
            if (command.same_str("tools")) {
                Status = BathStatus_Tools;
                continue;
            } else if (command.same_str("execute")) {
                Status = BathStatus_Execute;
                continue;
            } else if (command.same_str("finalize")) {
                Status = BathStatus_Finalize;
                continue;
            } else if (command.same_str("include")) {
                includeScript(line, path, args, argn);
                continue;
            }
            continue;
        }
        switch (Status) {
            case BathStatus_Unset:
                printf("Error: No status set before executing line: " STRREF_FMT "\n", STRREF_ARG(line));
                return 1;
            case BathStatus_Tools:
                if (!registerTool(line)) {
                    printf("Stopping at line %d of %s\n", scriptRef.count_lines(line), scriptFile);
                    return 1;
                }
                break;
            case BathStatus_Execute: {
                int errorCode = executeLine(line, path);
                if (errorCode != 0) {
                    printf("Failed execution at line %d of %s with error code %d\n", scriptRef.count_lines(line), scriptFile, errorCode);
                    return 1;
                }
                break;
            }
            case BathStatus_Finalize:
                if (!runExternalCommand(line)) {
                    printf("Stopping at line %d of %s\n", scriptRef.count_lines(line), scriptFile);
                    return 1;
                }
                break;
        }
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc != 2) {
        printf( "Usage: bath <script.sh>\n");
        return 1;
    }
#ifdef _WIN32
    char currDir[512];
    GetCurrentDirectoryA(sizeof(currDir), currDir);
    printf("%s\n", currDir);
#else
	char* currDir = getcwd(NULL, 0);
	if( currDir) {
		printf("%s\n", currDir);
//				free(dir);
	}
#endif

    int errorCode = runBath(argv[1], (const char **)(argv+2), argc-2);
    return errorCode;
}
