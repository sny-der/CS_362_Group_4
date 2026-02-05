# P2Ping
# Team Members:

- Charles Weber: Code Integration Coordinator
- Abram Gallup: Architecture Design Coordinator
- Jonathan Snyder: QA Coordinator
- Arjun Rahul Bhave: Product & Requirements Coordinator


# Communication:
Our primary channel of communication will be discord, and we will have weekly in-person meetings.
# Rules:
- Maintain courtesy, respect, and professionalism in the group chats and at the meetings.
- Any conflict will be mediated by non-involved group members, or resolved through voting.
- Meeting notes will be recorded and shared to the group chat, to keep a record and to inform any members who may have had to miss the meeting.
- We will strive to maintain a welcoming environment at all times.



# Product description 

# Product Title: P2Ping

# Abstract: 
Ever wanted to send large amounts of data from here to there? Well we have. This project will allow users to securely transfer data between computers. Information is transferred bit by bit in units of information called “packets.” While data is being transferred, packets can occasionally go missing or be intercepted by a hostile actor. This program will ensure that all packets arrive securely. Packets that don’t arrive will be resent and “bad” packets sent by malicious actors will be ignored. Once the data transfer is complete, the sender and receiver will be notified.

# Goal: 

We aim to create an encrypted p2p messaging/file transfer system that does not require the use of an external server or service to store the data, instead allowing for a direct file transfer between 2 machines.

# Current practice: 

Files are transferred through services like Google Drive, which has restrictions on storage space, and uses a third party server. Torrenting is good for large amounts of data, but is difficult for less technical users to use.
   
# Novelty: 

Direct connection between machines.

# Effects: 
Peers will be able to directly send encrypted messages without having to use an external server, and without being limited by a third party’s maximum transfer size, enhancing ease and speed of communication and collaboration. 

# Technical approach: 

We will be using a variety of languages to implement the necessary features. The application that runs the program on the computer will probably be coded in c. Encryption and package management will probably be coded inc c++. Other languages like python or javascript might be used depending on the project requirements.

# Risks: 

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
#### HTML/CSS Plugin
- [Electron Framework](https://www.electronjs.org/)

## Software Design

### Components 

- Packet Assembler & Disassembler - disassembles and reassembles files to and from packets
- Sender/Receiver - sends and receives packets over networks
- Address translator - translates local addresses into valid format for transfer protocols
- GUI - provides interface for user

### Interfaces

### Data

### Alternatives

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

# Feedback Timing

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
