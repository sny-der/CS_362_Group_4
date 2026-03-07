P2Ping is a direct file transfer system that allows users to directly send and receive files to and from each other over a direct connection. Using a direct connection allows P2Ping to prevent a reliance on external servers or storage for file sharing, uses a simple interface to create a low barrier to entry when compared to other decentralized file transfer systems, and is not restricted by physical distance, unlike other direct connection technologies, like Bluetooth.

## Installing P2Ping -- Windows Only 
A Windows machine, Python, and the Python libraries customtkinter and Pillow are needed to run this program.

- If you don't have Python installed, please install the latest version [here](https://www.python.org/downloads/)
- If you don't have customtkinter or pillow installed, install it by running the command `pip -r requirements.txt` in a terminal AFTER installing Python and cloning this repository.
  - You can check if you have custom tkinter installed by running the command `pip show customtkinter` in a terminal. If it is not installed, the terminal will show an error message.
  - You can check if you have pillow installed by running the command `pip show pillow` in a terminal. If it is not installed, the terminal will show an error message.

1. First clone or download the P2Ping repository to a desired folder. Step by step instructions on how to do this can be found [here](https://docs.github.com/en/get-started/start-your-journey/downloading-files-from-github)
2. You can run P2Ping from the .exe included in the controller folder in the src directory for P2Ping. For ease of access, you can create a shortcut to the file by right clicking on it, selecting "Show more options", then selecting "Send to" and then selecting desktop which will create a desktop shortcut for the file.


Set up is complete and you are ready to run P2Ping! 

**Note:** P2Ping requires an IPv6 connection to run properly. You can check if you are using an IPv6 connection [here](https://test-ipv6.com/). If you do not have an IPv6 connection through your current network, you will have to switch to another network that has IPv6 allowed (i.e. mobile hotspot) in order to use P2Ping in its current state.


## Using P2Ping to commuicate with others
1. Click on the "Connect to Peers" button on the top right corner of the screen.
2. First decide on a local connection (if on the same wifi) or a public connection (across networks).
3. Next take the IP address listed and send it to your peer who is ready to connect to your device. Have your peer do the same on their end and send you their IP address.
4. Paste their IP address in the text box and hit enter.
5. A chat should open up and you can send messages to each other directly.
6. Simply exit the program when you are finished messaging each other. You can also end the connection and start a new one with a different peer if desired. 

NOTICE: File sharing and encrypting files and messages is still a work in progess. The contact list on the left side is still a work in progress. Currently messages are not saved. Any messages will be deleted when the application is terminated.

