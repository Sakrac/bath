# Bath

Bath is intended to replace existing .bat / .sh files that run various tools to generate assets and programs to build something out of the outputs. The command line tools will be started in the order they are declared in the .bath file.

First declare the tools and the command line syntax. A line with \$Tool indicates that the tools declaration is following. The tool alias means that one tool can have different command line setups.

After the tools are declared the \$Execution block begins where each line contains the tool alias to use followed by the output(s) a colon then the input(s) and then additional arguments for this execution.

At the end a \$Finalize block begins that should just be raw command lines

## Directives

* \$Include is follwed by a path to another .bath file to reference. Includes are tracked so only one instance will be included even if the same .bath is referenced multiple times
* \$Tool starts a tool declaration block
* \$Execution starts rumming command line tools
* \$Parallelize starts running commands in multiple threads, add a number to limit the number of simultaneous threads (default 8)
* \$Sync waits for all the parallellized executions to complete before continuing
* \$Finalize starts the finalize execution. This is an optional and any parallellized execution will finish first.

## Tool declaration

\<tool alias\> \<tool path\> \<arguments\>

The inputs and outputs can be referenced by using \$In and \$Out in the arguments block.

In addition the arguments block can reference \$Args that inserts additional arguments supplied for each command after the outputs and inputs.

Note that \$Args is applied before \$In or \$Out so the per line custom arguments can refer to \$In or \$Out.

It can be convenient to reference for instance just the filename without path or extension in the arguments so there are special filters for that

$In.filename will evaluate to just the filename part and if you want to for example generate a listing file matching the source filename you can do it like this:

```
x65 ..\x65\x65 \$In -lst=lst\$In.filename.lst -obj obj\$Out -srcdbg
```

note that .filename will resolve to the first input/output if there are multiple, the dot suffixes can however be chained.

Available filters:
* \$In.filename - just the filename, no path, not extension
* \$In.noext - trim the extension from the path
* \$In.path - only the path without a trailing '/'
* \$In.1 - first file of a multiple
* \$In.<number> - indexed file of a multiple

\$Out filters are the same as the \$In filters. And only the first file if multiple are speficied will be considered for filters.

## Execution

Inserting a line with `$Execution` starts the execution pass where each line starts with a tool alias followed by Outputs : Inputs \<optional args>

```
x65 obj/Game.x65 : src/game.s
```

In this case .x65 is a linkable object file and .s is the source code that generates the object file.

## Multiple inputs and outputs for a single command

Put the inputs or outputs into parenthesis to supply multiple files, for instance an image might produce a charset, a screen map and a color map:

```
c64gfx (data/title.chr data/title.scr data/title.col) : assets/title.png -textmc
```

The tool declaration for c64gfx might look like this:

```
c64gfx ../C64Gfx/C64Gfx \$in \$Args -out=\$Out.noext
```

Multiple inputs works the same way:

```
cruncher LevelBlob : (level.map level.chr level.col)
```

## Command line arguments for bath

* -clean: deletes all the output files and does not run commands by default
* -rebuild: forces all commands to run regardless of time, can be combined with -clean
* -echo_off: don't print command lines as they are executed
* -verbose: output a lot more lines
* -simulate: generate command lines and print them but don't execute them
* -single: ignore $Parallelize

## Background

Due to "outdated" hardware I switched one computer to Linux. As a hobby retro hardware game developer I have various small projects in progress and all of them are building all the code and converting all the assets to hardware-ready data by running varikous .bat files. For instance

* Convert.bat loads mostly .png files and run tools that convert to a format that can be used for displaying on Commodore 64
* Make.bat runs an assembler or my various 6502 assembly source code files, and finalizes the game by linking code and data.

It usually completes in seconds or at least significantly faster than a minute. And switching to Linux means I created corresponding .sh files to the .bat files and manually make sure they sync up after working on one computer or another. This is surprisingly error prone and tedious, so I figured I could make a tool to process .bat/.sh so I'd only need to keep one.

I named it by verbifying ".bat/.sh" and started implementing .bat/.sh-it. As always happens this turned into a loop of realizing a ton of dos commands are just in dos so after implementing one after another this process just didn't hold up. Each time I'd run batshit I'd go more crazy. So I stopped to think.

One thing this really big project could use would be checking last modified date on the inputs and outputs before deciding to run each command, kind of like `make` does. And `make` has this big rule setup that I don't really want to think about again but I can make my own rule format!

Basically I came up with something a hundred times simpler than `make` but that kept the one thing I wanted. And I also decided to make a simple-ish way to abbreviate the long command lines in the declaration.

I named it `bath` because it sounds like batsh without being awkward to pronounce. So now I'm running baths instead of going batshit crazy.