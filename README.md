# Bath

Bath is a small build tool for running command pipelines from a single script. It is meant to replace handwritten .bat or .sh files for asset conversion, assembly, and other build steps.

A Bath script is read top to bottom. The tools are declared first, then execution commands are run, and an optional finalize section can run raw commands at the end.

## Script structure

A Bath file usually has three parts:

1. A tool declaration block started with `$tools`
2. An execution block started with `$execution`
3. An optional finalize block started with `$finalize`

## Directives

The current implementation recognizes these directives:

- `$Include` followed by a path to another Bath file. Includes are tracked so the same file is only loaded once.
- `$Tools` starts the tool declaration block.
- `$Execution` starts the execution block.
- `$Parallell` enables parallel execution for later commands.
- `$Sequential` switches back to sequential execution.
- `$Sync` waits for all currently running parallel commands to finish before continuing.
- `$Finalize` starts the finalize block. Any parallel work is finished first.

Directives ignore case so feel free to use any casing you are comfortable with, including uppercase spellings such as $TOOLS or $PARALLELL.

## Tool declaration

A tool declaration looks like this:

```text
tool_alias tool_path arguments
```

The command line for each execution can reference:

- `$In` for the input path or paths
- `$Out` for the output path or paths
- `$Args` for per-line arguments supplied in the execution block

The replacement order is `$Args`, then `$In`, then `$Out`, so the per-line arguments can still refer to the file paths.

It is often useful to reference only a filename or path component. Bath provides a small set of filters for this:

- `$In.filename` - filename only, no path and no extension
- `$In.noext` - path without the extension
- `$In.path` - path without a trailing slash
- `$In.1` - first file in a multi-file list
- `$In.<number>` - indexed file in a multi-file list

The same filters are available for `$Out`.

Here is an example of a tool declaration:

```
$Tools

x65 ..\x65\x65 $In -lst=lst\$In.filename.lst -obj $Out -srcdbg
```

## Execution

After the `$execution` directive, each line starts with a tool alias followed by output(s), a colon, input(s), and optional extra arguments:

```text
x65 obj/Game.x65 : src/game.s
```

In this example, `.x65` is the generated object file and `.s` is the source that produces it.

## Multiple inputs and outputs

Wrap multiple paths in parentheses to pass several files at once:

```text
c64gfx (data/title.chr data/title.scr data/title.col) : assets/title.png -textmc
```

The matching tool declaration can then refer to `$Out` or `$In` as a group:

```text
c64gfx ../C64Gfx/C64Gfx $In $Args -out=$Out.noext
```

Multiple inputs work the same way:

```text
cruncher LevelBlob : (level.map level.chr level.col)
```

## Command-line arguments for Bath

The current implementation accepts these flags:

- `-commands` - print commands and execute them
- `-nocommands` - print commands without executing them
- `single` or `-force-single-thread` - disable parallel execution even if `$parallell` is active
- `-rebuild` - force commands to run even when the outputs look newer
- `-verbose` - print extra diagnostic information
- `-echo_off` - skip printing the command lines before execution

A `-clean` flag is recognized in the parser, but the output-cleanup behavior is still a work in progress.

## Background

Due to "outdated" hardware I switched one computer to Linux. As a hobby retro hardware game developer I have various small projects in progress and all of them are building all the code and converting all the assets to hardware-ready data by running varikous .bat files. For instance

* Convert.bat loads mostly .png files and run tools that convert to a format that can be used for displaying on Commodore 64
* Make.bat runs an assembler or my various 6502 assembly source code files, and finalizes the game by linking code and data.

It usually completes in seconds or at least significantly faster than a minute. And switching to Linux means I created corresponding .sh files to the .bat files and manually make sure they sync up after working on one computer or another. This is surprisingly error prone and tedious, so I figured I could make a tool to process .bat/.sh so I'd only need to keep one.

I named it by verbifying ".bat/.sh" and started implementing .bat/.sh-it. As always happens this turned into a loop of realizing a ton of dos commands are just in dos so after implementing one after another this process just didn't hold up. Each time I'd run batshit I'd go more crazy. So I stopped to think.

One thing this really big project could use would be checking last modified date on the inputs and outputs before deciding to run each command, kind of like `make` does. And `make` has this big rule setup that I don't really want to think about again but I can make my own rule format!

Basically I came up with something a hundred times simpler than `make` but that kept the one thing I wanted. And I also decided to make a simple-ish way to abbreviate the long command lines in the declaration.

I named it `bath` because it sounds like batsh without being awkward to pronounce. So now I'm running baths instead of going batshit crazy.
