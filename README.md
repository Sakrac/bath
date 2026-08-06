# Bath

Bath is a small build tool for running command pipelines from a single script. It is meant to replace handwritten .bat or .sh files for asset conversion, assembly, and other build steps.

A bath script is read top to bottom. The tools are declared first, then execution commands are run.

Bath is essentially `make` but for running various tools in a specific order rather than compiling all touched files for a project.

The repo for Bath is located at https://github.com/sakrac/bath

## Simplified rule based execution

While at first glance the bath file structure might be intimidating it is intended to be intuitive. The best way to think of it is probably this:

1. Set up each tool alias as a macro using the keywords $In, $Out and $Args
2. Execute the commands by putting `tool alias`, then `output` : `input` followed by per command `arguments`

I will keep experimenting to make the process more intuitive as I convert my own projets one by one.

Note that unicode files are *fully supported* and the .bath files are expected to be saved as utf-8 with or without BOM. There are test files with Japanese paths to test this but if anything unexpected happens just file an issue in github.

## Example

I converted the build files for a commodore 64 demo I made in 2019 to show a "working" example (it works on my computer). When changing a single assembly source this enables a 25x build time speedup.

[Example.md](EXAMPLE.md)

## Script structure

A bath file usually has two parts:

1. A tool declaration block started with `$Tools`
2. An execution block started with `$Execution`

## Directives

The current implementation recognizes these directives:

- `$Include` followed by a path to another Bath file. Includes are tracked so the same file is only loaded once.
- `$Tools` starts the tool declaration block.
- `$Execution` starts the execution block.
- `$Parallel` or `$Parallell` enables parallel execution for the following commands.
- `$Sequential` switches back to sequential execution after waiting for all parallel commands to finish.
- `$Sync` waits for all currently running parallel commands to finish before continuing, but leaves parallel mode as it was.
- `$Raw` regular command lines follow, always runs. Useful while porting a .bat or .sh file to a full .bath file
- `$Ignore` or `$IgnoreErrors` keeps running even if errors are enoountered including if input files are missing. `$Ignore off` will re-enable error checking.
- `$Error` enables error checking if disabled with a `$Ignore`

Directives ignore case so feel free to use any casing you are comfortable with, including uppercase spellings such as $TOOLS or $PARALLELL.

A cautionary note on parallelization: Make sure you don't use the result of a file in the same parallelize block, such as assembling a binary file that is included in another assembly source! This is my own first mistake using the tool.

## Tool declaration

A tool declaration looks like this:

```text
tool_alias tool_path argument_layout
```

The command line for each execution can reference:

- `$In` for the input path or paths
- `$Out` for the output path or paths
- `$Args` for per-line arguments supplied in the execution block

The replacement order is `$Args`, then `$In`, then `$Out`, so the per-line arguments can still refer to the file paths.

## Path filters

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

If an input file is not found this is an error and the execution stops at this point.

Note that for format is: **tool output : input arguments**

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
- `-clean` or `-clear` - delete output files and don't execute
- `-rebuild` - force commands to run even when the outputs are newer than the inputs. Can be combined with `-clear` to first delete then rebuild.
- `-verbose` - print extra diagnostic information
- `-echo_off` - skip printing the command lines before execution
- `-stats` - show timing and file counts
- `show_commands` - print all evaluated command lines and don't run or check file times.

## Example

This is a fictional .bath file

```
# Run this bath to build assets into an srd cartridge

$Tools

# assembler is simple but generate a listing file

assembler tools/assemble $In $Out -listing=list/$In.filename -bin

# gfxconv generates two output files from one png file

gfxconv tools/graphic_tool -image $In -chars $Out.1 -map $Out.2

# Start running commands

$Execution

gfxconv (bin/title.chr bin/title.map) : assets/title.png

$Parallel

# These commands can run in parallel
assemble bin/boot.bin : src/boot.s
assemble bin/game.bin : src/game.s

$Sequential

# This file references the output of the previous commands
# so run one command at a time from here.
assemble cart.bin : src/link.s

# Complete the process with a single raw command line

$Raw

tools/cartconv cart.bin cart.srd
```

## Background

Due to "outdated" hardware I switched one computer to Linux. As a hobby retro hardware game developer I have various small projects in progress and all of them are building all the code and converting all the assets to hardware-ready data by running varikous .bat files. For instance

* Convert.bat loads mostly .png files and run tools that convert to a format that can be used for displaying on Commodore 64
* Make.bat runs an assembler or my various 6502 assembly source code files, and finalizes the game by linking code and data.

It usually completes in seconds or at least significantly faster than a minute. And switching to Linux means I created corresponding .sh files to the .bat files and manually make sure they sync up after working on one computer or another. This is surprisingly error prone and tedious, so I figured I could make a tool to process .bat/.sh so I'd only need to keep one.

I named it by verbifying ".bat/.sh" and started implementing .bat/.sh-it. As always happens this turned into a loop of realizations.

A ton of `ms-dos` commands are just in `ms-dos` so after implementing one after another this process just didn't hold up. Each time I'd run batshit I'd go more crazy.

So I stopped to think.

One thing this really big project could use would be checking last modified date on the inputs and outputs before deciding to run each command, kind of like `make` does. And `make` has this big rule setup that I don't really want to think about again but I can make my own rule format!

Basically I came up with something a hundred times simpler than `make` but that kept the one thing I wanted. And I also decided to make a simple-ish way to abbreviate the long command lines in the declaration.

I named it `bath` because it sounds like batsh without being awkward to pronounce. So now I'm running baths instead of going batshit crazy!
