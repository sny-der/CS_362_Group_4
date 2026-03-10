The developer documentation should include at least the following information:

- How to obtain the source code. If your system uses multiple repositories or submodules, provide clear instructions for how to obtain all relevant sources.
-The layout of your directory structure. What do the various directories (folders) contain, and where to find source files, tests, documentation, data files, etc.
- How to build the software. Provide clear instructions for how to use your project’s build system to build all system components.
- How to test the software. Provide clear instructions for how to run the system’s test cases. In some cases, the instructions may need to include information such as how to access data sources or how to interact with external systems. You may reference the user documentation (e.g., prerequisites) to avoid duplication.
- How to add new tests. Are there any naming conventions/patterns to follow when naming test files? Is there a particular test harness to use?
- How to build a release of the software. Describe any tasks that are not automated. For example, should a developer update a version number (in code and documentation) prior to invoking the build system? Are there any sanity checks a developer should perform after building a release?


### For re-compling the C code
The c file "2ipv6test_windows.c" is complied with this command:
- ```gcc -Wall -Wextra -O2 2ipv6test_windows.c -o contest_pybridge.exe -lws2_32 -lbcrypt```

### Custom Libraries
This project uses extra python libriaries that need to be installed. Use command:
```pip install -r requirements.txt```

## Splitter Component

split_file uses os to get the path to the uploaded file, and uses chunk to copy 1mb chunks of the selected file into memory before in an array before the metadata for the file (name, size, length of the chunk array, the chunk array) is returned by splitter. reassemble_file writes chunks out of an array passed to it to a file path that is passed to it, iterating through the array until the index has reached its maximum value for the length of the array. Both take the filepath for the file as arguments, so to change the filepath or default locations the argument to each component from whatever function is calling it can be changed to change either the file to be uploaded or the saved location of the file. The size of the chunk can be changed by altering the chunk_size variable. It is currently set to 1mb. Currently, splitter_tester contain the driver functions that enable the functions within splitter to run. The variables and format can be modified to change the locations of the files reassembler reassembles, or how split_file is passed files.
