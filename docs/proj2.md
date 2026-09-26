#  [Project #2 - B+Tree]
# Overview

In this project, you will implement a
[B+Tree](https://en.wikipedia.org/wiki/B%2B_tree)
index in your database system. A B+Tree is a balanced search tree in
which the internal pages direct the search and leaf pages contain the
actual data entries. The index provides fast data retrieval without
searching every row in a database table, enabling rapid random lookups
and efficient scans of ordered records. Your implementation will support
thread-safe search, insertion, and deletion (including splitting and
merging nodes), and an iterator for in-order leaf scans. You need to
complete the following tasks:

- [Task #1 - B+Tree Pages](../docs/p2t1.md)
- [Task #2 - B+Tree Operations (Insertion, Deletion, and Point Search)](../docs/p2t2.md)
- [Task #3 - Index Iterator](../docs/p2t3.md)
- [Task #4 - Concurrency Control](../docs/p2t4.md)

Your work in Project #2 depends on your implementation of the buffer
pool and page guards from [Project #1](../docs/proj1.md).

# Project Specification

We have provided stub classes that define the APIs that you must
implement. You should **not** modify the signatures of
these pre-defined functions. Similarly, you
should not remove existing member variables from the code we provide.
You may add functions and member variables to these classes to implement
your solution.

# Instructions
<!-- Refer to ../docs/p2misc.md for additional hints. -->

## Testing

You can test your B+ Tree implementation locally using the folloing
tests:

- `test/storage/b_plus_tree_insert_test.cpp`
- `test/storage/b_plus_tree_sequential_scale_test.cpp`
- `test/storage/b_plus_tree_delete_test.cpp`
- `test/storage/b_plus_tree_concurrent_test.cpp`

We **strongly** encourage you to write additional test cases for
yourself to better understand your implementation. Although the public
tests cover the most use cases, the grading script will test with more
complicated access patterns.

You can test the individual components of this assigment using our
testing framework. We use GTest for unit test cases.

You can compile and run each test individually from the command line:

```sh

$ mkdir build
$ cd build
$ make b_plus_tree_insert_test -j$(nproc)
$ ./test/b_plus_tree_insert_test
```

You can also run `make check-tests` to run all the test cases. Note that
some tests are disabled as you have not implemented future projects. You
can disable tests in GTest by adding a `DISABLED_` prefix to the test
name.

## Development Hints

You can use `BUSTUB_ASSERT` for assertions in debug mode. Note that the
statements within `BUSTUB_ASSERT` will NOT be executed in release mode.
If you have something to assert in all cases, use `BUSTUB_ENSURE`
instead.

We encourage you to use a graphical debugger to debug your project if
you are having problems.

If you are having compilation problems, running `make clean` does not
completely reset the compilation process. You will need to delete your
build directory and run `cmake ..` again before you rerun `make`.
