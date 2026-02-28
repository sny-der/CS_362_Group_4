'''
# This file contains driver prototypes and testing functionality for splitter.py

import splitter
from tkinter import Tk, filedialog
from pathlib import Path

Tk().withdraw()
filepath = filedialog.askopenfilename()  # Select file to upload
print("Selected:", filepath)


SAVE_DIR = Path("received_files") # Set downloaded file path to folder "received_files" in working directory
SAVE_DIR.mkdir(exist_ok=True)

file_name = Path(filepath).name  # strips directory traversal

output_path = SAVE_DIR / file_name # Add downloaded file name to file path

splitter.reassemble_file(splitter.split_file(filepath), output_path) # Split file from uploaded location, reassemble to chosen download location

'''
