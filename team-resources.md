# P2Ping
# Team Members:

- Charles Weber: Back-end Developer
    * Design/Implement network connection protocols
    * Design/Implement Sender/Receiver components
- Abram Gallup: Back-end Developer
    * Design/Implement Encryption
    * Design/Implement Database
- Jonathan Snyder: Back-end Developer
    * Design/Implement File Shredder/Recombiner
    * Design/Implement File Manager
- Arjun Rahul Bhave: Front-end developer
    * Design/Implement GUI
    * Design/Implement Controller component


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

- Cross-Platform File Path Inconsistency

> Likelihood of Occurring: Medium

> Impact: Medium

> Evidence: Different operating systems handle file paths differently. MacOS/Linux use forward slashes and case-sensitive filenames, while Windows uses backslashes and is case-insensitive, which can crash file-saving logic.

> Steps: Currently we plan to use Python's pathlib library to handle paths agnostically. 

> Mitigation: The app will be kept Windows-only until we can successfully interact across operating systems.

> Change from first submission: This requirement and this assessment are new additions for this deliverable. 

- Networking Environment Variablilty (NAT Types)

> Likelihood of Occurring: Low

> Impact: Medium

> Evidence: Some firewalls use Symmetric NAT, which specifically  blocks UDP Hole Punching, preventing a P2P connection even if our code is perfect

> Steps: We will identify any testing environments that will work with our app to perform tests

> Mitigation:  We will test across different types (Home Wi-Fi vs. Mobile Hotspot) to document connectivity limits.

> Change from first submission: This requirement and this assessment are new additions for this milestone.
  
- Cannot implement a robust encryption algorithm that runs smoothly on the users deivce.

> Likelihood of Occurring: Unlikely
  
> Impact: Low

> Evidence: Encryption algorithms can use a lot of memory, which leads to poor performance and slow processing.

> Steps: We will use the cryptography library, which uses C-bindings for speed.

> Mitigation: We will use chunk-based processing to lower memory usage.

> Change from first submission: This risk and this assessment are new for this submission.
 
- Not being able to resolve local addresses, to establish connections.

> Likelihood of Occurring: Possible

> Impact: Medium

> Evidence: Based on the network protocol used, and the network the user is connected to, their IP address can change between app shutdown and startup, or the network port the user can use to send and receive data can be different depending on where they are sending data to.

> Steps: We are using connection and transfer protocols that are more likely to allow users access to the same ports. 

> Mitigation: We will rely on the users using external means to verify each others' address, which also ensures that they are only sending and receiving data to/from who they want to.

> Change from first submission: This risk assessment uses more concrete language to describe the risk and the team's response.

- Main Thread Blocking (GUI Freezing)

> Likelihood of Occurring: Likely

> Impact: High

> Evidence: The network and transfer logic could be resource intensive enough to slow down or prevent the UI from functioning, preventing the user from using the app.

> Steps: We will use threading or asyncio libraries to prevent the threads from trying to access the same resource at the same time.

> Mitigation: We can restrict how resource-intensive other threads are, in order to prevent race conditions or thread locking.

> Change from first submission: This risk and assessment are new additions to this submission.

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

- File Splitter - disassembles and reassembles files to and from chunks, creates index and size of chunks which allows for transmission of files while maintaining direct control of chunks.
- GUI - provides interface for user, made with PySide/PyQT
- Sender: Sends chunks and metadata using UDP protocols.
- Receiver: Receives metadata and chunks, verifies authenticity of received chunks and unpacks them, requests any missing chunks or packets.
- Connection Manager: Creates and manages connection between users using UDP connection and transfer protocols
- Location Finder: Finds/translates IP addresses to enable direct connection
- Main: Handles overall execution of program
- File Manager: Communicates with OS to set pathing for received files, or to upload files
- Encryption: Encrypts chunks, utilizes a Diffie-Hellman based algorithm to allow users to generate shared keys.

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

- Gluonix designer - platform to build GUIs for python programs. Free to use and license, and has easy to use tools, but it is a little harder to use than Pyside/PyQT.
- aiortc - python framework designed to handle network connections and transfers. Would be easier to use and implement than using UDP the way we are, but would not allow us the level of control over the protocols that we desire, and could introduce a reliance on external servers to establish and maintain connections.


### Classes / Units of Abstraction


## Coding Guideline
- Python: [PEP 8](https://realpython.com/python-pep8/#code-layout)
- To adhere with this guideline, code commits or pull requests will be reviewed by another team member. The author will be notified of any changes that need to be made, or had to be made. 

# Team Roles

## Charles Weber
Charles is taking on the role of a back-end developer, focusing on network connection and transport protocols. These components are necessary because they form the core functionality of this software. Charles is taking on this role because he has the most experience with network protocols, so he is best suited to this role.

## Abrum Gallup
Abrum is taking on the role of a back-end developer, focusing on encryption and databases. These components are necessary because they ensure the safety and continued use of this software. Abrum is the most familiar with these programs, so he is best suited to this role.

## Jonathan Snyder
Jonathan is taking on the role of a back-end developer, focusing on file processing and management. These components are necessary because they allow our software to transmit saved files and to save files to the machine. Jonathan is the most familiar with these programs, so he is best suited to this role.

## Arjun Rahul Bhave
Arjun is taking on the role of a front-end developer, focusing on creating the GUI and how it interfaces with the rest of the software. These components are necessary because they allow use to provide a simplified user experience, which is one of our main goals. Arjun is the most familiar with GUI creation, so he is best suited to this role.

# Schedule

| Task  | Effort  | Dependencies  | Date|
|---|---|---|---|
| Milestone 1  | 1 week  | none | 1/15/26 |
|Milestone 2| 1 week | Milestone 1 | 1/21/26 |
| Milestone 3  | 2 weeks  |  Milestone 2 | 2/4/26 |
| Design Architecture  | 2 Days  | Requirements  | 2/2/26 |
| Create Structure Design  | 2 Days  | Architecture  | 2/4/26 |
| Create Schedule  | 1 day  | Structure  | 2/4/26 |
| Create GUI Mockup  | 1 day  | Structure  | 2/9/26 |
| Milestone 4  | 2 weeks  | Milestone 3  | 2/18/26 |
| Build basic GUI setup  | 3 days  | GUI Mockup | 2/20/26 |
| Implement Holepunch  | 3 days  | Structure  | 2/20/26 |
| Test Connection Establishment | 1 day | Holepunch | 2/21/26 |
| Implement Sender  | 3 days  | Holepunch  | 2/24/26 |
| Implement Receiver  | 3 days  | Holepunch  | 2/24/26 |
|Test Network Connections | 3 days | Sender/Receiver/Holepunch | 2/27/26 |
| Implement Up/Download Tracker  | 1 day  | Sender/Receiver  | 2/28/26 |
| Implement File Shredder  | 1 week  | Structure  | 2/18/26 |
| Implement File Recombiner  | 2 days  | File Shredder  | 2/20/26 |
| Milestone 5 | 2 days | Milestone 4 | 2/25/26 |
| Test Chunking Logic | 3 days | Shredder/Recombiner | 2/23/26 |
| Implement File Manager  | 2 days  | Recombiner  | 2/26/26 |
| Test OS Pathing | 2 days | File Manager | 2/28/26 |
| Implement Encryption  | 4 days  | Shredder/Sender  | 2/26/26 |
| Test Encryption | 2 days | Encryption | 2/28/26 |
| Combine all components  | 3 days  | All previous tasks   | 3/2/26 |
| Milestone 6  | Combine Components | 2 days | 3/3/26 |
| Test Integration | 3 days | Combine components | 3/6/26 |
| Test Usability | 2 days | Combine components/Test Integration | 3/5/26 |
| Finalize P2Ping 1.0 | 1 week  | All previous tasks  | 3/8/26 |
| Milestone 7 | 2 days | Finalize P2Ping 1.0 | 3/9/26 |

# Feedback Timing

We will receive feedback from the instructor and TA's after the weekly class sessions, and any actionable feedback will be integrated into the team's goals immediately. All other feedback will be used to guide and shape our development precess. The team will meet immediately after receiving feedback from the instructors or TA's, to determine how best to integrate the feedback into the team goals or the team's development process. 

# Features

### 1. Easy Discovery of Peers
- Peers discover each other though a quick message sent via text or other secondary method of communication. 
- Takes care of the “who/where are you?” without complicated hash tables like ones that Torrents use.

### 2. Easy “one-click” Connection
- The progam is as conveinent and quick to use as popular 3rd party servers like Google Drive, and Discord by quickly punching through each peer’s firewall, and securing a connection. 
- To ensure connectivity across heterogeneous network environments, we implement STUN-assisted UDP Hole Punching. This automates the process of 'punching' through NAT firewalls, establishing direct peer-to-peer tunnels without requiring manual router configuration by the user.

### 3. Decentralized Data Transfer
- Sending and receiving data directly from the original sender so that the system does not use any third party host or server. This avoids any unessasry fees that servers like google drive or discord ask for or requrie. 
- Connected devices send directly to each other in the form of messages and files. The software can send different file types including large files. The software also has a simple messaging system for small texts and pings that users can send to each other.
- Our transfer engine utilizes a binary chunking protocol to facilitate high-performance, serverless data exchange. By managing data in discrete segments with sequence-verification, we ensure 100% data integrity and optimal memory management during large-scale file transfers.

### 4. A Zero Knowledge Handshake
- All encryption in done behind the scenes with no thought or action by the users.
- Ensures privacy and peace of mind knowing shared files are stored locally and not constantly on the internet suseptible to online attacks. 
- We utilize a Diffie-Hellman (ECDH) handshake to establish end-to-end encryption (E2EE) at the session layer. This ensures a 'Zero-Knowledge' architecture where a shared secret is derived locally on each peer, providing Perfect Forward Secrecy for all transmitted data.


### Stretch goals


1. “Offline” send option. Sender-side Queuing. Have the sender store unsent data and wait until the receiver is online to send. 

2. It should also be possible for each device to perform a "peer exchange" that tells devices how to connect to the other devices. As long as one device is still in the same place and can be found. The other devices can find it and share their new locations.

# Documentation Plan

We plan to release a user guide with the system, explaining how to connect to other users, how to upload files, and how to set file paths or names. This will be developed after all components have been integrated, and will be modified after usability testing based on user feedback. We also plan to release dev guides with the software, detailing core components and how they function. This will be developed as each component is completed, which will also enable the team to better understand components they do not create.

# Testing Plan 

### 1. Unit Testing Strategy
Focus: Testing individual functions in isolation (no network involved).

- Testing: Cryptographic functions (Key derivation), File Chunking logic, and IP validation regex.

- This is so we can ensure the math and data-handling logic are perfect before adding the unpredictability of a network.

- We will use the PyTest framework to run a suite of "Assert" tests.
- Example: A unit test will feed a 1MB file into our Chunker and verify that it produces exactly 16 chunks of 64KB, and that the Checksum matches the original.

- Boundary Testing: We will test "Empty File" and "Extremely Large File Name" scenarios to ensure the code doesn't crash on edge cases.

### 2. System (Integration) Testing Strategy
Focus: Testing the interaction between the Networking, Encryption, and Storage layers.

- Testing: The "Handshake" flow, the NAT traversal success rate, and end-to-end file integrity.

- We want to test this becasue in P2P, bugs can happen when two different computers try to "talk" at the same time.

- We will run two instances of the app on one machine using 127.0.0.1 to test the state machine.

- Virtual Network Simulation: Using tools or multiple laptops to simulate high latency and packet loss. We need to see if our "Chunking" protocol can handle a "dropped" packet without corrupting the whole file.

- Cross-OS Testing: Validating that a file sent from Windows is saved correctly on a Linux/Mac machine (verifying the pathlib mitigation).

### 3. Usability Testing Strategy
Focus: The human element and the "Zero-Knowledge" requirement.

- Testing: The "One-Click" connection feature and the clarity of the Manual Connection String fallback.

- This is becasue if a user can’t figure out how to share their connection string, the "Easy Discovery" feature has failed.

- Black-Box Testing: We will give the app to a person who has not worked on the code. We will provide them with a peer's connection string and observe if they can establish a connection and send a file without asking the developers for help.

- Latency Perception: Measuring the "time to connect." If the NAT hole-punching takes more than 10 seconds, we need to implement a "Loading" bar to prevent the user from thinking the app is frozen.

### 4. Specific Test Suites

To ensure all core requirements are met, we have identified four specific test suites that map directly to our project features and high-level risks.

| Suite Name | Requirement Mapping | Test Case Description |
| :--- | :--- | :--- |
| **Crypto-Validation Suite** | Feature 4: Zero-Knowledge Handshake | **Secret Agreement Verification:** Uses mocked network inputs to ensure Peer A and Peer B derive an identical AES-256 session key via ECDH without ever transmitting the key itself. |
| **Stream-Integrity Suite** | Feature 3: Binary Chunking Protocol | **Corruption & Sequence Testing:** Simulates a "dropped" data chunk or an out-of-order packet to verify that the binary chunking engine correctly requests re-transmission and maintains file integrity. |
| **Connectivity-Matrix Suite** | Feature 2: NAT Traversal | **Heterogeneous Network Test:** Validates the STUN-assisted hole punching success rate by testing across different NAT types (e.g., Home Router to 5G Mobile Hotspot). |
| **Discovery & Fallback Suite** | Feature 1: Peer Discovery | **Re-addressing Persistence:** Simulates a dynamic IP change on one peer and verifies that the system can re-establish the link using the "Manual Connection String" fallback. |

* **Regression Strategy:** These suites will be executed after every major feature merge to ensure that new code (such as UI updates) does not introduce regressions into the sensitive networking or cryptographic layers.
* **Success Metric:** A test is considered "Passed" only if the SHA-256 hash of the received file perfectly matches the source file hash after being processed through the encryption and chunking pipeline.
