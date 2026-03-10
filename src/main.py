# The main for running our P2Ping Software. 

import threading
import time
from tkinter import filedialog
import shutil
import os

import model.NetworkMain_windows as bridge
from view.gui import messaging_app

def setup_gui_logic(app):
    """Links GUI buttons to NetworkMain_windows functions."""
    
    def handle_connect():

        # If already connected, uses this button
        if app.connect_btn.cget("text") == "End Connection":
            bridge.terminate_program() # Or your disconnect logic
            bridge.startup()
            app.update_button_to_disconnect()
            return
        
        mode = app.get_connection_mode()
        if not mode: return
        
        result = None
        if mode.lower() in ['local', 'l']:
            endpoint = bridge.prepare_local_endpoint()
            app.update_ip_display(endpoint)
            other = app.get_peer_address("Enter OTHER terminal's port:", endpoint)
            if other:
                result = bridge.establish_local_connection(other)
        else:
            endpoint = bridge.prepare_public_endpoint()
            app.update_ip_display(endpoint)
            other = app.get_peer_address("Enter OTHER device's [ipv6]:port:", endpoint)
            if other:
                result = bridge.establish_connection(other)

        if result and not str(result).startswith("ERROR"):
            app.after(0, app.update_button_to_connected)
            app.after(0, app.show_chat_ui)
        else:
            # Keep it black, maybe show an error
            print("Connection failed or cancelled.")
            app.update_button_to_disconnected()

    def handle_send():
        msg = app.message_entry.get()
        if msg:
            bridge.send_message(msg)
            app.display_message("You", msg)
            app.message_entry.delete(0, 'end')
    
    def handle_file_selection():
        # This opens the standard OS file picker
        file_path = filedialog.askopenfilename(
            title="Select a file to send",
            filetypes=[("All Files", "*.*")]
        )
        
        if file_path:
            # Call your bridge function to send the file
            # Assuming bridge.send_file(path) exists in NetworkMain_windows.py
            bridge.send_file(file_path)
            file_name = file_path.split("/")[-1] # Get just the name
            app.display_message("You", f"Sent file: {file_name}")

    # Link to  button
    app.file_btn.configure(command=handle_file_selection)

    # Assign commands to GUI components
    app.connect_btn.configure(command=handle_connect)
    app.send_btn.configure(command=handle_send)
    app.bind("<Return>", lambda _: handle_send())

def message_poller(app):
    """Background thread to update GUI when new messages arrive."""
    while True:
        if bridge._runtime and bridge._runtime.running:
            msg = bridge.read_inc_message_queue()

        # 1. Ignore empty/error messages
        if not msg or msg == "" or msg.startswith("ERROR:"):
            time.sleep(0.2)
            continue

        if msg.startswith("FILE:"):
            temp_path = msg.split("FILE:")[1].strip()
            app.after(0, lambda p=temp_path: handle_incoming_file(app, p))
        else:
            # Use .after() to safely update GUI from background thread
            app.after(0, lambda m=msg: app.display_message("Peer", m))
        time.sleep(0.2)

def handle_incoming_file(app, temp_path):
    # 1. Extract the default filename from the temp path
    default_name = os.path.basename(temp_path)
    
    # 2. Ask user where they want to save it
    save_path = filedialog.asksaveasfilename(
        title="Save Incoming File",
        initialfile=default_name,
        defaultextension=".*"
    )
    
    # 3. If they didn't cancel, move the file from temp to their choice
    if save_path:
        try:
            shutil.move(temp_path, save_path)
            app.display_message("System", f"File saved to: {save_path}")
        except Exception as e:
            app.display_message("System", f"Failed to save file: {e}")

if __name__ == "__main__":
    # 1. Start C Bridge
    startup_info = bridge.startup()
    if startup_info:
        # 2. Initialize GUI
        app = messaging_app()
        setup_gui_logic(app)
        
        # 3. Start Poller
        threading.Thread(target=message_poller, args=(app,), daemon=True).start()
        
        # 4. Run App
        try:
            app.mainloop()
        finally:
            bridge.terminate_program()
