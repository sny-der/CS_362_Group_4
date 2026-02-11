# P2Ping
# Team Members:

- Charles Weber: Code Integration Coordinator
    * Ensure all features have interfaces to transfer data between, when necessary
    * Ensure all components can function concurrently within the same program
- Abram Gallup: Architecture Design Coordinator
    * Lead in specifying overall design of the program
    * Lead in identifying & defining components
- Jonathan Snyder: QA Coordinator
    * Ensure program functions correctly
    * Lead in defining & implementing testing plan
- Arjun Rahul Bhave: Product & Requirements Coordinator
    * Lead in identifying functional and nonfunctional requirements
    * Lead in identifying most desired features


# Communication:
Our primary channel of communication will be discord, and we will have weekly in-person meetings.
# Rules:
- Maintain courtesy, respect, and professionalism in the group chats and at the meetings.
- Any conflict will be mediated by non-involved group members, or resolved through voting.
- Meeting notes will be recorded and shared to the group chat, to keep a record and to inform any members who may have had to miss the meeting.
- We will strive to maintain a welcoming environment at all times.



# Product description 

## Product Title: P2Ping

## Abstract: 
P2Ping is a direct file transfer system that establishes a direct connection between computers through the internet in order to transfer files directly between users' computers. Using a direct connection allows P2Ping to prevent a reliance on external servers or storage for file sharing, uses a simple interface to create a low barrier to entry when compared to other decentralized file transfer systems, and is not restricted by physical distance, unlike other direct connection technologies, like Bluetooth.

## Goal: 

We aim to create an encrypted p2p messaging/file transfer system that does not require the use of an external server or service to store the data, instead allowing for a direct file transfer between 2 machines.

## Current practice: 

Files are transferred through services like Google Drive, which has restrictions on storage space, and uses a third party server. Torrenting is good for large amounts of data, but is difficult for less technical users to use. Some other direct connection methods, like scp or ftp, are unsecured and transmit unencrypted traffic. Some other direct connection technologies, like Bluetooth, are restricted by the physical distance between computers.
   
## Novelty: 

- Simple interface for decentralized file transfer system
- Direct, secure connection unrestricted by physical distance
- No reliance on external servers or storage

## Effects: 
Peers will be able to directly send encrypted messages without having to use an external server, and without being limited by a third party’s maximum transfer size, enhancing ease and speed of communication and collaboration. 

## Technical approach: 

The program will use udp transfer protocols, to give us more control over the packets themselves and to allow us to holepunch firewalls or NATs more effectively. Using UDP also allows us to send and receive data without first establishing handshakes or other means of connection, which gives us a lightweight approach to establishing and maintaining connections without relying on external infrastructure. P2Ping will use an encryption protocol based on the Diffie-Hellman protocol, which allows users to generate shared private keys from public keys, which reduces overhead requirements and still results in an acceptable level of security. We will use a simplistic approach to gui, to prevent confusion and to maintain an ease of understanding. 

## Risks: 

- App might not work on different operating systems

> Likelihood of Occurring: Medium

> Impact: Medium

> Evidence: Different operating systems handle file transfers differently, and resolving those formats could prove challenging to us.

> Steps: We are researching how to resolve these formats for transfers between different OS, and we plan on testing it by using the app on a different OS

> Mitigation: The app will be kept Windows-only until we can successfully interact across operating systems.

> Change from first submission: This requirement and this assessment are new additions for this milestone.

- App might not be functional in different testing environments

> Likelihood of Occurring: Low

> Impact: Medium

> Evidence: Much like the problem with different operating systems, our app might not be functional or compatible with testing environments based on different architecture than our app, and could reduce our capability to test the app

> Steps: We will identify any testing environments that will work with our app to perform tests

> Mitigation: We will identify testing environments that will allow for full test coverage of our app, our the cause of incompatibility

> Change from first submission: This requirement and this assessment are new additions for this milestone.
  
- Not being able to handle large file transfers 
- Not being able to resolve local addresses, to establish connections. 
- Using Python, the GUI could freeze up while the program is processing data to send or receive data

# Use Cases
## Use Case 1:
Actors: Users who want to receive files through this software

Preconditions: The user has the software downloaded, and has a desire to receive files through this software

Triggers: The user launches the software.

Postcondition: The user views a valid address for their computer, which allows others to connect to them

Steps:
- The user launches the software
- The user opens the "Personal Info" submenu
- The user views their address
- The user sends this address to someone else with this software

Extensions:
- The user is able to download files that are sent to them, or send those files to another user

Exception:
- The user is unable to view their own address, or is unable to be found through the software

## Use Case 2 - Sending Data

Actors: The user sending data, and the user receiving data.

Triggers: Sender selects a file and chooses to send it to an existing contact on the user                interface in the file send place. 

Preconditions: 
- Both sender and receiver have the application installed and running.
- The sender has already added Receiver as a contact
- The sender has selected the receiver
- The file to be sent exists on the Sender’s device and is accessible.
- The receiver is allowing file receiving at this time.
  
Postconditions:
- The file is securely transferred end-to-end encrypted from Sender to Receiver.
- The Receiver receives and can access/decrypt the file on their device.
- The Sender receives confirmation that the transfer completed successfully.
- No intermediate server or third party ever accesses the file content.
- If the Receiver was offline, the file is queued on the Sender’s device and delivered           automatically once the Receiver comes online. (stretch goal)

List of Steps:
1. The Sender opens the chat/accesses the Receivers contact info
2. The Sender selects the “Send File” option (drag and drop)
3. The Sender chooses the file from their device
4. The app computes a hash (?) of the file and prepares metadata.
5. The app checks the Receiver’s online status.
6. The app initiates or re-uses the established P2P connection.
7. The app performs key agreement (?)
8. The app encrypts the file in chunks 
9. The app streams the encrypted chunks + metadata to Receiver
10. The Receiver’s app receives the chunks, decrypts and verifies
11. The Receiver’s app saves the file locally
12. The Receiver’s app sends a delivery acknowledgement to Sender
13. The Sender’s app displays “File sent successfully”
14. Both Sender and Receiver logs transfer.

Extensions / Variations of the Success Scenario
- Variation 1: Receiver Offline at Send Time
   - After step 5: App detects Receiver is offline
   - The app encrypts the file and stores it locally in an encrypted queue.
   - Background process periodically checks Receiver status.
   - When the receiver comes online, resume from step 6.
   - Sender sees status: “Queued – will send when Receiver is online”
- Variation 2: Large File with Resume Support
   - During transfer (step 9), if connection drops, both sides track last chunk
   - On reconnect, resume from the last acknowledged chunk 
- Variation 3: Multiple Files or Mixed with Text
   - Sender attaches multiple files or text + file in one message
   - Treated as a batch, encrypted/send sequentially or in parallel if allowed
  
Exceptions: failure conditions and scenarios:
- Receiver Never Comes Online: Queue persists indefinitely, or canceled by Sender, or            configurable timeout. 
- Connection Fails Repeatedly: After retries with different methods, queue file and notify       Sender. (“Delivery delayed – network issues”). 
- Integrity Check Failed on Receiver Side: Receiver rejects file, sends error to Sender, and     Sender notified.
- Key Mismatch or Authentication Failure: Connection refused, Sender prompted to re-verify       contact information
- Insufficient Storage / Space on Receiver: Receiver app rejects incoming transfer, notifies     Sender (“Receiver has insufficient space”)
- Sender Cancels Mid-Transfer: Transfer aborts gracefully, partial data discarded on both        sides, Sender sees “Canceled”. 

## Use Case 3


## Use Case 4
- Actors  
  - Sender (User initiating the message)  
  - Receiver (User receiving the message)  

- Triggers  
  - Sender opens a chat window or selects a contact and types a message or sends a “ping.”  

- Preconditions  
  - Both Sender and Receiver have the application installed.  
  - Sender and Receiver are added as contacts.  
  - Sender has selected the Receiver.  
  - Receiver is either online, or offline messaging is enabled (stretch goal).  
  - A secure P2P connection can be established or resumed.  

- Postconditions  
  - The message is delivered end-to-end encrypted from Sender to Receiver.  q
  - Receiver can read the decrypted message.  
  - Sender receives delivery or read confirmation (if enabled).  
  - Message logs are stored locally for both users.  
  - No third-party server stores or accesses the message contents.  

- Main Success Scenario
  - Sender opens the application.  
  - Sender selects a Receiver from the contact list.  
  - Sender types a text message or presses a “Ping” button.  
  - The app generates message metadata (timestamp, sender ID, message ID).  
  - The app checks Receiver’s online status.  
  - The app establishes or reuses an encrypted P2P connection.  
  - The app performs secure key agreement (if not already active).  
  - The message is encrypted locally.  
  - The encrypted message packet is transmitted to Receiver.  
  - Receiver’s app verifies packet integrity.  
  - Receiver’s app decrypts the message.  
  - The message is displayed in the chat interface.  
  - Receiver’s app sends a delivery acknowledgement.  
  - Sender’s app updates status to “Delivered”.  
  - Both sides log the message.  

# Non-Functional Requirements
- The software should have an easy to use gui
- The software should not compromise the security of the user's machine
- The software should be able to support a multitude of users

# External Requirements
- The software should be able to detect invalid address inputs, and prompt the user to reenter a valid address
- The software should reject invalid file types
- The software should be downloadable from GitHub, and should contain a text file instructing the user on how to set up and launch the software
- The software documentation should be clear enough for others to improve its functionality

# Software Toolset

## GUI Tools

#### Python Tools
- [PyQt](https://wiki.python.org/moin/PyQt)
- [PySide](https://www.pythonguis.com/pyside6/) PyQt extension
- QT Designer
#### HTML/CSS Plugin
- [Electron Framework](https://www.electronjs.org/)

## Software Design

### Architecture Design Pattern
- Peer-to-peer

### Components 

- File Splitter - disassembles and reassembles files to and from packets
- GUI - provides interface for user, made with PySide/PyQT
- Sender: Sends packets
- Receiver: Receives packets
- Connection Manager: Creates and manages connection between users
- Location Finder: Finds/translates IP addresses to enable direct connection
- Main: Handles overall execution of program
- File Manager: Handles data storage

- [Link to Components Diagram](https://docs.google.com/drawings/d/1WdjOrkrxiTuXHW_l_BpIKTf7qZ1-0r-29KI-O6DZVCI/edit)

### Interfaces
- File Manager: Interface between GUI/File Splitter and OS file system. Allows user to select files to upload, and to download files to their machine's storage. 
- File Splitter/Sender: File splitter passes packets to Sender to send them to the connected user.
- Receiver/File Splitter: Receiver sends packets to the File Splitter to be recombined.
- File Splitter/File Manager: File Splitter imports files to split them into packets, and exports recombined files from/to the File Manager.
- Connection Manager/OS network protocol: The connection manager will use network protocols on the machine to establish and maintain connections with other users
- Connection Manager/Sender/Receiver: The connection manager will send address information to the sender and receiver to ensure they are sending information to the correct place and receiving it from the correct place.
- Location Handler/Connection Manager: The location handler will send address information to the Connection Manager to provide the correct target for connection.
### Data

- SQL: Each user will run their own local instance of the database. The database includes the stretch goals, storing:
   - User Information: user id(stored as a hash of their public key), a user name, and              optionally, a static ip address.
   - message logs: a message id, the content of the message, timestamp, sender id, and              receiver id.
   - file transfer logs: file transfer id, file name, file size, timestamp, status (if the          file completely sent, failed, or canceled), sender id, and receiver id.

 - High level database schema:
![Relational Schema](images/relational_schema.png)


### Alternatives

- TCP v. UDP
- Electron Framework - can be used to create the GUI instead of python native functions.
- Encryption protocols


### Classes / Units of Abstraction


## Coding Guideline
- Python: [PEP 8](https://realpython.com/python-pep8/#code-layout)

# Team Roles

## Code Integration Coordinator
A code integration coordinator is necessary to ensure that all code written by team members works together to create required functionality. Charles is suited to this role because he has experience integrating software.

## Architecture Design Coordinator
An architecture design coordinator is necessary to define a structure for the software, and to ensure the software maintains that structure. Abrum is suited to this role because he has experience designing software structure.

## QA Coordinator
A qa coordinator is necessary to ensure the software performs as desired, and does not have any internal bugs that prevent its operation. Jonathan is suited to this role because he has experience troubleshooting software.

## Product & Requirements Lead
A product & requirements lead is necessary to elicit requirements of the software, and to determine the most user friendly methods to fulfill those requirements. Arjun is suited to this role because he has experience determining requirements & user expectations. 

# Schedule

| Task  | Effort  | Dependencies  |
|---|---|---|
| Milestone 1  | 1 week  | none |
|Milestone 2| 1 week | Milestone 1 |
| Milestone 3  | 2 weeks  |  Milestone 2 |
| Create Schedule  | 1 day  | Structure  |
| Design Architecture  | 2 Days  | Requirements  |
| Create Structure Design  | 2 Days  | Architecture  |
| Create GUI Mockup  | 1 day  | Structure  |
| Milestone 4  | 2 weeks  | Milestone 3  |
| Build basic GUI setup  | 3 days  | GUI Mockup  |
| Implement Holepunch  | 3 days  |   |
| Implement Sender  | 3 days  | Holepunch  |
| Implement Receiver  | 3 days  | Holepunch  |
| Implement Up/Download Tracker  | 1 day  | Sender/Receiver  |
| Implement File Shredder  | 1 week  | Structure  |
| Implement File Recombiner  | 2 days  | File Shredder  |
| Implement File Manager  | 2 days  | Recombiner  |
| Implement Encryption  | 1 week  | Shredder/Sender  |
| Combine all components  | 1-2 weeks  | All previous tasks   |
| Release Software  | 1-2 weeks  | All previous tasks  |

# Feedback Timing

We will receive feedback from the instructor and TA's after the weekly class sessions, and any actionable feedback will be integrated into the team's goals immediately. All other feedback will be used to guide and shape our development precess. The team will meet immediately after receiving feedback from the instructors or TA's, to determine how best to integrate the feedback into the team goals or the team's development process. 

# Features (WIP)

Easy to use gui.
- Should be easy to understand.
- Should be aesthetic
- Some toggleable features

There should be several options for devices to connect to each other.
- One should be one device to generate a link for the other device to connect to.
- Another should be sharing one device via the existing software.

Sending and receiving data
Sending and receiving data directly from the original sender. The system does not use any third party host or server. Connected devices send directly to each other in the form of messages and files. The software can send different file types including large files. The software also has a simple messaging system for small texts and pings that users can send to each other. 

Encryption.
- The packets are encrypted and decrypted on the sender and host machines. Any information traversing the internet will not have decipherable data.
- If a packet goes missing, the receiving device will notice and ask for a resend.



2+ stretch goals you hope to implement.


“Offline” send option. Sender-side Queuing. Have the sender store unsent data and wait until the receiver is online to send. 

It should also be possible for each device to have a public table that tells the other devices how to connect to the other devices. As long as one device is still in the same place and can be found. The other devices can find it and share their new locations.


### Term Schedule 
#### Week 4
Milestone 3 - Project Architecture and Design
#### Week 5 
Milestone 4 - Implementation and Documentation
#### Week 6 
Milestone 4 - Implemnetation and Documentation
#### Week 7
Milestone 5 - Testing and Implementation
#### Week 8
Milestone 6 - Beta Release
#### Week 9 
Milestone 7 - Final Release
