import customtkinter as ctk
from PIL import Image

class MessagingApp(ctk.CTk):
    def __init__(self):
        super().__init__()

        self.title("P2Ping")
        self.geometry("1100x600")
        
        # Set appearance
        ctk.set_appearance_mode("system")
        
        # Configure Grid
        self.grid_columnconfigure(1, weight=1)
        self.grid_rowconfigure(0, weight=1)

        # --- SIDEBAR (Messages List) ---
        self.sidebar = ctk.CTkFrame(self, width=300, corner_radius=0, fg_color="#F8F9FA")
        self.sidebar.grid(row=0, column=0, sticky="nsew")
        self.sidebar.grid_propagate(False)

        # Header in Sidebar
        self.sidebar_label = ctk.CTkLabel(self.sidebar, text="Messages", 
                                          text_color="black",
                                         font=ctk.CTkFont(size=22, weight="bold"))
        self.sidebar_label.pack(padx=20, pady=(20, 10), anchor="w")

        # Search Bar
        self.search_bar = ctk.CTkEntry(self.sidebar, placeholder_text="🔍 Search conversations...",
                                      height=35, fg_color="#E9ECEF", border_width=0)
        self.search_bar.pack(padx=20, pady=10, fill="x")

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

        #INSERT LOGIC TO GET IP ADDRESS NEEDED FOR CONNECTIONS
        self.ip_label = ctk.CTkLabel(self.top_bar, text="Your IP: 173.240.196.7", 
                                    fg_color="#E5E5E5",
                                    text_color="black",
                                    corner_radius=6,
                                    height=60,
                                    font=ctk.CTkFont(size=13),
                                    padx=10)
        self.ip_label.pack(side="left")

        #BUTTON TO CONNECT TO ANOTHER USER
        self.connect_btn = ctk.CTkButton(self.top_bar, 
                                        text="Connect to Peers", 
                                        fg_color="black", 
                                        text_color="white", 
                                        width=140,
                                        height=60,
                                        font=ctk.CTkFont(size=13))
        self.connect_btn.pack(side="right")

        # Center Empty State
        self.center_container = ctk.CTkFrame(self.main_view, fg_color="transparent")
        self.center_container.place(relx=0.5, rely=0.5, anchor="center")

        self.icon_label = ctk.CTkLabel(self.center_container, text="💬", font=("Arial", 60))
        self.icon_label.pack()

        self.title_label = ctk.CTkLabel(self.center_container, text="Select a conversation", 
                                        text_color="#333333",
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

if __name__ == "__main__":
    app = MessagingApp()
    app.mainloop()