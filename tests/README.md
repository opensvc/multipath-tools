# multipath-tools unit tests

Unit tests are built and run by running `make test` in the top directory,
or simply `make` in the `tests` subdirectory. The test output is saved as
`<testname>.out`. The test programs are called `<testname>-test`, and can
be run standalone e.g. for debugging purposes.

## Running tests under valgrind

The unit tests can be run under the valgrind debugger with `make valgrind`
in the `tests` directory, or `make valgrind-test` in the top directory.
If valgrind detects a bad memory access or leak, the test will fail. The
output of the test run, including valgrind output, is stored as
`<testname>.vgr`.

## Running tests manually

`make test` or `make -C test "$TEST.out"` will only run the test program if
the output files `$TEST.out` don't exist yet. To re-run the test, delete the
output file first. In order to run a test outside `make`, set the library
search path:

    cd tests
    export LD_LIBRARY_PATH=.:../libmpathutil:../libmpathcmd
	./dmevents-test  # or whatever other test you want to run

## Controlling verbosity for unit tests

Some test programs use the environment variable `MPATHTEST_VERBOSITY` to
control the log level during test execution.

## Notes on individual tests

### Tests that require root permissions

The following tests must be run as root, otherwise some test items will be
skipped because of missing permissions, or the test will fail outright:

 * `dmevents`
 * `directio` (if `DIO_TEST_DEV` is set, see below)

To run these tests, after building the tests as non-root user, change to the
`tests` directory and run `make test-clean`; then run `make` again as root.

### directio test

This test includes test items that require a access to a block device. The
device will be opened in read-only mode; you don't need to worry about data
loss. However, the user needs to specify a device to be used. Set the
environment variable `DIO_TEST_DEV` to the path of the device.
After that, run `make directio.out` as root in the `tests` directory to
perform the test.

With a real test device, the test results may note be 100% reproducible,
and sporadic test failures may occur under certain circumstances.
It may be necessary to introduce a certain delay between test
operations. To do so, set the environment variable `DIO_TEST_DELAY` to a
positive integer that determines the delay (in microseconds) after each
`io_submit()` operation. The default delay is 10 microseconds.

*Note:* `DIO_TEST_DEV` doesn't have to be set during compilation of
`directio-test`. This used to be the case in previous versions of
multipath-tools. Previously, it was possible to set `DIO_TEST_DEV` in a file
`tests/directio_test_dev`. This is not supported any more.

## Adding tests

The unit tests are based on the [cmocka test framework](https://cmocka.org/),
and make use of cmocka's "mock objects" feature to simulate how the code behaves
for different input values. cmocka achieves this by modifying the symbol
lookup at link time, substituting "wrapper functions" for the originally
called function. The Makefile contains code to make sure that `__wrap_xyz()`
wrapper functions are automatically passed to the linker with matching
`-Wl,--wrap` command line arguments, so that tests are correctly rebuilt if
wrapper functions are added or removed.

### Making sure symbol wrapping works: OBJDEPS

Special care must be taken to wrap function calls inside a library. Suppose you want
to wrap a function which is both defined in libmultipath and called from other
functions in libmultipath, such as `checker_check()`. When `libmultipath.so` is
created, the linker resolves calls to `checker_check()` inside the `.so`
file. When later the test executable is built by linking the test object file with
`libmultipath.so`, these calls can't be wrapped anymore, because they've
already been resolved, and wrapping works only for *unresolved* symbols.
Therefore, object files from libraries that contain calls to functions
which need to be wrapped must be explicitly listed on the linker command line
in order to make the wrapping work. To enforce this, add these object files to
the `xyz-test_OBJDEPS` variable in the Makefile.

### Using wrapper function libraries: TESTDEPS

Some wrapper functions are useful in multiple tests. These are maintained in
separate input files, such as `test-lib.c` or `test-log.c`. List these files
in the `xyz-test_TESTDEPS` variable for your test program if you need these
wrappers.

### Specifying library dependencies: LIBDEPS

In order to keep the tests lean, not all libraries that libmultipath
normally pulls in are used for every test. Add libraries you need (such as
`-lpthread`) to the `xyz-test_LIBDEPS` variable.
