P2Ping is a direct file transfer system that allows users to directly send and receive files to and from each other over a direct connection. Using a direct connection allows P2Ping to prevent a reliance on external servers or storage for file sharing, uses a simple interface to create a low barrier to entry when compared to other decentralized file transfer systems, and is not restricted by physical distance, unlike other direct connection technologies, like Bluetooth.

## Installing P2Ping -- Windows Only 
This software will need to compile a C file in the terminal and will need to be launched from the terminal using a python command. A g++ compiler along with the Python library for customtkinter is needed.

- If you don't have the g++ compiler installed, install it [here](https://code.visualstudio.com/docs/cpp/config-mingw) and follow the instructions for adding it to your system path.
- If you don't have Python installed, please install the latest version [here](https://www.python.org/downloads/)
- If you don't have customtkinter installed, install it by running the command `pip install customtkinter` in a terminal AFTER installing Python.
- If you don't have pillow installsed, install it by running the command `pip install pillow` in a terminal AFTER installing Python.


1. First clone or download the P2Ping repository to a desired folder. Step by step instructions on how to do this can be found [here](https://docs.github.com/en/get-started/start-your-journey/downloading-files-from-github)
2. Navigate to the controller directory in a terminal. `path/to/project/folder/src/controller/`
3. Compile the c code with: `gcc -Wall -Wextra -O2 ipv6test.c -o contest_pybridge.exe -lws2_32`

Set up is complete and you are ready to run P2Ping! 

## How to run P2Ping -- Windows Only
P2Ping is started through the terminal. Once in the controller directory of the P2Ping folder, run the command:
- `python WinNetworkMain.py`

in the terminal. If set up correctly the application will open a new window called P2Ping. You are ready to start connecting to your peers! 

## Using P2Ping to commuicate with others
After you have ran `python WinNetworkMain.py` in the controller directory and the application has opened in a new window:
1. Click on the "Connect to Peers" button on the top right corner of the screen.
2. First decide on a local connection (if on the same wifi) or a public connection (across networks).
3. Next take the IP address listed and send it to your peer who is ready to connect to your device. Have your peer do the same on their end and send you their IP address.
4. Paste thier IP address in the text box and hit enter.
5. A chat should open up and you can send messages to each other directly.
6. Simply exit the program when you are finished messaging each other. You can also end the connection and start a new one with a different peer if desired. 

NOTICE: File sharing and encrytping files and messages is still a work in progess. The contact list on the left side is still a work in progress. Currently messages are not saved. Any messages will be deleted when the application is terminated.

