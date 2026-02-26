import tkinter as tk
from tkinter import ttk

# -----------------------
# Main Window
# -----------------------
root = tk.Tk()
root.title("P2P Secure Chat")
root.geometry("900x600")
root.configure(bg="#0e1117")
root.resizable(False, False)

# -----------------------
# Header
# -----------------------
header = tk.Frame(root, bg="#0e1117")
header.pack(fill="x", padx=20, pady=15)

title = tk.Label(header, text="P2P", fg="white",
                 bg="#0e1117", font=("Segoe UI", 20, "bold"))
title.pack(side="left")

status = tk.Label(header,
                  text="🔒 Secure Connection: Encrypted & Private\nConnected to: 117.51.34.92",
                  fg="#8be9a3",
                  bg="#0e1117",
                  font=("Segoe UI", 10),
                  justify="right")
status.pack(side="right")

# -----------------------
# Chat Area (Canvas)
# -----------------------
chat_frame = tk.Frame(root, bg="#0e1117")
chat_frame.pack(fill="both", expand=True, padx=20)

canvas = tk.Canvas(chat_frame, bg="#0e1117", highlightthickness=0)
scrollbar = ttk.Scrollbar(chat_frame, orient="vertical", command=canvas.yview)

scrollable_frame = tk.Frame(canvas, bg="#0e1117")

scrollable_frame.bind(
    "<Configure>",
    lambda e: canvas.configure(scrollregion=canvas.bbox("all"))
)

canvas.create_window((0, 0), window=scrollable_frame, anchor="nw")
canvas.configure(yscrollcommand=scrollbar.set)

canvas.pack(side="left", fill="both", expand=True)
scrollbar.pack(side="right", fill="y")

# -----------------------
# Message Bubble Function (FIXED ALIGNMENT)
# -----------------------
def add_message(text, side="left"):
    row = tk.Frame(scrollable_frame, bg="#0e1117")
    row.pack(fill="x", pady=5)

    if side == "right":
        row.columnconfigure(0, weight=1)

        bubble = tk.Label(
            row,
            text=text,
            bg="#1f6f4a",
            fg="white",
            wraplength=400,
            justify="left",
            padx=15,
            pady=10,
            font=("Segoe UI", 11)
        )
        bubble.grid(row=0, column=1, sticky="e", padx=(50, 10))

    else:
        row.columnconfigure(1, weight=1)

        bubble = tk.Label(
            row,
            text=text,
            bg="#2a2f36",
            fg="white",
            wraplength=400,
            justify="left",
            padx=15,
            pady=10,
            font=("Segoe UI", 11)
        )
        bubble.grid(row=0, column=0, sticky="w", padx=(10, 50))

    # Auto-scroll
    canvas.update_idletasks()
    canvas.yview_moveto(1)

# -----------------------
# Demo Messages
# -----------------------
add_message("Hey, how’s it going?", "right")
add_message("Not bad! Just working on something new.", "left")

# -----------------------
# Bottom Input Bar
# -----------------------
input_frame = tk.Frame(root, bg="#161b22", height=70)
input_frame.pack(fill="x", side="bottom")

message_entry = tk.Entry(
    input_frame,
    bg="#0e1117",
    fg="white",
    insertbackground="white",
    font=("Segoe UI", 12),
    relief="flat"
)
message_entry.pack(side="left", fill="x", expand=True, padx=20, pady=15)

def send_message(event=None):
    text = message_entry.get()
    if text.strip():
        add_message(text, "right")
        message_entry.delete(0, tk.END)

send_button = tk.Button(
    input_frame,
    text="➤",
    command=send_message,
    bg="#238636",
    fg="white",
    font=("Segoe UI", 14, "bold"),
    relief="flat",
    width=4
)
send_button.pack(side="right", padx=20, pady=10)

# Press Enter to Send
message_entry.bind("<Return>", send_message)

root.mainloop()