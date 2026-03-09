import customtkinter as ctk
from PIL import Image

class messaging_app(ctk.CTk):
    def __init__(self):
        super().__init__()

        self.title("P2Ping")
        self.geometry("1100x600")
        
        # Set appearance
        ctk.set_appearance_mode("black")
        
        # Configure Grid
        self.grid_columnconfigure(1, weight=1)
        self.grid_rowconfigure(0, weight=1)

        # --- SIDEBAR (Messages List) ---
        self.sidebar = ctk.CTkFrame(self, width=300, corner_radius=0, fg_color="#F8F9FA")
        self.sidebar.grid(row=0, column=0, sticky="nsew")
        self.sidebar.grid_propagate(False)

        # Header in Sidebar
        self.sidebar_label = ctk.CTkLabel(self.sidebar, text="Messages", 
                                         font=ctk.CTkFont(size=22, weight="bold"))
        self.sidebar_label.pack(padx=20, pady=(20, 10), anchor="w")

        # Search Bar
        self.search_bar = ctk.CTkEntry(self.sidebar, placeholder_text="🔍 Search conversations...",
                                      height=35, fg_color="#E9ECEF", border_width=0)
        self.search_bar.pack(padx=20, pady=10, fill="x")
        
        self.title("Contacts")
        self.entries = []
        
        # Input area to add new contacts (prototype, does not yet connect to real contacts)
        input_frame = ctk.CTkFrame(self.sidebar)
        input_frame.pack(fill="x", padx=20, pady=10)
        self.user_input = ctk.CTkEntry(input_frame, placeholder_text="Name", height=35)
        self.user_input.pack(side="left", fill="x", expand=True)
        add_btn = ctk.CTkButton(input_frame, text="Add", width=60, command=lambda: self.add_contact())
        add_btn.pack(side="right", padx=(10, 0))
        self.container = ctk.CTkFrame(self.sidebar)
        self.container.pack(fill="both", expand=True, padx=20, pady=(0, 20))

        # Mock Conversation List
        contacts = [
            ("Sarah Chen"),
            ("Design Team"),
            ("Mike Johnson"),
            ("Product Team")
        ]

        for name in contacts:
            btn = ctk.CTkButton(self.sidebar, 
                                text=f"{name}\n", 
                                anchor="w", 
                                fg_color="white", 
                                border_width=1,
                                border_color="#D1D1D1",
                                text_color="black", 
                                hover_color="#E9ECEF",
                                corner_radius=0,
                                height=60, 
                                font=ctk.CTkFont(size=13))
            btn.pack(fill="x", padx=5)

        status_dot = ctk.CTkFrame(btn, # Set the button as the parent
                                      width=10, 
                                      height=10, 
                                      corner_radius=5, 
                                      fg_color="#28A745", # "Online" Green
                                      border_width=0)
        status_dot.place(relx=0.07, rely=0.5, anchor="center")

        # --- MAIN CONTENT AREA ---
        self.main_view = ctk.CTkFrame(self, corner_radius=0, fg_color="white")
        self.main_view.grid(row=0, column=1, sticky="nsew")

        # Top Bar (IP and Connect)
        self.top_bar = ctk.CTkFrame(self.main_view, 
                                    height=50, 
                                    fg_color="transparent")
        self.top_bar.pack(fill="x", padx=20, pady=10)

        # DISPLAY CONNECTION INFORMATION
        self.ip_label = ctk.CTkTextbox(self.top_bar, 
                                    fg_color="#E9ECEF",
                                    corner_radius=6,
                                    height=60,
                                    font=ctk.CTkFont(size=13),
                                    padx=10)
        self.ip_label.pack(side="left")

        # Insert placeholder text to the IP address box and disable editing
        self.ip_label.insert("0.0", "Your IP: Fetching...")
        self.ip_label.bind("<Key>", lambda e: "break" if not (e.state & 0x4 and e.keysym == 'c') else None)
        self.ip_label.configure(state='normal')

        #BUTTON TO CONNECT TO ANOTHER USER
        self.connect_btn = ctk.CTkButton(self.top_bar, 
                                        text="Connect to Peers", 
                                        fg_color="black", 
                                        text_color="white", 
                                        width=140,
                                        height=60,
                                        font=ctk.CTkFont(size=13),
                                        command=self._on_connect_clicked)
        self.connect_btn.pack(side="right")

        # --- 1. THE EMPTY STATE
        self.center_container = ctk.CTkFrame(self.main_view, fg_color="transparent")
        self.center_container.place(relx=0.5, rely=0.5, anchor="center")

        self.icon_label = ctk.CTkLabel(self.center_container, text="💬", font=("Arial", 60))
        self.icon_label.pack()

        self.title_label = ctk.CTkLabel(self.center_container, text="Select a conversation", 
                                       font=ctk.CTkFont(size=20, weight="bold"))
        self.title_label.pack(pady=(10, 0))

        self.connect_info_label = ctk.CTkLabel(self.center_container, 
                                              text="Connect to Peer for messaging and file transfers", 
                                              font=ctk.CTkFont(size=15),
                                              text_color="#333333")
        self.connect_info_label.pack(pady=2)

        self.sub_label = ctk.CTkLabel(self.center_container, 
                                     text="Choose a conversation from the sidebar to start messaging",
                                     text_color="gray")
        self.sub_label.pack(pady=5)

        # --- 2. THE CHAT INTERFACE
        self.chat_frame = ctk.CTkFrame(self.main_view, fg_color="white", corner_radius=0)
        
        self.chat_display = ctk.CTkTextbox(self.chat_frame, fg_color="#F8F9FA", text_color="black", state="disabled")
        self.chat_display.pack(expand=True, fill="both", padx=20, pady=10)

        self.input_area = ctk.CTkFrame(self.chat_frame, fg_color="transparent")
        self.input_area.pack(fill="x", padx=20, pady=(0, 20))

        self.message_entry = ctk.CTkEntry(self.input_area, placeholder_text="Type a message...")
        self.message_entry.pack(side="left", fill="x", expand=True, padx=(0, 10))

        self.send_btn = ctk.CTkButton(self.input_area, text="Send", width=80)
        self.send_btn.pack(side="right")

        self.file_btn = ctk.CTkButton(self.chat_frame, 
                               text="📁", 
                               width=40, 
                               command=None) #link in WinNetworkMain.py
        self.file_btn.pack(side="left", padx=5)

    def show_chat_ui(self):
        """Hides the empty state and shows the chat window."""
        self.center_container.place_forget() # Remove the 'Select a conversation' UI
        self.chat_frame.pack(fill="both", expand=True) # Show the chat box and input

    def update_ip_display(self, endpoint_string):
            """Call this to change the text of the IP label."""
            self.ip_label.configure(state="normal")
            self.ip_label.delete("0.0", "end")
            self.ip_label.insert("0.0", f"Your IP: {endpoint_string}")
            self.ip_label.configure(state="normal")

    def get_connection_mode(self):
        """Asks the user for Local or Public mode."""
        dialog = ctk.CTkInputDialog(text="Type 'local' or 'public':", title="Select Mode")
        return dialog.get_input()
    
    def get_peer_address(self, prompt_text, my_ip):
        """Asks for the peer's port or IP while showing our own IP in the prompt."""
        # full_prompt = f"Your IP: {my_ip}\n\n {prompt_text}"
        dialog = ctk.CTkInputDialog(text=prompt_text, title="Connect to Peer")

        def copy_to_clipboard():
            self.clipboard_clear()
            self.clipboard_append(my_ip)
            copy_btn.configure(text="Copied!", fg_color="green")
            # Reset button text after 2 seconds
            self.after(2000, lambda: copy_btn.configure(text="Copy", fg_color="black"))

        ip_display = ctk.CTkEntry(dialog, width=400, border_width=0, fg_color="transparent")
        ip_display.insert(0, f"Your IP: {my_ip}")
        ip_display.configure(state="normal")
        ip_display.bind("<Key>", lambda e: "break" if not (e.state & 0x4 and e.keysym == 'c') else None)
        ip_display.grid(row=3, column=0, columnspan=2, pady=10, padx=(20, 5), sticky="w")

        copy_btn = ctk.CTkButton(dialog, text="Copy", width=60, height=24, 
                               fg_color="black", command=copy_to_clipboard)
        copy_btn.grid(row=3, column=2, pady=(20, 5), padx=(5, 20), sticky="w")

        return dialog.get_input()

    def show_status(self, message):
        """Displays status messages (like 'Public peer configured') in the UI.""" 
        # Not implemented yet
        print(f"GUI STATUS: {message}")

    def _on_connect_clicked(self):
        """This will be overwritten or assigned by the controller"""

    def display_message(self, sender, message):
        self.chat_display.configure(state="normal")
        self.chat_display.insert("end", f"{sender}: {message}\n")
        self.chat_display.configure(state="disabled")
        self.chat_display.see("end")
    def add_contact(self): # Prototype for adding contacts. Does not yet fill the sidebar with real contacts or connect to them.
            entry_text = self.user_input.get()
            if entry_text:
                row_frame = ctk.CTkFrame(self.container)
                row_frame.pack(fill="x", pady=2)
                entry_label = ctk.CTkEntry(row_frame)
                entry_label.insert(0, entry_text)
                entry_label.pack(fill="x", padx=5, expand=True)
                lbtn = ctk.CTkButton(row_frame, text="X", width=80, command=lambda: row_frame.destroy())
                lbtn.pack(side="right", padx=(10, 0))
                self.entries.append((entry_label, row_frame))
                self.user_input.delete(0, "end")

if __name__ == "__main__":
    app = messaging_app()
    app.mainloop()
