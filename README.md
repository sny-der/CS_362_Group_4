# CS_362_Group_4
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

We foresee a problem regarding different operating systems and testing environments. Aside from the problems of common library files, we also have the problem of inter device communication. We will have to ensure that the method of transference works on all devices and between devices.

In order to mitigate these risks we will first ensure that all of our devices can run the languages and libraries that we are using. We will also be sure that the information transferred does not rely on other software to decipher. With this we will ensure that when problems arise it is possible for us to solve them.

# Use Cases
Use Case 1:
- Users who want to receive files through this software
- The user launches the software
- The user receives files from another specific user as they desire
- The user launches the software and is able to view their own address, either through the main display or sub menus, and sends this address to another user, who is able to directly connect to the machine at that address and send the specified files.
- The user is able to download those files, or is able to send those files to another user
- The user is unable to view their own address, or is unable to be found through the software

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

Parallel tunnels. Users should be able to connect to several devices at once. Each established connection should have an On and Off switch. The user should be able to enable and disable each individual connection and the entire software at will.


Encryption.
- The packets are encrypted and decrypted on the sender and host machines. Any information traversing the internet will not have decipherable data.
- If a packet goes missing, the receiving device will notice and ask for a resend.



2+ stretch goals you hope to implement.


“Offline” send option. Sender-side Queuing. Have the sender store unsent data and wait until the receiver is online to send. 

It should also be possible for each device to have a public table that tells the other devices how to connect to the other devices. As long as one device is still in the same place and can be found. The other devices can find it and share their new locations.
