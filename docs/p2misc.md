
## Common Pitfalls

- We do not test your iterator for thread-safe leaf scans. A correct
  implementation, however, would require the Leaf Page to throw a
  `std::exception` when it cannot acquire a latch on its sibling to
  avoid potential dead-locks.
- If you implement a concurrent B+Tree index correctly, every thread
  will always acquire latches from the header page to the bottom. When
  you release latches, make sure you release them in the same order
  (from the header page to the bottom).
- When implementing the page classes (Task 1), make sure you only add
  class fields of trivially-constructed types (e.g. `int`). Do not add
  vectors and do not modify `key_array_` and `value_array_`.

## Tree Visualization

BusTub includes a built-in tool
(`tools/b_plus_tree_printer/b_plus_tree_printer.cpp`)
for generating a graphical representation of your
B+Tree. This tool will help you check your solution for correct behavior
to ensure it is performing the splits and merges correctly.

To generate a dot file after constructing a tree:

```sh
# To build the tool
$ mkdir build
$ cd build
$ make b_plus_tree_printer -j
$ ./bin/b_plus_tree_printer
>> ... USAGE ...
>> 5 5 // set leaf node and internal node max size to be 5
>> f input.txt // Insert into the tree with some inserts
>> g my-tree.dot // output the tree to dot format
>> q // Quit the test (Or use another terminal)
```

You should now have a `my-tree.dot` file with the
[DOT](https://en.wikipedia.org/wiki/DOT_(graph_description_language))
file format in the same directory as your test binary, which you can
then visualize with a command line visualizer or an online visualizer:

1.  Dump the content to
    [http://dreampuf.github.io/GraphvizOnline/](http://dreampuf.github.io/GraphvizOnline/).
2.  Or download a [command line
    tool](https://graphviz.org/download/)
    for your platform. Then create a PNG file of your tree using this
    command: `dot -Tpng -O my-tree.dot`

You can also compare with our [reference solution running in your
browser](https://15445.courses.cs.cmu.edu/spring2026/bpt-printer/).
