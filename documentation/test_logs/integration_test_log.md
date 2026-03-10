# Automated Testing
The main branch of the repository utilizes a workflow which contains a linter to ensure that all integrated python code is valid. The test directory contains a script that was used to ensure the pybridge functionality was and is performing correctly.

# System 1.0 Tests
Integrity and proper functioning of the overall system will be tested by manually sending files and messages through public and local connections. The app will be launched, and messages will be sent from each end, files will be sent from one end. The test will be successful if both the receiver and sender can view message content, and/or the sender can upload a file to the receiver, who can then view and access that file through their file explorer.

## Logged Tests
1. Local message transfer: Successful
2. Local file transfer (PNG): Successful
3. Local file transfer (MP3): Successful
4. Public message transfer: Successful
5. Public file transfer (PNG): Successful
6. Public file transfer (MP3): Successful
