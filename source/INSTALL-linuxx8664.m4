The archive ffigen-bin-PLATFORM-gcc-GCC_VERSION.tar.gz contains:

bin/h-to-ffi.sh         a shell script which simplifies invocation of the
                        interface translator
ffigen/bin/ffigen       the interface translator itself, a modified version
                        of the GCC Objective C frontend
ffigen/ginclude/*        header files specific to this compiler version

The archive's contents can be used directly if the archive is
extracted  in some directory that contains a "bin" subdirectory on the
user's PATH, e.g., if /usr/local/bin exists on $PATH, then

# may need to be root to write to /usr/local, if /usr/local even exists
shell> tar xfz /path/to/ffigen-bin-PLATFORM-gcc-GCC_VERSION.tar.gz -C /usr/local

and likewise for /usr (though /usr may be a poor choice) or ~ (if ~/bin is
on $PATH).

It's also possible to extract the archive into a working directory and
install its contents in unrelated parts of the file system; this may
require editing the paths in bin/h-to-ffi.sh.

