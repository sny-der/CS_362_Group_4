The developer documentation should include at least the following information:

- How to obtain the source code. If your system uses multiple repositories or submodules, provide clear instructions for how to obtain all relevant sources.
-The layout of your directory structure. What do the various directories (folders) contain, and where to find source files, tests, documentation, data files, etc.
- How to build the software. Provide clear instructions for how to use your project’s build system to build all system components.
- How to test the software. Provide clear instructions for how to run the system’s test cases. In some cases, the instructions may need to include information such as how to access data sources or how to interact with external systems. You may reference the user documentation (e.g., prerequisites) to avoid duplication.
- How to add new tests. Are there any naming conventions/patterns to follow when naming test files? Is there a particular test harness to use?
- How to build a release of the software. Describe any tasks that are not automated. For example, should a developer update a version number (in code and documentation) prior to invoking the build system? Are there any sanity checks a developer should perform after building a release?

# Source Code
All source code is either contained in this repository or within the required libraries. Explanations of the files that make up the different components can be found below.

## Repo Structure

### Workflows
A CI workflow is contained in the workflows folder. It verifies the validity of python code.

### Data
The data folder contains a placeholder for a not yet implemented database, intended only to store contact information or chat logs as the user chooses.

### Documentation
The documentation folder contains files for testing the C file, test logs, and the dev guide.

#### Test Logs
Test logs contains the results of performed tests.

### Reports
Reports contains progress reports from the development team throughout the development process.

### SRC
SRC contains the source code created by the development team in three subdirectories; controller, model, and view; and contains the main file.

#### Controller
Controller is used to contain the intermediary components that communicate between the model and view components.

#### Model
Model contains the pybridge c file, the .exe it creates, and the python files that communicate directly with the pybridge. Model is used to contain the 'backend' functional components.

#### View 
View contains the GUI, and is used to contain the frontend user-centric components.

### Root files
The main directory contains all subdirectories, and all outside facing documents, such as the readme and user-manual, in order to properly convey the functions and use of P2Ping.

## For re-compling the C code
The c file "2ipv6test_windows.c" is complied with this command:
- ```gcc -Wall -Wextra -O2 2ipv6test_windows.c -o contest_pybridge.exe -lws2_32 -lbcrypt```

## Custom Libraries
This project uses extra python libriaries that need to be installed. Use command:
```pip install -r requirements.txt```

## Testing
Tests can be performed by following provided tests, or by implementing new tests in the same pattern for that component. Testing the C file specifically requires automation, as demonstrated in Ctest, along with the controller components. Other components, like the GUI, can be tested either automatically or manually. All test files should be named after the component they test in order to clearly show what the intended purpose of the tests is, such as Ctest, which tests the C file. All added test files should be kept in the directory with their components, and the results should be stored in the test_logs folder.  Additionally, tests can be added to the provided workflow.

## New Releases
All new features and bug fixes should be created in uniquely named branches of the repository. All new releases will require an updated version number, following the major minor pattern (1.0, 1.1, ...) which will have to be manually updated in README.md and in main.py. The build system will automatically trigger when the new version branch is merged into main. Verification testing should be performed prior to merging, to ensure the new functionality works as intended, and does not worsen the performance, security, or usability of the app as it was prior to the update. All new functionality should be designed to improve these aspects of the program, while still maintaining P2Ping as a peer-to-peer messaging and file transfer app, designed to avoid reliance on third-party systems and provide an easy to understand user interface and experience.

# Component/File explanation

### Splitter Component

split_file uses os to get the path to the uploaded file, and uses chunk to copy 1mb chunks of the selected file into memory before in an array before the metadata for the file (name, size, length of the chunk array, the chunk array) is returned by splitter. reassemble_file writes chunks out of an array passed to it to a file path that is passed to it, iterating through the array until the index has reached its maximum value for the length of the array. Both take the filepath for the file as arguments, so to change the filepath or default locations the argument to each component from whatever function is calling it can be changed to change either the file to be uploaded or the saved location of the file. The size of the chunk can be changed by altering the chunk_size variable. It is currently set to 1mb. Currently, splitter_tester contain the driver functions that enable the functions within splitter to run. The variables and format can be modified to change the locations of the files reassembler reassembles, or how split_file is passed files. Splitter is implemented in the contest_pybridge to facilitate sending and receiving functionality, but maintains the same fundamental structure as the python splitter functions.

### Contest_pybridge (Sender/Receiver Component)

contest_pybridge.exe is used by network_main to send/receive packets. It takes a port number as an argument to enable loopback communication between the pybridge and network_main so that network_main is able to make use of the functionality in pybridge. It makes use of UDP protocols with additional features to handle packet rerequesting and integrity checking. Places received files in a folder marked ReceivedFiles in the model directory where P2Ping has been installed. Sends a request to a stun server to receive an IPv6 address for the user. Queues packets into linked lists, which are managed by bitmap to reduce memory load. Controls connections by defining sockets. Defines packet types to ensure proper packet handling. Handles all receiving and sending of packets. 


### Main (Controller Component)
Interface between GUI and NetworkMain. Contains event handlers for GUI, and functionality to return or send sent or received information and packets. Handles file selection for uploads and transfers.

### NetworkMain_windows (Controller Component)
Interface between contest_pybridge and main. Expects the .exe to be named contest_pybridge.exe. The Python file calls this program whith a loopback communication port as an argument. The C program then sends its loopback communication port back to the C file. This is how the C and python files communicate. When the python program wants to send the C file a message, it will create a packet with an 8 character header and appropriate content. This will then be sent to the C program. The C program will do the same to the python file. The python file will use the appropriate messages to tell the C program what to do. The C progam will then analyse the incoming orders and act accordingly. When the C file is sending a message over the open internet, it uses a similar system. The packets are the same, but this time with an IP address of a different computer and a 12 character body header instead of an 8 character one. All of the python functions and cross device communication system are based on these protocols. 


### GUI
Creates the GUI for the app. Handles initial definition of connection mode. Resolves displayed information, updates gui when connection is active, and messages are sent or received. Uses customtkinter and PIL libraries for formatting.
