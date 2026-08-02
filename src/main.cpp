#include <windows.h>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#define STRUSE_IMPLEMENTATION
#include "struse/struse.h"

namespace fs = std::filesystem;

bool executeScript(const strref path, const strref currFolder, const strref args);

bool HandleMkDir(strref args, strref currFolder) {
    while(args) {
		args.skip_whitespace();
        // skip -p
        if (args.get_first() == '-') {
            ++args; args.skip_whitespace();
            args.skip(args.len_word());
        } else {
            strref path = args.get_path();
            if (!path.valid()) {
                printf("mkdir: missing operand\n");
                return false;
            }
            args.skip(path.get_len());
            args.skip_whitespace();

			strown<_MAX_PATH> fullPath(currFolder);
            if(currFolder.valid() && currFolder.get_last() != '/' && currFolder.get_last() != '\\') {
                fullPath.append('\\');
			}
            fullPath.append(path);
            fullPath.cleanup_path();

            fs::path target(std::string(fullPath.get(), fullPath.get_len()));
            std::error_code ec;
            fs::create_directories(target, ec);
            if (ec) {
                printf( "Failed to create directory: " STRREF_FMT "\n", STRREF_ARG(path));
                return false;
            }
        }
    }
    return true;
}

bool runExternalCommand(strref command, strref args) {
	strown<_MAX_PATH> fullCommand(command);
	fullCommand.append(' ').append(args);

    const int exitCode = std::system(fullCommand.c_str());
    if (exitCode != 0) {
        printf( "Command failed with exit code %d (" STRREF_FMT ")\n", exitCode, STRREF_ARG( fullCommand ));
        return false;
    }
    return true;
}

bool executeLine(const strref rawLine, const strref scriptDirectory, const strref args) {
    strown<1024> fixedLine(rawLine.get_trimmed_ws());
    fixedLine.replace('\\', '/');

    if (fixedLine.empty() || fixedLine.get_first() == '#') {
        return true;
    }

    strref line = fixedLine;
    strref command = line.get_path();
    line.skip(command.get_len());
    line.skip_whitespace();

    strref ext = command.after_last('.');
    if (ext.same_str("sh")) {
        printf("Executing script: " STRREF_FMT " " STRREF_FMT "\n", STRREF_ARG( command ), STRREF_ARG( line ) );
        return executeScript(command, scriptDirectory, strref());
    } else if (command.same_str("mkdir")) {
        return HandleMkDir(line, scriptDirectory);
    } else if (command.same_str("set") || command.same_str("rm")) {
        // do nothing for set
        return true;
    }
    printf("Executing command: " STRREF_FMT " " STRREF_FMT "\n", STRREF_ARG(command), STRREF_ARG(line));
    return runExternalCommand(command, line);
}

uint8_t *LoadFile(const char* path, size_t *outSize) {
    FILE *file = nullptr;
    if (fopen_s(&file, path, "rb") == 0 && file) {
        fseek(file, 0, SEEK_END);
        size_t size = ftell(file);
        fseek(file, 0, SEEK_SET);
        uint8_t *buffer = (uint8_t*)malloc(size);
        fread(buffer, 1, size, file);
        fclose(file);
        if (outSize) {
            *outSize = size;
        }
        return buffer;
    }
    return nullptr;
}

bool TryResolveScriptPath(const strref path, const strref currFolder, strown<_MAX_PATH>& outPath, uint8_t** scriptBuffer, size_t* scriptSize) {
    auto tryCandidate = [&](const strref base) -> bool {
        if (!base.valid()) {
            return false;
        }

        strown<_MAX_PATH> candidate;
        candidate.copy(base);
        if (candidate.get_last() != '/' && candidate.get_last() != '\\') {
            candidate.append('/');
        }
        candidate.append(path);
        candidate.cleanup_path();

        uint8_t *loaded = LoadFile(candidate.c_str(), scriptSize);
        if (!loaded) {
            return false;
        }

        outPath.copy(candidate);
        if (scriptBuffer) {
            *scriptBuffer = loaded;
        }
        return true;
    };

    if (path.get_first() == '/' || path.get_first() == '\\' || (path.get_len() >= 2 && path[1] == ':')) {
        strown<_MAX_PATH> absolute;
        absolute.copy(path);
        absolute.cleanup_path();
        uint8_t *loaded = LoadFile(absolute.c_str(), scriptSize);
        if (!loaded) {
            return false;
        }
        outPath.copy(absolute);
        if (scriptBuffer) {
            *scriptBuffer = loaded;
        }
        return true;
    }

    if (tryCandidate(currFolder)) {
        return true;
    }

    char cwd[4096];
    if (GetCurrentDirectoryA(sizeof(cwd), cwd)) {
        strref cwdRef(cwd);
        if (tryCandidate(cwdRef)) {
            return true;
        }
    }

    return false;
}

bool executeScript(const strref path, const strref currFolder, const strref args) {
    strown<_MAX_PATH> fullPath;
    size_t scriptSize = 0;
    uint8_t *script = nullptr;
    if (!TryResolveScriptPath(path, currFolder, fullPath, &script, &scriptSize)) {
        printf("Failed to load script: " STRREF_FMT "\n", STRREF_ARG(path));
        return false;
    }

    strref scriptFolder = fullPath.get_strref().before_last('/', '\\');

    strref file((const char*)script, (strl_t)scriptSize), fileOrig = file;
    while(strref line = file.line()) {
        if (!executeLine(line, scriptFolder, strref())) {
            printf("Stopping at line %d of " STRREF_FMT "\n", 
                fileOrig.count_lines(line), STRREF_ARG(fullPath));
            return false;
        }
    }
    return true;
}

int main(int argc, char** argv) {
    if (argc != 2) {
        printf( "Usage: bath <script.sh>\n");
        return 1;
    }

    char currDir[512];
    GetCurrentDirectoryA(sizeof(currDir), currDir);
    printf("%s\n", currDir);

    return executeScript(argv[1], strref(currDir), argc > 2 ? strref(argv[2]) : strref()) ? 0 : 2;
}
