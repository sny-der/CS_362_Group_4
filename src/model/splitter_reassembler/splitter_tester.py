import splitter
from tkinter import Tk, filedialog
from pathlib import Path

Tk().withdraw()
filepath = filedialog.askopenfilename()
print("Selected:", filepath)


SAVE_DIR = Path("received_files")
SAVE_DIR.mkdir(exist_ok=True)

file_name = Path(filepath).name  # strips directory traversal

output_path = SAVE_DIR / file_name

splitter.reassemble_file(splitter.split_file(filepath), output_path)




